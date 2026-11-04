/* Sound for the DevilutionX Warp port: Paula, one left and one right
   channel of audio.device, 8 bit, 22030 Hz (PAL period 161). A process of
   its own ("DevilutionX audio", priority 10) keeps a ring of 4 buffers of
   512 frames in chip RAM filled (~93 ms ahead); the game only starts,
   stops and changes voices (engine/sound_amiga.cpp, utils/soundsample_amiga.cpp).

   Mixer (measured with tools/amiga/mixbench.c on Amiga 1: 2-3% of the
   68060/100 for 4-8 voices): voices of signed 8-bit mono 22050 Hz samples
   (Diablo's effects are 8-bit mono 22050 Hz; other formats are converted
   when loaded). Each voice has a table tab[byte] = (s*vl/8) * 65536 +
   s*vr/8, so left and right are summed in one 32-bit add (exact while
   each sum fits in 16 bits: right = low half sign extended, left =
   (acc - right) >> 16). The sample end is checked once per run. Output
   through a clipping table, four samples per long write to chip RAM.

   Process pattern and channel allocation: Settlers port/amiga_audio.c
   (from Vastar). */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <devices/audio.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <graphics/gfxbase.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <clib/alib_protos.h>

#include <string.h>

#include <stdlib.h>

#include "msdl_internal.h"

int amiga_snd_init(void);
void amiga_snd_quit(void);

#define BUFS 4
#define FRAMES 512
#define VOICES 16

struct Voice {
    const unsigned char *data;  /* signed 8-bit samples (read as bytes) */
    long len, pos;
    int active, loop;
    unsigned gen;
    long tab[256];
};

static struct Voice voices[VOICES];
static unsigned next_gen = 1;
static long acc[FRAMES];
static signed char clip8[8192];   /* (sum >> 3) + 4096 -> 8 bit */
static ULONG *chip[BUFS][2];      /* left, right; FRAMES bytes each */

static int period, rate, pal_flag;
static struct SignalSemaphore lock;
static struct Process *proc;
static struct Task *parent;
static BYTE parent_sig = -1;
static volatile int proc_state;   /* 0 starting, 1 running, -1 failed, 2 done */
static volatile unsigned long underruns, rendered, render_ticks;

/* ---- mixer ---- */

static void build_tab(struct Voice *v, int vl, int vr)
{
    int s;
    for (s = -128; s < 128; s++)
        v->tab[(unsigned char)s] = (long)((s * vl) >> 3) * 65536L + ((s * vr) >> 3);
}

static void mix_voice(struct Voice *v)
{
    const long *tab = v->tab;
    const unsigned char *d = v->data;
    long p = v->pos, len = v->len;
    long *a = acc;
    int left = FRAMES;
    while (left > 0) {
        int run = len - p < left ? (int)(len - p) : left;
        const unsigned char *dp = d + p;
        left -= run;
        p += run;
        while (run >= 4) {
            a[0] += tab[dp[0]];
            a[1] += tab[dp[1]];
            a[2] += tab[dp[2]];
            a[3] += tab[dp[3]];
            a += 4;
            dp += 4;
            run -= 4;
        }
        while (run-- > 0) *a++ += tab[*dp++];
        if (p >= len) {
            if (!v->loop) {
                v->active = 0;
                break;
            }
            p = 0;
        }
    }
    v->pos = p;
}


/* ---- streams (0 music, 1 speech): a ring of signed 8-bit frames (mono,
   or left/right pairs) in fast RAM each. The game's main loop writes
   (amiga_snd_stream_write, from its event pump), the audio process reads;
   one writer, one reader: wr only moves in the game, rd only in the mixer.
   A stream marked "end" stops by itself when its ring runs empty. ---- */
#define RING 65536UL   /* frames, ~3 s */
#define STREAMS 2

struct Stream {
    signed char *ring;
    volatile unsigned long wr, rd;
    volatile int on, stereo, end;
    long tab_l[256], tab_r[256];   /* mono: tab_l + tab_r; stereo: left from tab_l, right from tab_r */
    volatile unsigned long gaps;
};
static struct Stream streams[STREAMS];

static void build_stream_tab(struct Stream *st, int vl, int vr)
{
    int s;
    for (s = -128; s < 128; s++) {
        st->tab_l[(unsigned char)s] = (long)((s * vl) >> 3) * 65536L;
        st->tab_r[(unsigned char)s] = (s * vr) >> 3;
    }
}

static void mix_stream(struct Stream *st)
{
    unsigned long rd = st->rd, avail = st->wr - rd, n = avail < FRAMES ? avail : FRAMES, i;
    const unsigned char *r = (const unsigned char *)st->ring;
    long *a = acc;
    if (n < FRAMES) {
        if (st->end) {
            st->on = 0;    /* played out */
        } else {
            st->gaps++;
        }
    }
    if (st->stereo) {
        for (i = 0; i < n; i++, rd++) {
            unsigned long k = (rd & (RING - 1)) * 2;
            a[i] += st->tab_l[r[k]] + st->tab_r[r[k + 1]];
        }
    } else {
        for (i = 0; i < n; i++, rd++) {
            unsigned char x = r[rd & (RING - 1)];
            a[i] += st->tab_l[x] + st->tab_r[x];
        }
    }
    st->rd = rd;
}

static void render(int b)
{
    ULONG *cl = chip[b][0], *cr = chip[b][1];
    unsigned long t0 = amiga_eclock();
    int i, k;
    memset(acc, 0, sizeof(acc));
    ObtainSemaphore(&lock);
    for (k = 0; k < STREAMS; k++)
        if (streams[k].on) mix_stream(&streams[k]);
    for (k = 0; k < VOICES; k++)
        if (voices[k].active) mix_voice(&voices[k]);
    ReleaseSemaphore(&lock);
    for (i = 0; i < FRAMES; i += 4) {
        ULONG ol = 0, or = 0;
        int j;
        for (j = 0; j < 4; j++) {
            long x = acc[i + j];
            short r = (short)x;
            long l = (x - r) >> 16;
            ol = (ol << 8) | (UBYTE)clip8[(l >> 3) + 4096];
            or = (or << 8) | (UBYTE)clip8[(r >> 3) + 4096];
        }
        cl[i >> 2] = ol;
        cr[i >> 2] = or;
    }
    render_ticks += amiga_eclock() - t0;
    rendered++;
}

/* ---- the audio process ---- */

static void queue(struct IOAudio *io)
{
    io->ioa_Request.io_Command = CMD_WRITE;
    io->ioa_Request.io_Flags = ADIOF_PERVOL;
    io->ioa_Length = FRAMES;
    BeginIO((struct IORequest *)io);
}

static void audio_process(void)
{
    static UBYTE channels[] = { 3, 5, 10, 12 };
    struct MsgPort *port;
    struct IOAudio *alloc;
    struct IOAudio *io[BUFS][2];
    int busy[BUFS];
    int next = 0, b, c, ok = 0;
    ULONG units;

    memset(io, 0, sizeof(io));
    port = CreateMsgPort();
    alloc = port ? (struct IOAudio *)CreateIORequest(port, sizeof(*alloc)) : NULL;
    if (alloc != NULL) {
        alloc->ioa_Request.io_Message.mn_Node.ln_Pri = 1;
        alloc->ioa_Request.io_Flags = ADIOF_NOWAIT;
        alloc->ioa_Data = channels;
        alloc->ioa_Length = sizeof(channels);
        if (OpenDevice((CONST_STRPTR)AUDIONAME, 0, (struct IORequest *)alloc, 0) == 0) {
            ok = 1;
        } else {
            DeleteIORequest((struct IORequest *)alloc);
            alloc = NULL;
        }
    }
    if (ok) {
        units = (ULONG)alloc->ioa_Request.io_Unit;
        for (b = 0; b < BUFS && ok; b++) {
            for (c = 0; c < 2; c++) {
                io[b][c] = (struct IOAudio *)AllocMem(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR);
                if (io[b][c] == NULL) {
                    ok = 0;
                    break;
                }
                *io[b][c] = *alloc;
                /* left: channels 0 and 3, right: 1 and 2 */
                io[b][c]->ioa_Request.io_Unit = (struct Unit *)(units & (c ? 0x06 : 0x09));
                io[b][c]->ioa_Data = (UBYTE *)chip[b][c];
                io[b][c]->ioa_Period = (UWORD)period;
                io[b][c]->ioa_Volume = 64;
                io[b][c]->ioa_Cycles = 1;
            }
        }
    }

    proc_state = ok ? 1 : -1;
    Signal(parent, 1UL << parent_sig);

    if (ok) {
        for (b = 0; b < BUFS; b++) {
            render(b);
            for (c = 0; c < 2; c++) queue(io[b][c]);
            busy[b] = 2;
        }
        for (;;) {
            ULONG got = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);
            struct Message *m;
            int idle = 1;
            if (got & SIGBREAKF_CTRL_C) break;
            while ((m = GetMsg(port)) != NULL) {
                for (b = 0; b < BUFS; b++)
                    for (c = 0; c < 2; c++)
                        if (m == &io[b][c]->ioa_Request.io_Message) busy[b]--;
            }
            for (b = 0; b < BUFS; b++)
                if (busy[b] != 0) idle = 0;
            if (idle) underruns++;
            /* refill the buffers whose two halves both finished, in ring order */
            while (busy[next] == 0) {
                b = next;
                render(b);
                for (c = 0; c < 2; c++) queue(io[b][c]);
                busy[b] = 2;
                next = (next + 1) % BUFS;
            }
        }
        for (b = 0; b < BUFS; b++)
            for (c = 0; c < 2; c++)
                if (busy[b] > 0) {
                    AbortIO((struct IORequest *)io[b][c]);
                    WaitIO((struct IORequest *)io[b][c]);
                }
    }

    for (b = 0; b < BUFS; b++)
        for (c = 0; c < 2; c++)
            if (io[b][c] != NULL) FreeMem(io[b][c], sizeof(struct IOAudio));
    if (alloc != NULL) {
        CloseDevice((struct IORequest *)alloc);
        DeleteIORequest((struct IORequest *)alloc);
    }
    if (port != NULL) DeleteMsgPort(port);

    Forbid();  /* the parent may go on once signalled */
    proc_state = 2;
    Signal(parent, 1UL << parent_sig);
}

/* ---- the game's side ---- */

int amiga_snd_init(void)
{
    ULONG clock;
    int b, c, i;
    if (proc != NULL) return 0;
    {
        char buf[8];
        if (msdl_env("DIABLO_NOSOUND", buf, sizeof(buf)) >= 0) {
            msdl_logf(SDL_LOG_PRIORITY_INFO, "sound: off (DIABLO_NOSOUND)");
            return -1;
        }
    }
    pal_flag = (GfxBase->DisplayFlags & PAL) != 0;
    clock = pal_flag ? 3546895UL : 3579545UL;
    /* period rounded up: the rate stays at most 22050 (PAL 161 -> 22030 Hz) */
    period = (int)((clock + 22049UL) / 22050UL);
    rate = (int)(clock / period);
    for (i = 0; i < 8192; i++) {
        int s = i - 4096;
        clip8[i] = (signed char)(s > 127 ? 127 : s < -128 ? -128 : s);
    }
    memset(voices, 0, sizeof(voices));
    InitSemaphore(&lock);
    for (b = 0; b < BUFS; b++)
        for (c = 0; c < 2; c++) {
            chip[b][c] = (ULONG *)AllocMem(FRAMES, MEMF_CHIP | MEMF_CLEAR);
            if (chip[b][c] == NULL) {
                amiga_snd_quit();
                return -1;
            }
        }
    parent = FindTask(NULL);
    parent_sig = AllocSignal(-1);
    if (parent_sig < 0) {
        amiga_snd_quit();
        return -1;
    }
    proc_state = 0;
    proc = CreateNewProcTags(NP_Entry, (ULONG)audio_process, NP_Name, (ULONG) "DevilutionX audio", NP_Priority, 10,
                             NP_StackSize, 16384, TAG_DONE);
    if (proc == NULL) {
        amiga_snd_quit();
        return -1;
    }
    while (proc_state == 0) Wait(1UL << parent_sig);
    if (proc_state < 0) {
        while (proc_state != 2) Wait(1UL << parent_sig);
        proc = NULL;
        amiga_snd_quit();
        msdl_logf(SDL_LOG_PRIORITY_ERROR, "sound: audio.device channels not available");
        return -1;
    }
    {
        static int registered;
        if (!registered) {
            registered = 1;
            atexit(amiga_snd_quit);  /* the process must not outlive the program */
        }
    }
    msdl_logf(SDL_LOG_PRIORITY_INFO, "sound: Paula %d Hz (period %d, %s)", rate, period, pal_flag ? "PAL" : "NTSC");
    return 0;
}

void amiga_snd_quit(void)
{
    int b, c;
    streams[0].on = streams[1].on = 0;
    if (proc != NULL) {
        Signal(&proc->pr_Task, SIGBREAKF_CTRL_C);
        while (proc_state != 2) Wait(1UL << parent_sig);
        proc = NULL;
    }
    if (parent_sig >= 0) {
        FreeSignal(parent_sig);
        parent_sig = -1;
    }
    for (b = 0; b < BUFS; b++)
        for (c = 0; c < 2; c++)
            if (chip[b][c] != NULL) {
                FreeMem(chip[b][c], FRAMES);
                chip[b][c] = NULL;
            }
}

/* Starts a voice; returns a handle (> 0) or 0. vl, vr: 0..64. */
int amiga_snd_play(const signed char *data, long len, int vl, int vr, int loop)
{
    int k, best = -1;
    unsigned oldest = 0xFFFFFFFFU;
    if (proc == NULL || data == NULL || len <= 0) return 0;
    ObtainSemaphore(&lock);
    for (k = 0; k < VOICES; k++) {
        if (!voices[k].active) {
            best = k;
            break;
        }
        /* all busy: take the oldest voice that does not loop */
        if (!voices[k].loop && voices[k].gen < oldest) {
            oldest = voices[k].gen;
            best = k;
        }
    }
    if (best >= 0) {
        struct Voice *v = &voices[best];
        v->data = (const unsigned char *)data;
        v->len = len;
        v->pos = 0;
        v->loop = loop;
        v->gen = next_gen++;
        if (next_gen > 0xFFFFFF) next_gen = 1;
        build_tab(v, vl, vr);
        v->active = 1;
    }
    ReleaseSemaphore(&lock);
    return best < 0 ? 0 : (int)((voices[best].gen << 5) | (unsigned)best);
}

static struct Voice *voice_of(int handle)
{
    struct Voice *v;
    if (handle <= 0) return NULL;
    v = &voices[handle & 31];
    return ((unsigned)handle >> 5) == v->gen ? v : NULL;
}

int amiga_snd_playing(int handle)
{
    struct Voice *v = voice_of(handle);
    return v != NULL && v->active;
}

void amiga_snd_stop(int handle)
{
    struct Voice *v;
    if (proc == NULL) return;
    ObtainSemaphore(&lock);
    v = voice_of(handle);
    if (v != NULL) v->active = 0;
    ReleaseSemaphore(&lock);
}

/* Stops every voice playing these samples (before they are freed). */
void amiga_snd_stop_data(const signed char *data)
{
    int k;
    if (proc == NULL) return;
    ObtainSemaphore(&lock);
    for (k = 0; k < VOICES; k++)
        if (voices[k].data == (const unsigned char *)data) voices[k].active = 0;
    ReleaseSemaphore(&lock);
}

void amiga_snd_volume(int handle, int vl, int vr)
{
    struct Voice *v;
    if (proc == NULL) return;
    ObtainSemaphore(&lock);
    v = voice_of(handle);
    if (v != NULL) build_tab(v, vl, vr);
    ReleaseSemaphore(&lock);
}


/* Streams (0 music, 1 speech). vl, vr 0..64. */
int amiga_snd_stream_start(int slot, int stereo, int vl, int vr)
{
    struct Stream *st = &streams[slot & 1];
    if (proc == NULL) return -1;
    if (st->ring == NULL) {
        st->ring = (signed char *)AllocVec(RING * 2, MEMF_ANY);
        if (st->ring == NULL) return -1;
    }
    ObtainSemaphore(&lock);
    st->on = 0;
    st->wr = st->rd = 0;
    st->stereo = stereo;
    st->end = 0;
    build_stream_tab(st, vl, vr);
    st->on = 1;
    ReleaseSemaphore(&lock);
    return 0;
}

void amiga_snd_stream_stop(int slot)
{
    if (proc == NULL) return;
    ObtainSemaphore(&lock);
    streams[slot & 1].on = 0;
    ReleaseSemaphore(&lock);
}

void amiga_snd_stream_volume(int slot, int vl, int vr)
{
    if (proc == NULL) return;
    ObtainSemaphore(&lock);
    build_stream_tab(&streams[slot & 1], vl, vr);
    ReleaseSemaphore(&lock);
}

/* No more data will come: the stream stops when its ring is empty. */
void amiga_snd_stream_end(int slot)
{
    streams[slot & 1].end = 1;
}

int amiga_snd_stream_playing(int slot)
{
    return streams[slot & 1].on;
}

/* Frames the ring can take now. */
long amiga_snd_stream_space(int slot)
{
    struct Stream *st = &streams[slot & 1];
    if (!st->on || st->end) return 0;
    return (long)(RING - (st->wr - st->rd));
}

/* n frames (mono bytes, or left/right byte pairs), at most the space. */
void amiga_snd_stream_write(int slot, const signed char *frames, long n)
{
    struct Stream *st = &streams[slot & 1];
    unsigned long wr = st->wr, at = wr & (RING - 1), first = RING - at;
    int fb = st->stereo ? 2 : 1;
    if (!st->on || n <= 0) return;
    if ((unsigned long)n < first) first = (unsigned long)n;
    memcpy(st->ring + at * fb, frames, first * fb);
    if ((unsigned long)n > first) memcpy(st->ring, frames + first * fb, ((unsigned long)n - first) * fb);
    st->wr = wr + (unsigned long)n;   /* after the data */
}

unsigned long amiga_snd_stream_gaps(void) { return streams[0].gaps + streams[1].gaps; }

/* The game's pump (engine/sound_amiga.cpp), called from the event loop. */
void (*amiga_snd_pump_fn)(void);

/* For the stats line: share of the time spent mixing (per mille), gaps. */
void amiga_snd_stats(unsigned long *ticks, unsigned long *buffers, unsigned long *gaps)
{
    *ticks = render_ticks;
    *buffers = rendered;
    *gaps = underruns;
}
