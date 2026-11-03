/* Mini-SDL2: the frames-per-second counter in the corner of the screen
   (on by default, the Help key switches it). Drawn into the finished frame
   just before it is shown, with a 3x5 font doubled: a few hundred bytes
   written, nothing read. */
#include <SDL.h>
#include "msdl_internal.h"

int msdl_fps_on = 1;

static Uint32 fps_t0;
static int fps_frames;
static int fps_value = -1;      /* tenths of a frame per second */

/* Counts a shown frame; the value is renewed every second. */
void msdl_fps_tick(void)
{
    Uint32 now = SDL_GetTicks();
    if (fps_t0 == 0) fps_t0 = now;
    fps_frames++;
    if (now - fps_t0 >= 1000) {
        fps_value = (int)((Uint32)fps_frames * 10000u / (now - fps_t0));
        fps_frames = 0;
        fps_t0 = now;
    }
}

/* 3x5 glyphs, one row of 3 bits each (bit 2 = left) */
static const Uint8 glyph_digit[10][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,2,2}, {7,5,7,5,7}, {7,5,7,1,7}
};
static const Uint8 glyph_dot[5] = {0,0,0,0,2};
static const Uint8 glyph_f[5] = {7,4,6,4,4};
static const Uint8 glyph_p[5] = {6,5,6,4,4};
static const Uint8 glyph_s[5] = {7,4,7,1,7};

static const Uint8 *glyph(char c)
{
    if (c >= '0' && c <= '9') return glyph_digit[c - '0'];
    if (c == '.') return glyph_dot;
    if (c == 'F') return glyph_f;
    if (c == 'P') return glyph_p;
    if (c == 'S') return glyph_s;
    return NULL;
}

/* "28.3 FPS", or nothing in the first second */
static int fps_text(char *buf)
{
    int n = 0, v = fps_value;
    if (v < 0) return 0;
    if (v >= 1000) buf[n++] = (char)('0' + v / 1000 % 10);
    buf[n++] = (char)('0' + v / 100 % 10);
    buf[n++] = (char)('0' + v / 10 % 10);
    buf[n++] = '.';
    buf[n++] = (char)('0' + v % 10);
    buf[n++] = ' ';
    buf[n++] = 'F';
    buf[n++] = 'P';
    buf[n++] = 'S';
    return n;
}

#define SCALE 2
#define ORIGIN_X 4
#define ORIGIN_Y 4

/* The counter's box, for both pixel sizes: plot(x, y, colour 0 bg / 1 fg). */
#define DRAW_TEXT(PX_T, FG, BG) \
    char buf[16]; \
    int n = fps_text(buf), i, gx, gy, x, y; \
    int bw = n * 4 * SCALE + SCALE, bh = 7 * SCALE; \
    if (n == 0 || w < bw + ORIGIN_X || h < bh + ORIGIN_Y) return; \
    for (y = 0; y < bh; y++) { \
        PX_T *row = (PX_T *)((Uint8 *)px + (ORIGIN_Y + y) * pitch) + ORIGIN_X; \
        for (x = 0; x < bw; x++) row[x] = BG; \
    } \
    for (i = 0; i < n; i++) { \
        const Uint8 *g = glyph(buf[i]); \
        if (g == NULL) continue; \
        for (gy = 0; gy < 5; gy++) { \
            for (gx = 0; gx < 3; gx++) { \
                if (!(g[gy] & (4 >> gx))) continue; \
                for (y = 0; y < SCALE; y++) { \
                    PX_T *row = (PX_T *)((Uint8 *)px + (ORIGIN_Y + SCALE + gy * SCALE + y) * pitch) \
                                + ORIGIN_X + SCALE + (i * 4 + gx) * SCALE; \
                    for (x = 0; x < SCALE; x++) row[x] = FG; \
                } \
            } \
        } \
    }

void msdl_fps_draw8(Uint8 *px, int pitch, int w, int h, Uint8 fg, Uint8 bg)
{
    if (!msdl_fps_on || px == NULL) return;
    {
        DRAW_TEXT(Uint8, fg, bg)
    }
}

void msdl_fps_draw16(Uint16 *px, int pitch, int w, int h)
{
    if (!msdl_fps_on || px == NULL) return;
    {
        DRAW_TEXT(Uint16, 0xFFFFu, 0x0000u)
    }
}
