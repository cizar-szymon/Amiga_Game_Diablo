/* Fast RAM -> Fast RAM row copies as the cell renderer does them (scene
   canvas, pitch 1152, into the game buffer, pitch 640): 352 rows of 640
   bytes, source offset 0 / 2 / 5. CopyMem (libnix memcpy), long loop
   unrolled x4, MOVE16 (aligned only, after CacheClearE of the source).
   Usage: copybench [repeats] */
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Device *TimerBase;
static struct timerequest tr;

static ULONG now(ULONG *per_s)
{
    struct EClockVal ev;
    *per_s = ReadEClock(&ev);
    return ev.ev_lo;
}

static void copy_long(UBYTE *d, const UBYTE *s, ULONG n)
{
    ULONG *dl = (ULONG *)d;
    const ULONG *sl = (const ULONG *)s;
    ULONG k = n >> 4;
    while (k--) {
        dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3];
        dl += 4; sl += 4;
    }
    d = (UBYTE *)dl; s = (const UBYTE *)sl;
    n &= 15;
    while (n--) *d++ = *s++;
}

static void copy_move16(UBYTE *d, const UBYTE *s, ULONG n)
{
    register const UBYTE *a0 __asm("a0") = s;
    register UBYTE *a1 __asm("a1") = d;
    register ULONG d0 __asm("d0") = n >> 4;
    __asm__ volatile("1:\n\t.word 0xf620,0x9000\n\tsubq.l #1,%2\n\tbne.s 1b" : "+a"(a0), "+a"(a1), "+d"(d0) : : "cc", "memory");
}

int main(int argc, char **argv)
{
    int reps = argc > 1 ? atoi(argv[1]) : 20, i, m, y;
    static const int offs[3] = { 0, 2, 5 };
    ULONG per_s;
    UBYTE *a = (UBYTE *)AllocVec(1152 * 480 + 64, MEMF_FAST), *b = (UBYTE *)AllocVec(640 * 480 + 64, MEMF_FAST);
    UBYTE *A, *B;
    if (a == NULL || b == NULL) return 20;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&tr, 0) != 0) return 20;
    TimerBase = tr.tr_node.io_Device;
    A = (UBYTE *)(((ULONG)a + 15) & ~15UL);
    B = (UBYTE *)(((ULONG)b + 15) & ~15UL);
    for (i = 0; i < 1152 * 480; i++) A[i] = (UBYTE)i;
    for (m = 0; m < 4; m++) {
        int o;
        for (o = 0; o < 3; o++) {
            ULONG t0, t1;
            if (m == 3 && offs[o] != 0) continue;
            t0 = now(&per_s);
            for (i = 0; i < reps; i++) {
                if (m == 3) CacheClearE(A, 1152 * 352, CACRF_ClearD);
                for (y = 0; y < 352; y++) {
                    const UBYTE *s = A + 64 + offs[o] + y * 1152;
                    UBYTE *d = B + y * 640;
                    if (m == 0) CopyMem((APTR)s, d, 640);
                    else if (m == 1) memcpy(d, s, 640);
                    else if (m == 2) copy_long(d, s, 640);
                    else copy_move16(d, s, 640);
                }
            }
            t1 = now(&per_s);
            printf("%-8s src offset %d: %lu us per 640x352\n", m == 0 ? "CopyMem" : m == 1 ? "memcpy" : m == 2 ? "long x4" : "MOVE16",
                   offs[o], (unsigned long)((t1 - t0) / (per_s / 10000) * 100 / reps));
        }
    }
    CloseDevice((struct IORequest *)&tr);
    FreeVec(a);
    FreeVec(b);
    return 0;
}
