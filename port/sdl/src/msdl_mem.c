/* memcpy for the game. libnix's memcpy calls exec's CopyMem for every
   copy (the Warp's patched one); the game copies many short rows (tile
   scratch, partial copies of the scene, text): in the dungeon fight 15% of
   the time went there (profile 4.11.2026). Here: bytes until the target is
   long aligned, then 16 bytes per pass with long moves (the 68060 reads
   unaligned longs), the rest in bytes. No overlap handling (as memcpy).
   In asm, so that the compiler cannot turn the loop back into memcpy. */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);

__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _memcpy\n"
    "_memcpy:\n"
    "    move.l  4(sp),a1\n"                /* dst */
    "    move.l  8(sp),a0\n"                /* src */
    "    move.l  12(sp),d1\n"               /* n */
    "    cmp.l   #16,d1\n"
    "    bcs.s   5f\n"
    "    move.l  a1,d0\n"
    "    and.l   #3,d0\n"                   /* the whole register: d0 is subtracted from n */
    "    beq.s   2f\n"
    "    neg.l   d0\n"
    "    addq.l  #4,d0\n"                   /* 1..3 bytes to the long boundary */
    "    sub.l   d0,d1\n"
    "    subq.w  #1,d0\n"
    "1:  move.b  (a0)+,(a1)+\n"
    "    dbf     d0,1b\n"
    "2:  move.l  d1,d0\n"
    "    lsr.l   #4,d0\n"
    "    beq.s   4f\n"
    "3:  move.l  (a0)+,(a1)+\n"
    "    move.l  (a0)+,(a1)+\n"
    "    move.l  (a0)+,(a1)+\n"
    "    move.l  (a0)+,(a1)+\n"
    "    subq.l  #1,d0\n"
    "    bne.s   3b\n"
    "4:  and.w   #15,d1\n"
    "5:  subq.w  #1,d1\n"
    "    bmi.s   7f\n"
    "6:  move.b  (a0)+,(a1)+\n"
    "    dbf     d1,6b\n"
    "7:  move.l  4(sp),d0\n"
    "    rts\n");
