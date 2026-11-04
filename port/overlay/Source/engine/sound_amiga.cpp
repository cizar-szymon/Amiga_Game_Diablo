/**
 * @file sound_amiga.cpp
 *
 * Warp port: engine/sound.cpp without SDL_audiolib. Effects play on the
 * Paula mixer (port/sdl/src/amiga_sound.c) through SoundSample
 * (utils/soundsample_amiga.cpp). A sound that is still playing is started
 * again on a voice of its own (the original makes a duplicate stream).
 * Music: streamed. The event pump (amiga_snd_pump_fn, from SDL_PumpEvents)
 * reads the WAV from the MPQ in pieces into the mixer's ring (~3 s), as
 * signed 8 bit (16-bit samples keep the high byte); the track loops.
 */
#include "engine/sound.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>

#include <SDL.h>

#include "appfat.h"
#include "engine/assets.hpp"
#include "monster.h"
#include "options.h"
#include "utils/log.hpp"
#include "game_mode.hpp"
#include "utils/str_cat.hpp"

extern "C" {
int amiga_snd_init(void);
void amiga_snd_quit(void);
int amiga_snd_stream_start(int stereo, int vol);
void amiga_snd_stream_stop(void);
void amiga_snd_stream_volume(int vol);
long amiga_snd_stream_space(void);
void amiga_snd_stream_write(const signed char *frames, long n);
extern void (*amiga_snd_pump_fn)(void);
}

namespace devilution {

bool gbSndInited;
/** The active background music track id. */
_music_id sgnMusicTrack = NUM_MUSIC;
bool gbMusicOn = true;
/** Specifies whether sound effects are enabled. */
bool gbSoundOn = true;

namespace {

int CapVolume(int volume)
{
	return std::clamp(volume, VOLUME_MIN, VOLUME_MAX);
}

std::expected<void, std::string> LoadAudioFile(const char *path, bool stream, SoundSample &result)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok())
		return std::unexpected(StrCat("Audio file not found\n", path));
	if (stream) {
		if (result.SetChunkStream(path, /*isMp3=*/false, /*logErrors=*/true) != 0)
			return std::unexpected(StrCat("Failed to load audio file\n", path));
		return {};
	}
	const size_t size = ref.size();
	AssetHandle handle = OpenAsset(std::move(ref));
	if (!handle.ok())
		return std::unexpected(StrCat("Failed to load audio file\n", path));
	auto waveFile = MakeArraySharedPtr<std::uint8_t>(size);
	if (!handle.read(waveFile.get(), size))
		return std::unexpected(StrCat("Failed to read file\n", path));
	if (result.SetChunk(waveFile, size, /*isMp3=*/false) != 0)
		return std::unexpected(StrCat("Unsupported audio file\n", path));
	return {};
}


int MusicVolume64()
{
	const int v = *GetOptions().Audio.musicVolume;
	if (v <= VOLUME_MIN)
		return 0;
	// soundsample.cpp: millibel range to linear (-10 dB = half the loudness)
	const float scaled = -10000.F * static_cast<float>(v - VOLUME_MAX) / static_cast<float>(VOLUME_MIN - VOLUME_MAX);
	return std::clamp(static_cast<int>(std::pow(10.F, scaled / 3321.9281F) * 64.F + 0.5F), 0, 64);
}

/** Maps from track ID to track name in spawn. */
const char *const SpawnMusicTracks[NUM_MUSIC] = {
	"music\\stowne.wav",
	"music\\slvla.wav",
	"music\\slvla.wav",
	"music\\slvla.wav",
	"music\\slvla.wav",
	"music\\dlvlf.wav",
	"music\\dlvle.wav",
	"music\\sintro.wav",
};
/** Maps from track ID to track name. */
const char *const MusicTracks[NUM_MUSIC] = {
	"music\\dtowne.wav",
	"music\\dlvla.wav",
	"music\\dlvlb.wav",
	"music\\dlvlc.wav",
	"music\\dlvld.wav",
	"music\\dlvlf.wav",
	"music\\dlvle.wav",
	"music\\dintro.wav",
};

struct MusicStream {
	std::unique_ptr<AssetHandle> handle;
	long dataStart = 0;
	uint32_t dataLen = 0;
	uint32_t pos = 0; // bytes of the data chunk read
	unsigned channels = 1, bits = 16;
	bool on = false;
	bool muted = false;
};
MusicStream music;
uint8_t musicIn[8192];
signed char musicOut[8192];

uint32_t Le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t Le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

/** Tops the ring up (called from the event pump). */
void MusicPump()
{
	if (!music.on || music.handle == nullptr)
		return;
	const unsigned frameBytes = music.channels * (music.bits / 8);
	bool ok = true;
	for (int round = 0; round < 8; round++) {
		long space = amiga_snd_stream_space();
		if (space < 2048)
			return;
		uint32_t frames = std::min<uint32_t>(static_cast<uint32_t>(space), sizeof(musicIn) / frameBytes);
		if (music.pos >= music.dataLen) {
			// loop: back to the start of the samples
			if (!music.handle->seek(music.dataStart)) {
				ok = false;
				break;
			}
			music.pos = 0;
		}
		frames = std::min<uint32_t>(frames, (music.dataLen - music.pos) / frameBytes);
		if (frames == 0) {
			music.pos = music.dataLen;
			continue;
		}
		if (!music.handle->read(musicIn, frames * frameBytes)) {
			ok = false;
			break;
		}
		music.pos += frames * frameBytes;
		const uint8_t *in = musicIn;
		signed char *out = musicOut;
		const unsigned samples = frames * music.channels;
		if (music.bits == 16) {
			for (unsigned i = 0; i < samples; i++, in += 2)
				*out++ = static_cast<signed char>(in[1]);
		} else {
			for (unsigned i = 0; i < samples; i++)
				*out++ = static_cast<signed char>(static_cast<int>(*in++) - 128);
		}
		amiga_snd_stream_write(musicOut, static_cast<long>(frames));
	}
	if (ok)
		return;
	// a read failed: no more music
	LogError(LogCategory::Audio, "music: read failed");
	music.on = false;
	amiga_snd_stream_stop();
}

bool MusicOpen(const char *path)
{
	AssetRef ref = FindAsset(path);
	if (!ref.ok())
		{ LogError(LogCategory::Audio, "music: open step 1 failed"); return false; }
	auto handle = std::make_unique<AssetHandle>(OpenAsset(std::move(ref)));
	if (!handle->ok())
		{ LogError(LogCategory::Audio, "music: open step 2 failed"); return false; }
	// header: RIFF, then chunks up to "data"
	uint8_t head[12];
	if (!handle->read(head, 12) || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
		{ LogError(LogCategory::Audio, "music: open step 3 failed"); return false; }
	long at = 12;
	unsigned format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	for (int i = 0; i < 16; i++) {
		uint8_t ch[8];
		if (!handle->read(ch, 8))
			{ LogError(LogCategory::Audio, "music: open step 4 failed"); return false; }
		const uint32_t n = Le32(ch + 4);
		at += 8;
		if (std::memcmp(ch, "data", 4) == 0) {
			if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate != 22050) {
				LogError(LogCategory::Audio, "music: unsupported format {} ({} ch, {} bit, {} Hz)", path, channels, bits, rate);
				return false;
			}
			music.handle = std::move(handle);
			music.dataStart = at;
			music.dataLen = n;
			music.pos = 0;
			music.channels = channels;
			music.bits = bits;
			return true;
		}
		if (std::memcmp(ch, "fmt ", 4) == 0 && n >= 16) {
			uint8_t f[16];
			if (!handle->read(f, 16))
				{ LogError(LogCategory::Audio, "music: open step 5 failed"); return false; }
			format = Le16(f);
			channels = Le16(f + 2);
			rate = Le32(f + 4);
			bits = Le16(f + 14);
			if (!handle->seek(at + n + (n & 1)))
				{ LogError(LogCategory::Audio, "music: open step 6 failed"); return false; }
		} else if (!handle->seek(at + n + (n & 1))) {
			{ LogError(LogCategory::Audio, "music: open step 7 failed"); return false; }
		}
		at += n + (n & 1);
	}
	return false;
}

} // namespace

void ClearDuplicateSounds()
{
	// duplicates are extra voices of the same samples: nothing to free
}

void snd_play_snd(TSnd *pSnd, int lVolume, int lPan, int userVolume)
{
	if (pSnd == nullptr || !gbSoundOn)
		return;

	const uint32_t tc = SDL_GetTicks();
	if (tc - pSnd->start_tc < 80)
		return;

	pSnd->DSB.PlayNew(lVolume, userVolume, lPan);
	pSnd->start_tc = tc;
}

std::expected<std::unique_ptr<TSnd>, std::string> SoundFileLoadWithStatus(const char *path, bool stream)
{
	auto snd = std::make_unique<TSnd>();
	snd->start_tc = SDL_GetTicks() - 80 - 1;
	auto loaded = LoadAudioFile(path, stream, snd->DSB);
	if (!loaded.has_value())
		return std::unexpected(loaded.error());
	return snd;
}

std::unique_ptr<TSnd> sound_file_load(const char *path, bool stream)
{
	std::expected<std::unique_ptr<TSnd>, std::string> result = SoundFileLoadWithStatus(path, stream);
	if (!result.has_value()) {
		// a missing or odd file costs the sound, not the game
		LogError(LogCategory::Audio, "{}", result.error());
		auto snd = std::make_unique<TSnd>();
		snd->start_tc = SDL_GetTicks() - 80 - 1;
		return snd;
	}
	return std::move(result).value();
}

TSnd::~TSnd()
{
	if (DSB.IsLoaded())
		DSB.Stop();
	DSB.Release();
}

void snd_init()
{
	GetOptions().Audio.soundVolume.SetValue(CapVolume(*GetOptions().Audio.soundVolume));
	gbSoundOn = *GetOptions().Audio.soundVolume > VOLUME_MIN;
	sgbSaveSoundOn = gbSoundOn;

	GetOptions().Audio.musicVolume.SetValue(CapVolume(*GetOptions().Audio.musicVolume));
	gbMusicOn = *GetOptions().Audio.musicVolume > VOLUME_MIN;

	if (amiga_snd_init() != 0) {
		LogError(LogCategory::Audio, "Paula sound not available");
		return;
	}
	gbSndInited = true;
}

void snd_deinit()
{
	music_stop();
	amiga_snd_pump_fn = nullptr;
	if (gbSndInited)
		amiga_snd_quit();
	gbSndInited = false;
}

_music_id GetLevelMusic(dungeon_type dungeonType)
{
	switch (dungeonType) {
	case DTYPE_TOWN:
		return TMUSIC_TOWN;
	case DTYPE_CATHEDRAL:
		return TMUSIC_CATHEDRAL;
	case DTYPE_CATACOMBS:
		return TMUSIC_CATACOMBS;
	case DTYPE_CAVES:
		return TMUSIC_CAVES;
	case DTYPE_HELL:
		return TMUSIC_HELL;
	case DTYPE_NEST:
		return TMUSIC_NEST;
	case DTYPE_CRYPT:
		return TMUSIC_CRYPT;
	default:
		return TMUSIC_INTRO;
	}
}

void music_stop()
{
	if (music.on)
		amiga_snd_stream_stop();
	music.on = false;
	music.handle = nullptr;
	sgnMusicTrack = NUM_MUSIC;
}

void music_start(_music_id nTrack)
{
	assert(nTrack < NUM_MUSIC);
	music_stop();
	if (!gbMusicOn || !gbSndInited)
		return;
	const char *trackPath = HaveFullMusic() ? MusicTracks[nTrack] : SpawnMusicTracks[nTrack];
	if (!MusicOpen(trackPath)) {
		LogError(LogCategory::Audio, "music: cannot play {}", trackPath);
		music.handle = nullptr;
		return;
	}
	if (amiga_snd_stream_start(music.channels == 2 ? 1 : 0, music.muted ? 0 : MusicVolume64()) != 0) {
		music.handle = nullptr;
		return;
	}
	music.on = true;
	amiga_snd_pump_fn = MusicPump;
	MusicPump(); // fill the ring before it plays out
	sgnMusicTrack = nTrack;
}

void sound_disable_music(bool disable)
{
	if (disable) {
		music_stop();
	} else if (sgnMusicTrack != NUM_MUSIC) {
		music_start(sgnMusicTrack);
	}
}

int sound_get_or_set_music_volume(int volume)
{
	if (volume == 1)
		return *GetOptions().Audio.musicVolume;
	GetOptions().Audio.musicVolume.SetValue(volume);
	if (music.on)
		amiga_snd_stream_volume(music.muted ? 0 : MusicVolume64());
	return *GetOptions().Audio.musicVolume;
}

int sound_get_or_set_sound_volume(int volume)
{
	if (volume == 1)
		return *GetOptions().Audio.soundVolume;
	GetOptions().Audio.soundVolume.SetValue(volume);
	return *GetOptions().Audio.soundVolume;
}

int SoundGetOrSetAudioCuesVolume(int volume)
{
	if (volume == 1)
		return *GetOptions().Audio.audioCuesVolume;
	GetOptions().Audio.audioCuesVolume.SetValue(volume);
	return *GetOptions().Audio.audioCuesVolume;
}

void music_mute()
{
	music.muted = true;
	if (music.on)
		amiga_snd_stream_volume(0);
}

void music_unmute()
{
	music.muted = false;
	if (music.on)
		amiga_snd_stream_volume(MusicVolume64());
}

} // namespace devilution
