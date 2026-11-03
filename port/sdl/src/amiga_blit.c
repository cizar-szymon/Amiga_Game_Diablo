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
    "    rts\n");

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

void amiga_blit_map(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map)
{
    unsigned i;
    if (!verify_on()) {
        if (use_c) {
            for (i = 0; i < n; i++) dst[i] = map[src[i]];
            return;
        }
        amiga_blit_map_asm(dst, src, n, map);
        return;
    }
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

void amiga_blit_blend_map(unsigned char *dst, const unsigned char *src, unsigned n, const unsigned char *map,
                          const unsigned char *pal)
{
    static unsigned char before[1024];
    unsigned i;
    if (!verify_on() || n > sizeof(before)) {
        if (use_c) {
            for (i = 0; i < n; i++) dst[i] = pal[dst[i] * 256 + map[src[i]]];
            return;
        }
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
