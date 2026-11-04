/* 68060 loops for DevilutionX's two hottest pixel loops in the dungeon
   (engine/render/blit_impl.hpp, profile 3.11.2026: 39% + 22% of the time):

     amiga_blit_map:        dst[i] = map[src[i]]               (lit tiles)
     amiga_blit_blend_map:  dst[i] = pal[dst[i]][map[src[i]]]  (see-through walls)

   gcc's loops do one pixel per pass with byte loads and use the index
   register right after loading it (a stall on the 68060). Here: four pixels
   per pass, one long read of the source (and of the destination), the table
   look-ups interleaved, one long write. The 68020+ reads and writes longs at
   any address. Index registers are only ever written as bytes / words, so
   their upper bits stay zero.

   ENV:DIABLO_VERIFY_BLIT: every call is checked against the C formula;
   differences go to the log (slow, for testing). */
#include <string.h>

#include "msdl_internal.h"

void amiga_blit_map_asm(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map);
void amiga_blit_blend_map_asm(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map,
                              const unsigned char *pal);

__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _amiga_blit_map_asm\n"
    "_amiga_blit_map_asm:\n"
    "    movem.l d2-d5/a2,-(sp)\n"          /* 5 regs: args from 24(sp) */
    "    move.l  24(sp),a0\n"               /* dst */
    "    move.l  28(sp),a1\n"               /* src */
    "    move.l  32(sp),d4\n"               /* n */
    "    move.l  36(sp),a2\n"               /* map */
    "    moveq   #0,d2\n"
    "    moveq   #0,d3\n"
    "    move.l  d4,d5\n"
    "    lsr.l   #2,d5\n"
    "    beq.s   2f\n"
    "1:  move.l  (a1)+,d1\n"                /* s0 s1 s2 s3 */
    "    rol.l   #8,d1\n"
    "    move.b  d1,d2\n"                   /* s0 */
    "    rol.l   #8,d1\n"
    "    move.b  d1,d3\n"                   /* s1 */
    "    move.b  0(a2,d2.l),d0\n"
    "    rol.l   #8,d1\n"
    "    lsl.w   #8,d0\n"
    "    move.b  d1,d2\n"                   /* s2 */
    "    move.b  0(a2,d3.l),d0\n"           /* d0.w = m0 m1 */
    "    rol.l   #8,d1\n"
    "    swap    d0\n"
    "    move.b  d1,d3\n"                   /* s3 */
    "    move.b  0(a2,d2.l),d0\n"
    "    lsl.w   #8,d0\n"
    "    move.b  0(a2,d3.l),d0\n"           /* d0 = m0 m1 m2 m3 */
    "    move.l  d0,(a0)+\n"
    "    subq.l  #1,d5\n"
    "    bne.s   1b\n"
    "2:  and.l   #3,d4\n"
    "    beq.s   4f\n"
    "    subq.w  #1,d4\n"
    "3:  move.b  (a1)+,d2\n"
    "    move.b  0(a2,d2.l),(a0)+\n"
    "    dbf     d4,3b\n"
    "4:  movem.l (sp)+,d2-d5/a2\n"
    "    rts\n"
    "\n"
    "    .even\n"
    "    .globl _amiga_blit_blend_map_asm\n"
    "_amiga_blit_blend_map_asm:\n"
    "    movem.l d2-d6/a2-a3,-(sp)\n"       /* 7 regs: args from 32(sp) */
    "    move.l  32(sp),a0\n"               /* dst */
    "    move.l  36(sp),a1\n"               /* src */
    "    move.l  40(sp),d4\n"               /* n */
    "    move.l  44(sp),a2\n"               /* map */
    "    move.l  48(sp),a3\n"               /* pal [256][256] */
    "    moveq   #0,d2\n"
    "    moveq   #0,d3\n"
    "    move.l  d4,d5\n"
    "    lsr.l   #2,d5\n"
    "    beq.s   2f\n"
    "1:  move.l  (a1)+,d1\n"                /* src s0..s3 */
    "    move.l  (a0),d6\n"                 /* dst t0..t3 */
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  d1,d2\n"                   /* s0 */
    "    move.b  d6,d3\n"                   /* t0 */
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"           /* d3.w = t0:m0 */
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  0(a3,d3.l),d0\n"           /* out0 */
    "    move.b  d1,d2\n"                   /* s1 */
    "    move.b  d6,d3\n"                   /* t1 */
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    lsl.w   #8,d0\n"
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  0(a3,d3.l),d0\n"           /* d0.w = out0 out1 */
    "    move.b  d1,d2\n"                   /* s2 */
    "    move.b  d6,d3\n"                   /* t2 */
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    swap    d0\n"
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  0(a3,d3.l),d0\n"
    "    move.b  d1,d2\n"                   /* s3 */
    "    move.b  d6,d3\n"                   /* t3 */
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    lsl.w   #8,d0\n"
    "    move.b  0(a3,d3.l),d0\n"           /* d0 = out0..out3 */
    "    move.l  d0,(a0)+\n"
    "    subq.l  #1,d5\n"
    "    bne.s   1b\n"
    "2:  and.l   #3,d4\n"
    "    beq.s   4f\n"
    "    subq.w  #1,d4\n"
    "3:  move.b  (a1)+,d2\n"
    "    move.b  (a0),d3\n"
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    move.b  0(a3,d3.l),(a0)+\n"
    "    dbf     d4,3b\n"
    "4:  movem.l (sp)+,d2-d6/a2-a3\n"
    "    rts\n"
    "\n"
    /* amiga_map_lines(dst, step, src, n0, dn, count, map, pal):
       whole unclipped tile in one call. Line i: width n0 + i*dn,
       destination dst - i*step, source contiguous. */
    "    .even\n"
    "    .globl _amiga_map_lines_asm\n"
    "_amiga_map_lines_asm:\n"
    "    movem.l d2-d7/a2-a4,-(sp)\n"       /* 9 regs: args from 40(sp) */
    "    move.l  40(sp),a3\n"               /* dst */
    "    move.l  44(sp),a4\n"               /* step */
    "    move.l  48(sp),a1\n"               /* src */
    "    move.l  52(sp),d4\n"               /* width */
    "    move.l  56(sp),d5\n"               /* dn */
    "    move.l  60(sp),d6\n"               /* count */
    "    move.l  64(sp),a2\n"               /* map */
    "    moveq   #0,d2\n"
    "    moveq   #0,d3\n"
    "    tst.l   d6\n"
    "    ble     9f\n"
    "5:  move.l  a3,a0\n"
    "    move.l  d4,d7\n"
    "    lsr.l   #2,d7\n"
    "    beq.s   2f\n"
    "1:  move.l  (a1)+,d1\n"
    "    rol.l   #8,d1\n"
    "    move.b  d1,d2\n"
    "    rol.l   #8,d1\n"
    "    move.b  d1,d3\n"
    "    move.b  0(a2,d2.l),d0\n"
    "    rol.l   #8,d1\n"
    "    lsl.w   #8,d0\n"
    "    move.b  d1,d2\n"
    "    move.b  0(a2,d3.l),d0\n"
    "    rol.l   #8,d1\n"
    "    swap    d0\n"
    "    move.b  d1,d3\n"
    "    move.b  0(a2,d2.l),d0\n"
    "    lsl.w   #8,d0\n"
    "    move.b  0(a2,d3.l),d0\n"
    "    move.l  d0,(a0)+\n"
    "    subq.l  #1,d7\n"
    "    bne.s   1b\n"
    "2:  move.l  d4,d1\n"
    "    and.l   #3,d1\n"
    "    beq.s   4f\n"
    "    subq.w  #1,d1\n"
    "3:  move.b  (a1)+,d2\n"
    "    move.b  0(a2,d2.l),(a0)+\n"
    "    dbf     d1,3b\n"
    "4:  suba.l  a4,a3\n"
    "    add.l   d5,d4\n"
    "    subq.l  #1,d6\n"
    "    bne     5b\n"
    "9:  movem.l (sp)+,d2-d7/a2-a4\n"
    "    rts\n"
    "\n"
    /* amiga_blend_lines(dst, step, src, n0, dn, count, map, pal): the same
       lines with dst = pal[dst][map[src]]; the line count stays on the stack */
    "    .even\n"
    "    .globl _amiga_blend_lines_asm\n"
    "_amiga_blend_lines_asm:\n"
    "    movem.l d2-d7/a2-a5,-(sp)\n"       /* 10 regs: args from 44(sp) */
    "    move.l  44(sp),a4\n"               /* dst */
    "    move.l  48(sp),a5\n"               /* step */
    "    move.l  52(sp),a1\n"               /* src */
    "    move.l  56(sp),d4\n"               /* width */
    "    move.l  60(sp),d5\n"               /* dn */
    "    move.l  68(sp),a2\n"               /* map */
    "    move.l  72(sp),a3\n"               /* pal */
    "    moveq   #0,d2\n"
    "    moveq   #0,d3\n"
    "    tst.l   64(sp)\n"
    "    ble     9f\n"
    "5:  move.l  a4,a0\n"
    "    move.l  d4,d7\n"
    "    lsr.l   #2,d7\n"
    "    beq.s   2f\n"
    "1:  move.l  (a1)+,d1\n"
    "    move.l  (a0),d6\n"
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  d1,d2\n"
    "    move.b  d6,d3\n"
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  0(a3,d3.l),d0\n"
    "    move.b  d1,d2\n"
    "    move.b  d6,d3\n"
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    lsl.w   #8,d0\n"
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  0(a3,d3.l),d0\n"
    "    move.b  d1,d2\n"
    "    move.b  d6,d3\n"
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    swap    d0\n"
    "    rol.l   #8,d1\n"
    "    rol.l   #8,d6\n"
    "    move.b  0(a3,d3.l),d0\n"
    "    move.b  d1,d2\n"
    "    move.b  d6,d3\n"
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    lsl.w   #8,d0\n"
    "    move.b  0(a3,d3.l),d0\n"
    "    move.l  d0,(a0)+\n"
    "    subq.l  #1,d7\n"
    "    bne.s   1b\n"
    "2:  move.l  d4,d1\n"
    "    and.l   #3,d1\n"
    "    beq.s   4f\n"
    "    subq.w  #1,d1\n"
    "3:  move.b  (a1)+,d2\n"
    "    move.b  (a0),d3\n"
    "    lsl.w   #8,d3\n"
    "    move.b  0(a2,d2.l),d3\n"
    "    move.b  0(a3,d3.l),(a0)+\n"
    "    dbf     d1,3b\n"
    "4:  suba.l  a5,a4\n"
    "    add.l   d5,d4\n"
    "    subq.l  #1,64(sp)\n"
    "    bne     5b\n"
    "9:  movem.l (sp)+,d2-d7/a2-a5\n"
    "    rts\n");

void amiga_map_lines_asm(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                         const unsigned char *map, const unsigned char *pal);
void amiga_blend_lines_asm(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                           const unsigned char *map, const unsigned char *pal);

/* ---- the entry points with the optional check ---- */
static int verify = -1;
static unsigned long v_calls, v_bad;

static int use_c = -1;   /* ENV:DIABLO_BLIT_C: plain C loops (A/B measurement) */

static int verify_on(void)
{
    if (verify < 0) {
        char buf[8];
        verify = msdl_env("DIABLO_VERIFY_BLIT", buf, sizeof(buf)) >= 0;
        use_c = msdl_env("DIABLO_BLIT_C", buf, sizeof(buf)) >= 0;
        if (verify) msdl_logf(SDL_LOG_PRIORITY_INFO, "blit: checking the 68060 loops against C");
        if (use_c) msdl_logf(SDL_LOG_PRIORITY_INFO, "blit: plain C loops (DIABLO_BLIT_C)");
    }
    return verify;
}

static void report(const char *what, unsigned n)
{
    v_bad++;
    if (v_bad <= 20 || (v_bad & 1023) == 0)
        msdl_logf(SDL_LOG_PRIORITY_ERROR, "blit: %s differs (n %u), %lu bad of %lu calls", what, n, v_bad, v_calls);
}

static void map_checked(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map)
{
    unsigned i;
    v_calls++;
    amiga_blit_map_asm(dst, src, n, map);
    for (i = 0; i < n; i++) {
        if (dst[i] != map[src[i]]) {
            report("map", n);
            break;
        }
    }
    if ((v_calls & 0xFFFF) == 0) msdl_logf(SDL_LOG_PRIORITY_INFO, "blit: %lu calls checked, %lu bad", v_calls, v_bad);
}

extern unsigned long msdl_counter[8];

static void map_c(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map)
{
    unsigned i;
    msdl_counter[4] += n;
    msdl_counter[6]++;
    for (i = 0; i < n; i++) dst[i] = map[src[i]];
}

static void blend_checked(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map,
                          const unsigned char *pal)
{
    static unsigned char before[1024];
    unsigned i;
    if (n > sizeof(before)) {
        amiga_blit_blend_map_asm(dst, src, n, map, pal);
        return;
    }
    v_calls++;
    memcpy(before, dst, n);
    amiga_blit_blend_map_asm(dst, src, n, map, pal);
    for (i = 0; i < n; i++) {
        if (dst[i] != pal[before[i] * 256 + map[src[i]]]) {
            report("blend", n);
            break;
        }
    }
}

static void blend_c(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map,
                    const unsigned char *pal)
{
    unsigned i;
    msdl_counter[5] += n;
    msdl_counter[7]++;
    for (i = 0; i < n; i++) dst[i] = pal[dst[i] * 256 + map[src[i]]];
}

/* The game calls through these pointers (blit_impl.hpp): the asm loops
   directly, chosen once by the first call (ENV:DIABLO_VERIFY_BLIT checks
   them against C, ENV:DIABLO_BLIT_C uses plain C). */
static void map_first(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map);
static void blend_first(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map,
                        const unsigned char *pal);
void (*amiga_blit_map_fn)(unsigned char *, const unsigned char *, unsigned, const unsigned char *) = map_first;
void (*amiga_blit_blend_map_fn)(unsigned char *, const unsigned char *, unsigned, const unsigned char *,
                                const unsigned char *) = blend_first;

static void choose(void)
{
    if (verify_on()) {
        amiga_blit_map_fn = map_checked;
        amiga_blit_blend_map_fn = blend_checked;
    } else if (use_c) {
        amiga_blit_map_fn = map_c;
        amiga_blit_blend_map_fn = blend_c;
    } else {
        amiga_blit_map_fn = amiga_blit_map_asm;
        amiga_blit_blend_map_fn = amiga_blit_blend_map_asm;
    }
}

/* whole tiles: C version (also the reference of the check) */
static void lines_c(int blend, unsigned char *dst, int step, const unsigned char *src, int n, int dn, int count,
                    const unsigned char *map, const unsigned char *pal)
{
    int i, k;
    msdl_counter[blend ? 7 : 6]++;
    for (k = 0; k < count; k++, dst -= step, src += n, n += dn) {
        msdl_counter[blend ? 5 : 4] += n;
        for (i = 0; i < n; i++) dst[i] = blend ? pal[dst[i] * 256 + map[src[i]]] : map[src[i]];
    }
}

static void map_lines_c(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                        const unsigned char *map, const unsigned char *pal)
{
    lines_c(0, dst, step, src, n0, dn, count, map, pal);
}

static void blend_lines_c(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                          const unsigned char *map, const unsigned char *pal)
{
    lines_c(1, dst, step, src, n0, dn, count, map, pal);
}

/* check: the lines are saved, drawn by C, saved as the expected result,
   restored, drawn by asm and compared */
static void lines_checked(int blend, unsigned char *dst, int step, const unsigned char *src, int n0, int dn,
                          int count, const unsigned char *map, const unsigned char *pal)
{
    static unsigned char before[64 * 64], expect[64 * 64];
    int k, n, o = 0, total = 0;
    for (k = 0, n = n0; k < count; k++, n += dn) total += n;
    if (count > 64 || total > (int)sizeof(before)) {
        (blend ? amiga_blend_lines_asm : amiga_map_lines_asm)(dst, step, src, n0, dn, count, map, pal);
        return;
    }
    for (k = 0, n = n0; k < count; k++, o += n, n += dn) memcpy(before + o, dst - k * step, n);
    lines_c(blend, dst, step, src, n0, dn, count, map, pal);
    for (k = 0, n = n0, o = 0; k < count; k++, o += n, n += dn) {
        memcpy(expect + o, dst - k * step, n);
        memcpy(dst - k * step, before + o, n);
    }
    (blend ? amiga_blend_lines_asm : amiga_map_lines_asm)(dst, step, src, n0, dn, count, map, pal);
    v_calls++;
    for (k = 0, n = n0, o = 0; k < count; k++, o += n, n += dn) {
        if (memcmp(expect + o, dst - k * step, n) != 0) {
            report(blend ? "blend lines" : "map lines", (unsigned)total);
            break;
        }
    }
}

static void map_lines_checked(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                              const unsigned char *map, const unsigned char *pal)
{
    lines_checked(0, dst, step, src, n0, dn, count, map, pal);
}

static void blend_lines_checked(unsigned char *dst, int step, const unsigned char *src, int n0, int dn,
                                int count, const unsigned char *map, const unsigned char *pal)
{
    lines_checked(1, dst, step, src, n0, dn, count, map, pal);
}

typedef void (*lines_fn)(unsigned char *, int, const unsigned char *, int, int, int, const unsigned char *,
                         const unsigned char *);
static void map_lines_first(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                            const unsigned char *map, const unsigned char *pal);
static void blend_lines_first(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                              const unsigned char *map, const unsigned char *pal);
lines_fn amiga_map_lines_fn = map_lines_first;
lines_fn amiga_blend_lines_fn = blend_lines_first;

static void choose_lines(void)
{
    if (verify_on()) {
        amiga_map_lines_fn = map_lines_checked;
        amiga_blend_lines_fn = blend_lines_checked;
    } else if (use_c) {
        amiga_map_lines_fn = map_lines_c;
        amiga_blend_lines_fn = blend_lines_c;
    } else {
        amiga_map_lines_fn = amiga_map_lines_asm;
        amiga_blend_lines_fn = amiga_blend_lines_asm;
    }
}

static void map_lines_first(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                            const unsigned char *map, const unsigned char *pal)
{
    choose_lines();
    amiga_map_lines_fn(dst, step, src, n0, dn, count, map, pal);
}

static void blend_lines_first(unsigned char *dst, int step, const unsigned char *src, int n0, int dn, int count,
                              const unsigned char *map, const unsigned char *pal)
{
    choose_lines();
    amiga_blend_lines_fn(dst, step, src, n0, dn, count, map, pal);
}

static void map_first(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map)
{
    choose();
    amiga_blit_map_fn(dst, src, n, map);
}

static void blend_first(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map,
                        const unsigned char *pal)
{
    choose();
    amiga_blit_blend_map_fn(dst, src, n, map, pal);
}
