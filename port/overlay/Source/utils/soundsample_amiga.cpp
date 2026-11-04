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
#include "utils/log.hpp"

extern "C" {
int amiga_snd_play(const signed char *data, long len, int vl, int vr, int loop);
int amiga_snd_playing(int handle);
void amiga_snd_stop(int handle);
void amiga_snd_stop_data(const signed char *data);
void amiga_snd_volume(int handle, int vl, int vr);
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

} // namespace

SoundSample::SoundSample(SoundSample &&other) noexcept
{
	*this = std::move(other);
}

SoundSample &SoundSample::operator=(SoundSample &&other) noexcept
{
	if (this != &other) {
		Release();
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
	}
	return *this;
}

SoundSample::~SoundSample()
{
	Release();
}

void SoundSample::Release()
{
	if (pcm_ != nullptr)
		amiga_snd_stop_data(reinterpret_cast<const signed char *>(pcm_.get()));
	pcm_ = nullptr;
	file_data_ = nullptr;
	file_data_size_ = 0;
	pcmLen_ = 0;
	voice_ = 0;
}

bool SoundSample::IsPlaying()
{
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
	// Loaded whole for now (speech: 100-400 KB); streaming comes with the music.
	AssetRef ref = FindAsset(filePath);
	if (!ref.ok()) {
		if (logErrors)
			LogError(LogCategory::Audio, "Audio file not found: {}", filePath);
		return -1;
	}
	const size_t size = ref.size();
	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok())
		return -1;
	auto wav = MakeArraySharedPtr<std::uint8_t>(size);
	if (!handle.read(wav.get(), size))
		return -1;
	const int result = SetChunk(wav, size, isMp3);
	file_path_ = std::move(filePath);
	return result;
}

void SoundSample::VoiceVolumes(int &vl, int &vr) const
{
	const int v = muted_ ? 0 : volume_;
	vl = (v * gainL_) >> 8;
	vr = (v * gainR_) >> 8;
}

bool SoundSample::Play(int numIterations)
{
	if (pcm_ == nullptr)
		return false;
	if (voice_ != 0)
		amiga_snd_stop(voice_);
	int vl, vr;
	VoiceVolumes(vl, vr);
	voice_ = amiga_snd_play(reinterpret_cast<const signed char *>(pcm_.get()), pcmLen_, vl, vr, numIterations == 0 ? 1 : 0);
	return voice_ != 0;
}

bool SoundSample::PlayNew(int logSoundVolume, int logUserVolume, int logPan)
{
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
	if (pcm_ != nullptr)
		amiga_snd_stop_data(reinterpret_cast<const signed char *>(pcm_.get()));
	voice_ = 0;
}

void SoundSample::SetVolume(int logVolume, int logMin, int logMax)
{
	volume_ = VolumeTo64(logVolume, logMin, logMax);
	if (voice_ != 0) {
		int vl, vr;
		VoiceVolumes(vl, vr);
		amiga_snd_volume(voice_, vl, vr);
	}
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
	if (voice_ != 0) {
		int vl, vr;
		VoiceVolumes(vl, vr);
		amiga_snd_volume(voice_, vl, vr);
	}
}

void SoundSample::Mute()
{
	muted_ = true;
	if (voice_ != 0)
		amiga_snd_volume(voice_, 0, 0);
}

void SoundSample::Unmute()
{
	muted_ = false;
	if (voice_ != 0) {
		int vl, vr;
		VoiceVolumes(vl, vr);
		amiga_snd_volume(voice_, vl, vr);
	}
}

int SoundSample::GetLength() const
{
	return static_cast<int>(pcmLen_ * 20 / 441); // * 1000 / 22050 without overflow
}

} // namespace devilution
