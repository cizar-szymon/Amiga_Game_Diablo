/* Mini SDL_mixer: up to 32 sample channels plus a music hook, mixed in
   16-bit stereo for the Paula output of amiga_audio.c (22030 Hz on PAL).
   The game converts its samples to Mix_QuerySpec()'s format itself
   (Vocfile.cpp), so a chunk is plain S16 big-endian stereo. */
#include <stdio.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include "amiga_audio.h"

#define MAX_CHANNELS 32

typedef struct {
    Mix_Chunk *chunk;
    Uint32 pos;         /* bytes */
    int loops;
    int volume;         /* 0..128 */
    int tag;
} Channel;

static Channel ch[MAX_CHANNELS];
static int num_channels = MIX_CHANNELS;
static int reserved;
static int opened;
static int out_rate;
static void (*music_hook)(void *, Uint8 *, int);
static void *music_arg;
static int music_volume = MIX_MAX_VOLUME;

static Sint32 *acc;
static int acc_frames;
static Sint16 *music_buf;

int Mix_Init(int flags) { (void)flags; return 0; }
void Mix_Quit(void) {}

/* Runs in the audio process with the lock held. */
void mix_fill(short *out, int frames, void *user)
{
    int i, c, n = frames * 2;
    (void)user;
    if (acc_frames < frames) return;
    memset(acc, 0, n * sizeof(Sint32));

    for (c = 0; c < num_channels; c++) {
        Channel *k = &ch[c];
        int done = 0;
        if (k->chunk == NULL) continue;
        while (done < n && k->chunk != NULL) {
            const Sint16 *s = (const Sint16 *)(k->chunk->abuf + k->pos);
            int left = (int)((k->chunk->alen - k->pos) / 2);
            int take = n - done;
            int vol = (k->volume * k->chunk->volume) >> 7;
            if (take > left) take = left;
            if (take <= 0) {        /* odd length: the last byte is no sample */
                k->chunk = NULL;
                break;
            }
            if (vol > 0) {
                Sint32 *a = acc + done;
                for (i = 0; i < take; i++) a[i] += (s[i] * vol) >> 7;
            }
            done += take;
            k->pos += take * 2;
            if (k->pos >= k->chunk->alen) {
                if (k->loops != 0) {
                    if (k->loops > 0) k->loops--;
                    k->pos = 0;
                } else {
                    k->chunk = NULL;
                }
            }
        }
    }

    if (music_hook != NULL && music_buf != NULL) {
        music_hook(music_arg, (Uint8 *)music_buf, n * 2);
        for (i = 0; i < n; i++) acc[i] += music_buf[i];
    }

    for (i = 0; i < n; i++) {
        Sint32 v = acc[i];
        out[i] = (short)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
}

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
    int frames;
    (void)frequency;
    (void)format;
    (void)channels;
    if (opened) return 0;
    {
        int i;
        memset(ch, 0, sizeof(ch));
        for (i = 0; i < MAX_CHANNELS; i++) {
            ch[i].volume = MIX_MAX_VOLUME;
            ch[i].tag = -1;
        }
    }
    /* buffers of about 1/25 s */
    frames = chunksize > 0 ? chunksize : 1024;
    if (frames > 1024) frames = 1024;
    if (frames < 512) frames = 512;
    acc = (Sint32 *)malloc(frames * 2 * sizeof(Sint32));
    music_buf = (Sint16 *)malloc(frames * 2 * sizeof(Sint16));
    if (acc == NULL || music_buf == NULL) {
        free(acc);
        free(music_buf);
        acc = NULL;
        music_buf = NULL;
        return SDL_SetError("out of memory");
    }
    acc_frames = frames;
    if (amiga_audio_open(frames, mix_fill, NULL) != 0) {
        free(acc);
        free(music_buf);
        acc = NULL;
        music_buf = NULL;
        return SDL_SetError("cannot open audio.device");
    }
    out_rate = amiga_audio_rate();
    opened = 1;
    /* every way out of the program (exit() from an exception, Ctrl-C) must
       stop the audio process, or it keeps running on freed code */
    {
        static int registered;
        if (!registered) {
            registered = 1;
            atexit(Mix_CloseAudio);
        }
    }
    return 0;
}

void Mix_CloseAudio(void)
{
    if (!opened) return;
    amiga_audio_close();
    opened = 0;
    free(acc);
    free(music_buf);
    acc = NULL;
    music_buf = NULL;
    memset(ch, 0, sizeof(ch));
}

int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
    if (!opened) return 0;
    if (frequency != NULL) *frequency = out_rate;
    if (format != NULL) *format = AUDIO_S16SYS;
    if (channels != NULL) *channels = 2;
    return 1;
}

int Mix_AllocateChannels(int numchans)
{
    if (numchans < 0) return num_channels;
    if (numchans > MAX_CHANNELS) numchans = MAX_CHANNELS;
    if (opened) amiga_audio_lock();
    {
        int i;
        for (i = numchans; i < num_channels; i++) ch[i].chunk = NULL;
        for (i = num_channels; i < numchans; i++) {
            memset(&ch[i], 0, sizeof(ch[i]));
            ch[i].volume = MIX_MAX_VOLUME;
            ch[i].tag = -1;
        }
    }
    num_channels = numchans;
    if (opened) amiga_audio_unlock();
    return num_channels;
}

int Mix_ReserveChannels(int num)
{
    if (num > num_channels) num = num_channels;
    reserved = num;
    return num;
}

int Mix_GroupChannels(int from, int to, int tag)
{
    int i, n = 0;
    for (i = from; i <= to && i < num_channels; i++, n++) ch[i].tag = tag;
    return n;
}

int Mix_GroupAvailable(int tag)
{
    int i;
    for (i = 0; i < num_channels; i++) {
        if ((tag == -1 || ch[i].tag == tag) && ch[i].chunk == NULL) return i;
    }
    return -1;
}

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
    (void)ticks;
    if (chunk == NULL) return SDL_SetError("no chunk");
    if (!opened) return SDL_SetError("audio not open");
    amiga_audio_lock();
    if (channel == -1) {
        int i;
        for (i = reserved; i < num_channels; i++) {
            if (ch[i].chunk == NULL) break;
        }
        channel = i < num_channels ? i : -1;
    }
    if (channel >= 0 && channel < num_channels) {
        ch[channel].chunk = chunk;
        ch[channel].pos = 0;
        ch[channel].loops = loops;
    } else {
        channel = -1;
        SDL_SetError("no free channels available");
    }
    amiga_audio_unlock();
    return channel;
}

int Mix_HaltChannel(int channel)
{
    int i;
    if (opened) amiga_audio_lock();
    for (i = 0; i < num_channels; i++) {
        if (channel == -1 || channel == i) ch[i].chunk = NULL;
    }
    if (opened) amiga_audio_unlock();
    return 0;
}

int Mix_Playing(int channel)
{
    int i, n = 0;
    if (channel >= 0) return channel < num_channels && ch[channel].chunk != NULL;
    for (i = 0; i < num_channels; i++) n += ch[i].chunk != NULL;
    return n;
}

int Mix_Volume(int channel, int volume)
{
    int i, old = channel >= 0 && channel < num_channels ? ch[channel].volume : MIX_MAX_VOLUME;
    if (volume < 0) return old;
    if (volume > MIX_MAX_VOLUME) volume = MIX_MAX_VOLUME;
    for (i = 0; i < num_channels; i++) {
        if (channel == -1 || channel == i) ch[i].volume = volume;
    }
    return old;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
    int i;
    if (chunk == NULL) return;
    if (opened) amiga_audio_lock();
    for (i = 0; i < MAX_CHANNELS; i++) {
        if (ch[i].chunk == chunk) ch[i].chunk = NULL;
    }
    if (opened) amiga_audio_unlock();
    if (chunk->allocated) free(chunk->abuf);
    free(chunk);
}

void Mix_HookMusic(void (*mix_func)(void *udata, Uint8 *stream, int len), void *arg)
{
    if (opened) amiga_audio_lock();
    music_hook = mix_func;
    music_arg = arg;
    if (opened) amiga_audio_unlock();
}

/* No compressed music (OGG/MP3/MID files of the "directory" player). */
Mix_Music *Mix_LoadMUS(const char *file)
{
    SDL_SetError("Mix_LoadMUS(%s): not supported", file);
    return NULL;
}
void Mix_FreeMusic(Mix_Music *music) { (void)music; }
int Mix_PlayMusic(Mix_Music *music, int loops) { (void)music; (void)loops; return -1; }
int Mix_HaltMusic(void) { return 0; }
int Mix_PlayingMusic(void) { return 0; }

int Mix_VolumeMusic(int volume)
{
    int old = music_volume;
    if (volume >= 0) music_volume = volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume;
    return old;
}
