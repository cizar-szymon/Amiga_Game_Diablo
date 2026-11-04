/**
 * @file soundsample_amiga.cpp
 *
 * Warp port: SoundSample on the Paula mixer (port/sdl/src/amiga_sound.c)
 * instead of SDL_audiolib. A WAV file is converted once, when loaded, to
 * signed 8-bit mono 22050 Hz (the format of nearly all of Diablo's
 * effects: then it is a plain copy); 16-bit samples keep their high byte,
 * stereo is averaged, other rates are stepped to 22050 Hz (nearest
 * sample). Volume and pan follow the formulas of soundsample.cpp
 * (millibel to linear, pan as an attenuation of the far side), as 0..64.
 */
#include "utils/soundsample.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "engine/assets.hpp"
#include "utils/amiga_wavstream.hpp"
#include "utils/log.hpp"

extern "C" {
int amiga_snd_play(const signed char *data, long len, int vl, int vr, int loop);
int amiga_snd_playing(int handle);
void amiga_snd_stop(int handle);
void amiga_snd_stop_data(const signed char *data);
void amiga_snd_volume(int handle, int vl, int vr);
int amiga_snd_stream_start(int slot, int stereo, int vl, int vr);
void amiga_snd_stream_stop(int slot);
void amiga_snd_stream_volume(int slot, int vl, int vr);
void amiga_snd_stream_end(int slot);
int amiga_snd_stream_playing(int slot);
long amiga_snd_stream_space(int slot);
void amiga_snd_stream_write(int slot, const signed char *frames, long n);
}

namespace devilution {

namespace {

constexpr int OutRate = 22050;
/** soundsample.cpp: -10 dB is half the perceived loudness */
constexpr float VolumeScale = 3321.9281F;
constexpr float MillibelMin = -10000.F;
constexpr float StereoSeparation = 6000.F;

int VolumeTo64(int logVolume, int logMin, int logMax)
{
	if (logMax == logMin)
		return 64;
	logVolume = std::clamp(logVolume, std::min(logMin, logMax), std::max(logMin, logMax));
	const float scaled = MillibelMin + (0.F - MillibelMin) * static_cast<float>(logVolume - logMin) / static_cast<float>(logMax - logMin);
	const float linear = std::pow(10.F, scaled / VolumeScale);
	return std::clamp(static_cast<int>(linear * 64.F + 0.5F), 0, 64);
}

uint16_t Le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

/** WAV (PCM 8/16 bit, mono/stereo, any rate) -> signed 8-bit mono 22050 Hz. */
bool ConvertWav(const uint8_t *wav, size_t size, ArraySharedPtr<std::uint8_t> &out, long &outLen)
{
	if (size < 12 || std::memcmp(wav, "RIFF", 4) != 0 || std::memcmp(wav + 8, "WAVE", 4) != 0)
		return false;
	unsigned format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	const uint8_t *data = nullptr;
	uint32_t dataLen = 0;
	size_t o = 12;
	while (o + 8 <= size) {
		const uint32_t n = Le32(wav + o + 4);
		if (std::memcmp(wav + o, "fmt ", 4) == 0 && o + 24 <= size) {
			format = Le16(wav + o + 8);
			channels = Le16(wav + o + 10);
			rate = Le32(wav + o + 12);
			bits = Le16(wav + o + 22);
		} else if (std::memcmp(wav + o, "data", 4) == 0) {
			data = wav + o + 8;
			dataLen = static_cast<uint32_t>(std::min<size_t>(n, size - (o + 8)));
			break;
		}
		o += 8 + n + (n & 1);
	}
	if (data == nullptr || format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate == 0)
		return false;
	const unsigned frameBytes = channels * (bits / 8);
	const uint32_t frames = dataLen / frameBytes;
	// output frames: frames * 22050 / rate, without 64-bit products
	const uint32_t outFrames = static_cast<uint32_t>((static_cast<double>(frames) * OutRate) / rate);
	if (outFrames == 0)
		return false;
	out = MakeArraySharedPtr<std::uint8_t>(outFrames);
	uint8_t *dst = out.get();
	uint32_t idx = 0, frac = 0;
	const uint32_t whole = rate / OutRate, part = rate % OutRate;
	for (uint32_t i = 0; i < outFrames && idx < frames; i++) {
		const uint8_t *s = data + idx * frameBytes;
		int v;
		if (bits == 8) {
			v = static_cast<int>(s[0]) - 128;
			if (channels == 2)
				v = (v + static_cast<int>(s[1]) - 128) >> 1;
		} else {
			v = static_cast<int8_t>(s[1]);
			if (channels == 2)
				v = (v + static_cast<int8_t>(s[3])) >> 1;
		}
		dst[i] = static_cast<uint8_t>(static_cast<int8_t>(v));
		idx += whole;
		frac += part;
		if (frac >= OutRate) {
			frac -= OutRate;
			idx++;
		}
	}
	outLen = static_cast<long>(outFrames);
	return true;
}

uint8_t streamIn[8192];
signed char streamOut[16384];

/** Speech: one stream at a time (effects.cpp plays one), owned by the sample playing it. */
AmigaWavStream speech;
const SoundSample *speechOwner = nullptr;

} // namespace

// ---- AmigaWavStream ----

bool AmigaWavStream::Open(const char *path, uint32_t *frames22k)
{
	Stop();
	handle_ = nullptr;
	AssetRef ref = FindAsset(path);
	if (!ref.ok())
		return false;
	auto handle = std::make_unique<AssetHandle>(OpenAsset(std::move(ref)));
	if (!handle->ok())
		return false;
	uint8_t head[12];
	if (!handle->read(head, 12) || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
		return false;
	long at = 12;
	unsigned format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	for (int i = 0; i < 16; i++) {
		uint8_t ch[8];
		if (!handle->read(ch, 8))
			return false;
		const uint32_t n = Le32(ch + 4);
		at += 8;
		if (std::memcmp(ch, "data", 4) == 0) {
			if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate == 0) {
				LogError(LogCategory::Audio, "unsupported WAV {} ({} ch, {} bit, {} Hz)", path, channels, bits, rate);
				return false;
			}
			handle_ = std::move(handle);
			dataStart_ = at;
			dataLen_ = n;
			pos_ = 0;
			channels_ = channels;
			bits_ = bits;
			rate_ = rate;
			frac_ = 0;
			skip_ = 0;
			if (frames22k != nullptr) {
				const uint32_t frames = n / (channels * (bits / 8));
				*frames22k = static_cast<uint32_t>(static_cast<double>(frames) * OutRate / rate);
			}
			return true;
		}
		if (std::memcmp(ch, "fmt ", 4) == 0 && n >= 16) {
			uint8_t f[16];
			if (!handle->read(f, 16))
				return false;
			format = Le16(f);
			channels = Le16(f + 2);
			rate = Le32(f + 4);
			bits = Le16(f + 14);
		}
		at += n + (n & 1);
		if (!handle->seek(at))
			return false;
	}
	return false;
}

bool AmigaWavStream::Start(int slot, bool loop, int vl, int vr)
{
	if (handle_ == nullptr)
		return false;
	slot_ = slot;
	loop_ = loop;
	ended_ = false;
	if (amiga_snd_stream_start(slot, channels_ == 2 ? 1 : 0, vl, vr) != 0)
		return false;
	on_ = true;
	Pump(); // fill the ring before it plays out
	return true;
}

void AmigaWavStream::Stop()
{
	if (on_)
		amiga_snd_stream_stop(slot_);
	on_ = false;
	handle_ = nullptr;
}

bool AmigaWavStream::Playing() const
{
	return on_ && amiga_snd_stream_playing(slot_) != 0;
}

void AmigaWavStream::SetVolume(int vl, int vr)
{
	if (on_)
		amiga_snd_stream_volume(slot_, vl, vr);
}

void AmigaWavStream::Pump()
{
	if (!on_ || handle_ == nullptr || ended_)
		return;
	const unsigned frameBytes = channels_ * (bits_ / 8);
	bool ok = true;
	for (int round = 0; round < 8 && ok; round++) {
		const long space = amiga_snd_stream_space(slot_);
		if (space < 2048)
			return;
		if (pos_ >= dataLen_) {
			if (!loop_) {
				amiga_snd_stream_end(slot_); // plays out what is in the ring
				ended_ = true;
				return;
			}
			if (!handle_->seek(dataStart_)) {
				ok = false;
				break;
			}
			pos_ = 0;
		}
		// input frames for at most `space` output frames (and the buffers)
		uint32_t inFrames = static_cast<uint32_t>(static_cast<double>(space - 2) * rate_ / OutRate);
		inFrames = std::min<uint32_t>(inFrames, sizeof(streamIn) / frameBytes);
		inFrames = std::min<uint32_t>(inFrames, (sizeof(streamOut) / channels_) * rate_ / OutRate / 2);
		inFrames = std::min<uint32_t>(inFrames, (dataLen_ - pos_) / frameBytes);
		if (inFrames == 0) {
			pos_ = dataLen_;
			continue;
		}
		if (!handle_->read(streamIn, inFrames * frameBytes)) {
			ok = false;
			break;
		}
		pos_ += inFrames * frameBytes;
		signed char *out = streamOut;
		long outFrames = 0;
		auto sampleAt = [&](uint32_t f, unsigned c) -> signed char {
			const uint8_t *p = streamIn + f * frameBytes + c * (bits_ / 8);
			return bits_ == 16 ? static_cast<signed char>(p[1]) : static_cast<signed char>(static_cast<int>(p[0]) - 128);
		};
		if (rate_ == static_cast<uint32_t>(OutRate)) {
			for (uint32_t f = 0; f < inFrames; f++)
				for (unsigned c = 0; c < channels_; c++)
					*out++ = sampleAt(f, c);
			outFrames = inFrames;
		} else {
			// nearest sample: input index steps rate/22050 per output frame
			uint32_t idx = skip_;
			while (idx < inFrames) {
				for (unsigned c = 0; c < channels_; c++)
					*out++ = sampleAt(idx, c);
				outFrames++;
				frac_ += rate_;
				while (frac_ >= static_cast<uint32_t>(OutRate)) {
					frac_ -= OutRate;
					idx++;
				}
			}
			skip_ = idx - inFrames;
		}
		amiga_snd_stream_write(slot_, streamOut, outFrames);
	}
	if (ok)
		return;
	// a read failed: what is in the ring plays out
	LogError(LogCategory::Audio, "sound stream: read failed");
	amiga_snd_stream_end(slot_);
	ended_ = true;
}

// ---- SoundSample ----

SoundSample::SoundSample(SoundSample &&other) noexcept
{
	*this = std::move(other);
}

SoundSample &SoundSample::operator=(SoundSample &&other) noexcept
{
	if (this != &other) {
		Release();
		if (speechOwner == &other)
			speechOwner = this;
		file_data_ = std::move(other.file_data_);
		file_data_size_ = other.file_data_size_;
		file_path_ = std::move(other.file_path_);
		isMp3_ = other.isMp3_;
		pcm_ = std::move(other.pcm_);
		pcmLen_ = other.pcmLen_;
		voice_ = other.voice_;
		volume_ = other.volume_;
		gainL_ = other.gainL_;
		gainR_ = other.gainR_;
		muted_ = other.muted_;
		other.pcmLen_ = 0;
		other.voice_ = 0;
		other.file_path_.clear();
	}
	return *this;
}

SoundSample::~SoundSample()
{
	Release();
}

void SoundSample::Release()
{
	if (speechOwner == this) {
		speech.Stop();
		speechOwner = nullptr;
	}
	if (pcm_ != nullptr)
		amiga_snd_stop_data(reinterpret_cast<const signed char *>(pcm_.get()));
	pcm_ = nullptr;
	file_data_ = nullptr;
	file_data_size_ = 0;
	file_path_.clear();
	pcmLen_ = 0;
	voice_ = 0;
}

bool SoundSample::IsPlaying()
{
	if (IsStreaming())
		return speechOwner == this && speech.Playing();
	return voice_ != 0 && amiga_snd_playing(voice_) != 0;
}

int SoundSample::SetChunk(ArraySharedPtr<std::uint8_t> fileData, std::size_t dwBytes, bool isMp3)
{
	Release();
	if (isMp3)
		return -1;
	ArraySharedPtr<std::uint8_t> pcm;
	long len = 0;
	if (!ConvertWav(fileData.get(), dwBytes, pcm, len))
		return -1;
	pcm_ = pcm;
	pcmLen_ = len;
	// not "streaming" for IsStreaming(); the WAV itself is not kept
	file_data_ = pcm;
	file_data_size_ = static_cast<std::size_t>(len);
	isMp3_ = false;
	return 0;
}

int SoundSample::SetChunkStream(std::string filePath, bool isMp3, bool logErrors)
{
	// Streamed (speech): only the header is read now (the length); the file
	// is opened again and read in pieces when it plays.
	Release();
	if (isMp3)
		return -1;
	AmigaWavStream probe;
	uint32_t frames = 0;
	if (!probe.Open(filePath.c_str(), &frames)) {
		if (logErrors)
			LogError(LogCategory::Audio, "Audio file not found or not supported: {}", filePath);
		return -1;
	}
	file_path_ = std::move(filePath);
	pcmLen_ = static_cast<long>(frames);
	isMp3_ = false;
	return 0;
}

void SoundSample::VoiceVolumes(int &vl, int &vr) const
{
	const int v = muted_ ? 0 : volume_;
	vl = (v * gainL_) >> 8;
	vr = (v * gainR_) >> 8;
}

bool SoundSample::Play(int numIterations)
{
	int vl, vr;
	VoiceVolumes(vl, vr);
	if (IsStreaming()) {
		if (file_path_.empty())
			return false;
		if (speechOwner != nullptr)
			speech.Stop();
		speechOwner = nullptr;
		if (!speech.Open(file_path_.c_str()) || !speech.Start(1, numIterations == 0, vl, vr))
			return false;
		speechOwner = this;
		return true;
	}
	if (pcm_ == nullptr)
		return false;
	if (voice_ != 0)
		amiga_snd_stop(voice_);
	voice_ = amiga_snd_play(reinterpret_cast<const signed char *>(pcm_.get()), pcmLen_, vl, vr, numIterations == 0 ? 1 : 0);
	return voice_ != 0;
}

bool SoundSample::PlayNew(int logSoundVolume, int logUserVolume, int logPan)
{
	if (IsStreaming()) {
		SetVolume(logSoundVolume + logUserVolume * (ATTENUATION_MIN / VOLUME_MIN), ATTENUATION_MIN, 0);
		SetStereoPosition(logPan);
		return Play();
	}
	if (pcm_ == nullptr)
		return false;
	// the volume of a voice already playing stays as it was
	const int keep = voice_;
	voice_ = 0;
	SetVolume(logSoundVolume + logUserVolume * (ATTENUATION_MIN / VOLUME_MIN), ATTENUATION_MIN, 0);
	SetStereoPosition(logPan);
	int vl, vr;
	VoiceVolumes(vl, vr);
	voice_ = amiga_snd_play(reinterpret_cast<const signed char *>(pcm_.get()), pcmLen_, vl, vr, 0);
	if (voice_ == 0)
		voice_ = keep;
	return voice_ != 0;
}

void SoundSample::Stop()
{
	if (speechOwner == this) {
		speech.Stop();
		speechOwner = nullptr;
	}
	if (pcm_ != nullptr)
		amiga_snd_stop_data(reinterpret_cast<const signed char *>(pcm_.get()));
	voice_ = 0;
}

namespace {
void ApplyLive(const SoundSample *self, int voice, int vl, int vr)
{
	if (speechOwner == self)
		speech.SetVolume(vl, vr);
	else if (voice != 0)
		amiga_snd_volume(voice, vl, vr);
}
} // namespace

void SoundSample::SetVolume(int logVolume, int logMin, int logMax)
{
	volume_ = VolumeTo64(logVolume, logMin, logMax);
	int vl, vr;
	VoiceVolumes(vl, vr);
	ApplyLive(this, voice_, vl, vr);
}

void SoundSample::SetStereoPosition(int logPan)
{
	gainL_ = gainR_ = 256;
	if (logPan != 0) {
		const int far = static_cast<int>(std::pow(10.F, static_cast<float>(-std::abs(logPan)) / StereoSeparation) * 256.F + 0.5F);
		if (logPan < 0)
			gainR_ = far; // to the left: the right side quieter
		else
			gainL_ = far;
	}
	int vl, vr;
	VoiceVolumes(vl, vr);
	ApplyLive(this, voice_, vl, vr);
}

void SoundSample::Mute()
{
	muted_ = true;
	ApplyLive(this, voice_, 0, 0);
}

void SoundSample::Unmute()
{
	muted_ = false;
	int vl, vr;
	VoiceVolumes(vl, vr);
	ApplyLive(this, voice_, vl, vr);
}

int SoundSample::GetLength() const
{
	return static_cast<int>(pcmLen_ * 20 / 441); // * 1000 / 22050 without overflow
}

void AmigaSpeechPump()
{
	speech.Pump();
}

} // namespace devilution
