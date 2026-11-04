/* Cost of the planned sound mixer for the Diablo port, without the game.
   Paula: one left and one right channel, 8-bit, 22030 Hz (period 161 PAL).
   Mixer: voices of signed 8-bit samples (Diablo's effects are 8-bit mono
   22050 Hz), each with a left and a right volume (pan) taken from tables
   int16 vol[65][256]; music 16-bit mono with its volume; sums in 16 bit,
   then to 8 bit through a clipping table into the chip RAM buffers.
   Voice after voice over the whole buffer (small working set).
   Prints the time per second of sound for 0..16 voices and the share of
   the CPU. Usage: mixbench [seconds] */
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 22030
#define FRAMES 512            /* per buffer and channel, as Settlers */
#define SAMPLE_LEN 30000      /* bytes per test sample (~1.4 s) */

struct Device *TimerBase;
static struct timerequest tr;

static short vol[65][256];    /* vol[v][s + 128] = s * v */
static signed char clip[4096];/* (sum >> 2) + 2048 -> 8 bit */

struct Voice {
    const signed char *data;
    long len, pos;
    int vl, vr;
    long tab[256];            /* v2: (left << 16) + right for every sample value, built when the volume changes */
};

static ULONG now(ULONG *per_s)
{
    struct EClockVal ev;
    *per_s = ReadEClock(&ev);
    return ev.ev_lo;
}

static void mix(struct Voice *v, int nv, const short *music, long *mpos, long mlen, int mvol, short *sl, short *sr,
                BYTE *cl, BYTE *cr)
{
    int i, k;
    /* music first (sets the sums) */
    if (music != NULL) {
        long p = *mpos;
        for (i = 0; i < FRAMES; i++) {
            short m = (short)((music[p] * mvol) >> 8);
            sl[i] = m >> 6;
            sr[i] = m >> 6;
            if (++p >= mlen) p = 0;
        }
        *mpos = p;
    } else {
        memset(sl, 0, FRAMES * 2);
        memset(sr, 0, FRAMES * 2);
    }
    for (k = 0; k < nv; k++) {
        const short *tl = vol[v[k].vl] + 128, *tr2 = vol[v[k].vr] + 128;
        const signed char *d = v[k].data;
        long p = v[k].pos, len = v[k].len;
        for (i = 0; i < FRAMES; i++) {
            int s = d[p];
            sl[i] += tl[s];
            sr[i] += tr2[s];
            if (++p >= len) p = 0;
        }
        v[k].pos = p;
    }
    for (i = 0; i < FRAMES; i++) {
        cl[i] = clip[((sl[i] >> 2) + 2048) & 4095];
        cr[i] = clip[((sr[i] >> 2) + 2048) & 4095];
    }
}

/* v2: left and right summed in one 32-bit add: acc = sumL * 65536 + sumR
   exactly (as long as each sum fits in 16 bits); right = low half sign
   extended, left = (acc - right) >> 16. Sample end checked per run, not
   per sample. Output four samples per long write to chip RAM. */
static void build_tab(struct Voice *v)
{
    int s;
    for (s = -128; s < 128; s++) v->tab[s & 255] = (long)(s * v->vl) * 65536L + s * v->vr;
}

static void mix2(struct Voice *v, int nv, const short *music, long *mpos, long mlen, int mvol, long *acc, ULONG *cl,
                 ULONG *cr)
{
    int i, k;
    if (music != NULL) {
        long p = *mpos;
        for (i = 0; i < FRAMES; i++) {
            long m = ((long)music[p] * mvol) >> 14;     /* about the range of 4 voices */
            acc[i] = m * 65537L;
            if (++p >= mlen) p = 0;
        }
        *mpos = p;
    } else {
        memset(acc, 0, FRAMES * 4);
    }
    for (k = 0; k < nv; k++) {
        const long *tab = v[k].tab;
        const unsigned char *d = (const unsigned char *)v[k].data;
        long p = v[k].pos, len = v[k].len;
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
            if (p >= len) p = 0;
        }
        v[k].pos = p;
    }
    for (i = 0; i < FRAMES; i += 4) {
        ULONG ol = 0, or = 0;
        int j;
        for (j = 0; j < 4; j++) {
            long x = acc[i + j];
            short r = (short)x;
            long l = (x - r) >> 16;
            ol = (ol << 8) | (UBYTE)clip[((l >> 2) + 2048) & 4095];
            or = (or << 8) | (UBYTE)clip[((r >> 2) + 2048) & 4095];
        }
        cl[i >> 2] = ol;
        cr[i >> 2] = or;
    }
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5, nvs[] = { 0, 2, 4, 8, 12, 16 }, t, i, k;
    long bufs = (long)secs * RATE / FRAMES;
    ULONG per_s;
    signed char *samples;
    short *music, *sl, *sr;
    BYTE *cl, *cr, *fl, *fr;
    struct Voice v[16];
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&tr, 0) != 0) return 20;
    TimerBase = tr.tr_node.io_Device;
    samples = (signed char *)AllocVec(16 * SAMPLE_LEN, MEMF_FAST);
    music = (short *)AllocVec(RATE * 2 * 2, MEMF_FAST);
    sl = (short *)AllocVec(FRAMES * 2, MEMF_FAST);
    sr = (short *)AllocVec(FRAMES * 2, MEMF_FAST);
    cl = (BYTE *)AllocVec(FRAMES, MEMF_CHIP);
    cr = (BYTE *)AllocVec(FRAMES, MEMF_CHIP);
    fl = (BYTE *)AllocVec(FRAMES, MEMF_FAST);
    fr = (BYTE *)AllocVec(FRAMES, MEMF_FAST);
    if (!samples || !music || !sl || !sr || !cl || !cr || !fl || !fr) { printf("no memory\n"); return 20; }
    for (k = 0; k <= 64; k++)
        for (i = 0; i < 256; i++) vol[k][i] = (short)((i - 128) * k);
    for (i = 0; i < 4096; i++) {
        int s = i - 2048;          /* sum / 4: 16 voices * 127 * 64 / 4 = 32512 max */
        s >>= 2;
        clip[i] = (signed char)(s > 127 ? 127 : s < -128 ? -128 : s);
    }
    for (i = 0; i < 16 * SAMPLE_LEN; i++) samples[i] = (signed char)((i * 37 + (i >> 5)) & 255);
    for (i = 0; i < RATE * 2; i++) music[i] = (short)(i * 911);
    for (k = 0; k < 16; k++) {
        v[k].data = samples + k * SAMPLE_LEN;
        v[k].len = SAMPLE_LEN - k * 1000;
        v[k].pos = 0;
        v[k].vl = 20 + k;
        v[k].vr = 50 - k;
    }
    printf("mixer: %d Hz, buffers of %d frames, %d s of sound per test\n", RATE, FRAMES, secs);
    for (t = 0; t < 2; t++) {
        BYTE *ol = t == 0 ? cl : fl, *or = t == 0 ? cr : fr;
        for (k = 0; k < (int)(sizeof(nvs) / sizeof(nvs[0])); k++) {
            long mpos = 0, b;
            ULONG t0, t1, us;
            t0 = now(&per_s);
            for (b = 0; b < bufs; b++) mix(v, nvs[k], music, &mpos, RATE * 2, 200, sl, sr, ol, or);
            t1 = now(&per_s);
            us = (t1 - t0) / (per_s / 100000) * 10 / (ULONG)secs;   /* us per second of sound */
            printf("%s, music + %2d voices: %5lu us per second of sound = %lu.%02lu%% CPU\n",
                   t == 0 ? "to chip RAM" : "to fast RAM", nvs[k], (unsigned long)us, (unsigned long)(us / 10000),
                   (unsigned long)(us / 100 % 100));
        }
    }
    {
        long *acc = (long *)AllocVec(FRAMES * 4, MEMF_FAST);
        if (acc == NULL) return 20;
        for (k = 0; k < 16; k++) build_tab(&v[k]);
        for (k = 0; k < (int)(sizeof(nvs) / sizeof(nvs[0])); k++) {
            long mpos = 0, b;
            ULONG t0, t1, us;
            t0 = now(&per_s);
            for (b = 0; b < bufs; b++) mix2(v, nvs[k], music, &mpos, RATE * 2, 200, acc, (ULONG *)cl, (ULONG *)cr);
            t1 = now(&per_s);
            us = (t1 - t0) / (per_s / 100000) * 10 / (ULONG)secs;
            printf("v2 to chip RAM, music + %2d voices: %5lu us per second of sound = %lu.%02lu%% CPU\n", nvs[k],
                   (unsigned long)us, (unsigned long)(us / 10000), (unsigned long)(us / 100 % 100));
        }
    }
    CloseDevice((struct IORequest *)&tr);
    return 0;
}
