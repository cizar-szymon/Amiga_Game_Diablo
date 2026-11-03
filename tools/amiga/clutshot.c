/* Screenshot of the front 8-bit screen with its own palette (AmiAgent's
   SHOT converts CLUT screens with a wrong palette): RAM:clutshot.ppm */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/rastport.h>
#include <stdio.h>
#include <stdlib.h>
extern struct IntuitionBase *IntuitionBase;
int main(void)
{
    struct Screen *s = IntuitionBase->FirstScreen;
    int w = s->Width, h = s->Height, x, y;
    ULONG pal[256 * 3];
    UBYTE *pens = malloc(((w + 15) & ~15) * (h + 1));
    struct RastPort tmp;
    FILE *f;
    if (pens == NULL) return 20;
    GetRGB32(s->ViewPort.ColorMap, 0, 256, pal);
    tmp = s->RastPort;
    tmp.Layer = NULL;
    tmp.BitMap = AllocBitMap(w, 1, 8, 0, s->RastPort.BitMap);
    ReadPixelArray8(&s->RastPort, 0, 0, w - 1, h - 1, pens, &tmp);
    f = fopen("RAM:clutshot.ppm", "wb");
    if (f == NULL) return 20;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            UBYTE p = pens[y * ((w + 15) & ~15) + x];
            fputc(pal[p * 3] >> 24, f);
            fputc(pal[p * 3 + 1] >> 24, f);
            fputc(pal[p * 3 + 2] >> 24, f);
        }
    }
    fclose(f);
    FreeBitMap(tmp.BitMap);
    printf("RAM:clutshot.ppm %dx%d\n", w, h);
    return 0;
}
