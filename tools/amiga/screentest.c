/* Memory check: open and close an 8-bit Picasso96 screen as the game does,
   N times, and print the free Fast RAM before and after each round.
   Usage: screentest [rounds] [step] [depth] [exclusive]
   (step 1: screen only, 2: + window, 3: + palette, 4: + bitmap lock,
   5: + BltBitMap; depth 8 or 16; exclusive 1 / 0) */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

struct Library *P96Base;
static ULONG pal[1 + 256 * 3 + 1];

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 3, step = argc > 2 ? atoi(argv[2]) : 5, r;
    int depth = argc > 3 ? atoi(argv[3]) : 8, excl = argc > 4 ? atoi(argv[4]) : 1;
    ULONG fmt = depth == 16 ? RGBFB_R5G6B5 : RGBFB_CLUT, fmts = depth == 16 ? RGBFF_R5G6B5 : RGBFF_CLUT;
    ULONG id;
    P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    if (P96Base == NULL) return 20;
    id = p96BestModeIDTags(P96BIDTAG_NominalWidth, 640, P96BIDTAG_NominalHeight, 480, P96BIDTAG_Depth, 8, TAG_DONE);
    {
        struct List *ml = p96AllocModeListTags(P96MA_FormatsAllowed, fmts, TAG_DONE);
        struct P96Mode *m;
        if (ml != NULL) {
            for (m = (struct P96Mode *)ml->lh_Head; m->Node.ln_Succ != NULL; m = (struct P96Mode *)m->Node.ln_Succ)
                if (m->Width == 640 && m->Height == 480 && m->Depth == depth) id = m->DisplayID;
            p96FreeModeList(ml);
        }
    }
    for (r = 0; r < rounds; r++) {
        ULONG before = AvailMem(MEMF_FAST), after;
        struct Screen *s = p96OpenScreenTags(P96SA_DisplayID, id, P96SA_Width, 640, P96SA_Height, 480, P96SA_Depth, depth,
                                             P96SA_RGBFormat, fmt, P96SA_Quiet, TRUE, P96SA_ShowTitle, FALSE,
                                             P96SA_Exclusive, excl, TAG_DONE);
        struct Window *w = NULL;
        if (s == NULL) { printf("no screen\n"); break; }
        if (step >= 2)
            w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 0, WA_Top, 0, WA_Width, 640, WA_Height, 480,
                               WA_Backdrop, TRUE, WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                               WA_ReportMouse, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE,
                               WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY, TAG_DONE);
        if (step >= 3 && depth == 8) {
            int i;
            pal[0] = 256UL << 16;
            for (i = 0; i < 256; i++) pal[1 + i * 3] = pal[2 + i * 3] = pal[3 + i * 3] = (ULONG)i * 0x01010101UL;
            LoadRGB32(&s->ViewPort, pal);
        }
        if (step >= 4) {
            struct RenderInfo ri;
            LONG lock = p96LockBitMap(s->RastPort.BitMap, (UBYTE *)&ri, sizeof(ri));
            if (lock) p96UnlockBitMap(s->RastPort.BitMap, lock);
        }
        if (step >= 5) BltBitMap(s->RastPort.BitMap, 8, 8, s->RastPort.BitMap, 0, 0, 600, 400, 0xC0, 0xFF, NULL);
        WaitBlit();
        Delay(25);
        if (w) CloseWindow(w);
        CloseScreen(s);
        after = AvailMem(MEMF_FAST);
        printf("round %d: fast %lu -> %lu (%ld)\n", r, before, after, (long)after - (long)before);
    }
    CloseLibrary(P96Base);
    return 0;
}
