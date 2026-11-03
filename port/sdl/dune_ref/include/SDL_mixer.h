/* Mini SDL_mixer for Dune Legacy on the Amiga: sample channels and a music
   hook, mixed into the Amiga audio output (sdl_mixer.c). */
#ifndef MINI_SDL_MIXER_H
#define MINI_SDL_MIXER_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIX_MAX_VOLUME 128
#define MIX_CHANNELS   8
#define MIX_DEFAULT_FREQUENCY 22050

typedef enum {
    MIX_INIT_FLAC = 0x01, MIX_INIT_MOD = 0x02, MIX_INIT_MP3 = 0x08,
    MIX_INIT_OGG = 0x10, MIX_INIT_MID = 0x20, MIX_INIT_FLUIDSYNTH = MIX_INIT_MID
} MIX_InitFlags;

typedef struct Mix_Chunk {
    int allocated;
    Uint8 *abuf;
    Uint32 alen;
    Uint8 volume;
} Mix_Chunk;

typedef struct Mix_Music Mix_Music;

int Mix_Init(int flags);
void Mix_Quit(void);
int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void Mix_CloseAudio(void);
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
int Mix_AllocateChannels(int numchans);
int Mix_ReserveChannels(int num);
int Mix_GroupChannels(int from, int to, int tag);
int Mix_GroupAvailable(int tag);
int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
#define Mix_PlayChannel(channel, chunk, loops) Mix_PlayChannelTimed(channel, chunk, loops, -1)
int Mix_HaltChannel(int channel);
int Mix_Playing(int channel);
int Mix_Volume(int channel, int volume);
void Mix_FreeChunk(Mix_Chunk *chunk);

void Mix_HookMusic(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg);
Mix_Music *Mix_LoadMUS(const char *file);
void Mix_FreeMusic(Mix_Music *music);
int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_HaltMusic(void);
int Mix_PlayingMusic(void);
int Mix_VolumeMusic(int volume);

#define Mix_GetError SDL_GetError
#define Mix_SetError SDL_SetError

#ifdef __cplusplus
}
#endif

#endif
