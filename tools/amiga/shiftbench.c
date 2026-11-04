/* BltBitMap of a 640x352 area onto itself, shifted by (dx, dy), on an
   8-bit Picasso96 screen 640x480 (as the game's view scroll, msdl_video.c
   inc_shift). Prints the time per shift for each direction.
   Usage: shiftbench [repeats] */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <libraries/Picasso96.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <proto/timer.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

struct Library *P96Base;
struct Device *TimerBase;
static struct timerequest tr;

static ULONG now(ULONG *per_s)
{
    struct EClockVal ev;
    *per_s = ReadEClock(&ev);
    return ev.ev_lo;
}

int main(int argc, char **argv)
{
    static const int sh[][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 }, { 2, 1 }, { -2, 1 }, { 2, -1 }, { -2, -1 },
                                 { 4, 0 }, { -4, 0 }, { 0, 8 }, { 0, -8 } };
    int reps = argc > 1 ? atoi(argv[1]) : 20, i, k;
    ULONG id, per_s;
    struct Screen *s;
    P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    if (P96Base == NULL) return 20;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&tr, 0) != 0) return 20;
    TimerBase = tr.tr_node.io_Device;
    id = p96BestModeIDTags(P96BIDTAG_NominalWidth, 640, P96BIDTAG_NominalHeight, 480, P96BIDTAG_Depth, 8, TAG_DONE);
    s = p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, 640, P96SA_Height, 480, P96SA_Depth, 8,
                          P96SA_RGBFormat, RGBFB_CLUT, P96SA_Quiet, TRUE, P96SA_ShowTitle, FALSE, TAG_DONE);
    if (s == NULL) { printf("no screen\n"); return 20; }
    {
        struct RenderInfo ri;
        LONG lock = p96LockBitMap(s->RastPort.BitMap, (UBYTE *)&ri, sizeof(ri));
        if (lock) {
            int y, x;
            for (y = 0; y < 480; y++)
                for (x = 0; x < 640; x++) ((UBYTE *)ri.Memory)[y * ri.BytesPerRow + x] = (UBYTE)(x ^ y);
            p96UnlockBitMap(s->RastPort.BitMap, lock);
        }
    }
    for (k = 0; k < (int)(sizeof(sh) / sizeof(sh[0])); k++) {
        int dx = sh[k][0], dy = sh[k][1];
        int w = 640 - (dx < 0 ? -dx : dx), h = 352 - (dy < 0 ? -dy : dy);
        ULONG t0, t1;
        WaitBlit();
        t0 = now(&per_s);
        for (i = 0; i < reps; i++) {
            BltBitMap(s->RastPort.BitMap, dx > 0 ? dx : 0, dy > 0 ? dy : 0, s->RastPort.BitMap, dx < 0 ? -dx : 0,
                      dy < 0 ? -dy : 0, w, h, 0xC0, 0xFF, NULL);
            WaitBlit();
        }
        t1 = now(&per_s);
        printf("shift dx %3d dy %3d: %lu us\n", dx, dy, (unsigned long)((t1 - t0) / (per_s / 10000) * 100 / reps));
    }
    CloseScreen(s);
    CloseDevice((struct IORequest *)&tr);
    CloseLibrary(P96Base);
    return 0;
}
