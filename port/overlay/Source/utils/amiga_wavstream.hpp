/**
 * @file amiga_wavstream.hpp
 *
 * Warp port: a WAV file read from the MPQ in pieces into one of the Paula
 * mixer's stream rings (port/sdl/src/amiga_sound.c; slot 0 music, 1
 * speech). Signed 8 bit; 16-bit samples keep the high byte; stereo stays
 * stereo; other rates than 22050 Hz are stepped (nearest sample).
 * Pump() is called from the event loop (SDL_PumpEvents).
 */
#pragma once

#include <cstdint>
#include <memory>

#include "engine/assets.hpp"

namespace devilution {

class AmigaWavStream {
public:
	/** Opens and reads the header; frames22k: length in output frames. */
	bool Open(const char *path, uint32_t *frames22k = nullptr);
	/** Starts playing the opened file in the slot (vl, vr: 0..64). */
	bool Start(int slot, bool loop, int vl, int vr);
	void Stop();
	void Pump();
	[[nodiscard]] bool Playing() const;
	void SetVolume(int vl, int vr);

private:
	std::unique_ptr<AssetHandle> handle_;
	long dataStart_ = 0;
	uint32_t dataLen_ = 0;
	uint32_t pos_ = 0;
	unsigned channels_ = 1;
	unsigned bits_ = 16;
	uint32_t rate_ = 22050;
	uint32_t frac_ = 0;
	uint32_t skip_ = 0;
	int slot_ = 0;
	bool loop_ = false;
	bool on_ = false;
	bool ended_ = false;
};

/** Speech: pumps the stream of the sound playing now (soundsample_amiga.cpp). */
void AmigaSpeechPump();

} // namespace devilution
