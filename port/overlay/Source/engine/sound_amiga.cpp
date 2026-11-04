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
#include "utils/amiga_wavstream.hpp"
#include "utils/str_cat.hpp"

extern "C" {
int amiga_snd_init(void);
void amiga_snd_quit(void);
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

AmigaWavStream musicStream;
bool musicOn = false;
bool musicMuted = false;

/** Event pump: music and speech rings topped up. */
void PumpStreams()
{
	if (musicOn)
		musicStream.Pump();
	AmigaSpeechPump();
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
	amiga_snd_pump_fn = PumpStreams;
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
	musicStream.Stop();
	musicOn = false;
	sgnMusicTrack = NUM_MUSIC;
}

void music_start(_music_id nTrack)
{
	assert(nTrack < NUM_MUSIC);
	music_stop();
	if (!gbMusicOn || !gbSndInited)
		return;
	const char *trackPath = HaveFullMusic() ? MusicTracks[nTrack] : SpawnMusicTracks[nTrack];
	const int v = musicMuted ? 0 : MusicVolume64();
	if (!musicStream.Open(trackPath) || !musicStream.Start(0, /*loop=*/true, v, v)) {
		LogError(LogCategory::Audio, "music: cannot play {}", trackPath);
		musicStream.Stop();
		return;
	}
	musicOn = true;
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
	if (musicOn) {
		const int v = musicMuted ? 0 : MusicVolume64();
		musicStream.SetVolume(v, v);
	}
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
	musicMuted = true;
	if (musicOn)
		musicStream.SetVolume(0, 0);
}

void music_unmute()
{
	musicMuted = false;
	if (musicOn)
		musicStream.SetVolume(MusicVolume64(), MusicVolume64());
}

} // namespace devilution
