/* Test of the game's memcpy (port/sdl/src/msdl_mem.c, linked in place of
   libnix's): every length 0..300 at every source / target misalignment
   0..7, compared with a byte loop; guard bytes around the target. */
#include <stdio.h>
#include <string.h>

static unsigned char src[1024], dst[1024], ref[1024];

int main(void)
{
    int n, so, d0, i, bad = 0, tests = 0;
    for (i = 0; i < 1024; i++) src[i] = (unsigned char)(i * 7 + 3);
    for (n = 0; n <= 300; n++)
        for (so = 0; so < 8; so++)
            for (d0 = 0; d0 < 8; d0++) {
                void *r;
                memset(dst, 0xAA, sizeof(dst));
                memset(ref, 0xAA, sizeof(ref));
                for (i = 0; i < n; i++) ref[64 + d0 + i] = src[so + i];
                r = memcpy(dst + 64 + d0, src + so, (size_t)n);
                tests++;
                if (r != dst + 64 + d0 || memcmp(dst, ref, sizeof(dst)) != 0) {
                    if (bad < 10) printf("BAD n %d src+%d dst+%d\n", n, so, d0);
                    bad++;
                }
            }
    printf("memcpy: %d tests, %d bad\n", tests, bad);
    return bad != 0;
}
