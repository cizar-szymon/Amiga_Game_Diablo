/* Test mode (ENV:DIABLO_CELLS): how much of each frame really changes.
   Question: would a renderer that copies the unchanged part of the last
   frame on the card (blitter, VRAM->VRAM, shifted by the camera move) and
   draws only the changed cells with the CPU pay off?

   Every 10th frame the frame is compared with the one before it:
     1. the camera shift (dx, dy) is guessed by matching strips of a few
        rows of the view over dx -16..16, dy -12..12;
     2. the view (rows above the panel) is split into 32x32 cells; a cell is
        "changed" when any pixel differs from the last frame at the shifted
        position (or the shifted position is outside: newly exposed);
     3. the panel (bottom rows) is compared without a shift.
   Every 5 s a log line: changed view cells with the shift and without it,
   changed panel cells, how often the camera moved. Costs time itself:
   the frame rate is not the real one while it runs. */
#include <stdlib.h>
#include <string.h>

#include "msdl_internal.h"

#define CELL 32
#define PANEL_H 128
#define MAXDX 16
#define MAXDY 12

static int cells_on = -1;
static Uint8 *prev;
static int prev_w, prev_h, have_prev;
static unsigned long frame_no;
static unsigned long n_frames, sum_view_shift, sum_view_still, sum_panel, n_moved, n_cells_view, n_cells_panel;
static unsigned long t0;

static int find_shift(const Uint8 *cur, int pitch, int w, int view_h, int *best_dx, int *best_dy)
{
    /* sample rows across the view, a 128-pixel strip in the middle */
    static const int rows[] = { 40, 90, 140, 190, 240, 290 };
    int dx, dy, i, best = 0x7FFFFFFF;
    int x0 = w / 2 - 64;
    *best_dx = 0;
    *best_dy = 0;
    for (dy = -MAXDY; dy <= MAXDY; dy++) {
        for (dx = -MAXDX; dx <= MAXDX; dx++) {
            int diff = 0;
            for (i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])) && diff < best; i++) {
                int y = rows[i], py = y - dy, x;
                const Uint8 *c, *p;
                if (y >= view_h || py < 0 || py >= view_h) {
                    diff += 128;
                    continue;
                }
                c = cur + y * pitch + x0;
                p = prev + py * prev_w + x0 - dx;
                for (x = 0; x < 128; x++) diff += c[x] != p[x];
            }
            /* prefer no shift on a tie */
            if (diff < best || (diff == best && dx == 0 && dy == 0)) {
                best = diff;
                *best_dx = dx;
                *best_dy = dy;
            }
        }
    }
    return best;
}

/* Changed cells of rows y0..y1 with the content moved by (dx, dy). */
static int count_cells(const Uint8 *cur, int pitch, int w, int y0, int y1, int dx, int dy, int *ncells)
{
    int cy, cx, changed = 0;
    *ncells = 0;
    for (cy = y0; cy < y1; cy += CELL) {
        for (cx = 0; cx < w; cx += CELL) {
            int y, x, diff = 0;
            int ye = cy + CELL < y1 ? cy + CELL : y1;
            int xe = cx + CELL < w ? cx + CELL : w;
            (*ncells)++;
            for (y = cy; y < ye && !diff; y++) {
                int py = y - dy;
                const Uint8 *c = cur + y * pitch;
                if (py < y0 || py >= y1) {
                    diff = 1;
                    break;
                }
                for (x = cx; x < xe; x++) {
                    int px = x - dx;
                    if (px < 0 || px >= w || c[x] != prev[py * prev_w + px]) {
                        diff = 1;
                        break;
                    }
                }
            }
            changed += diff;
        }
    }
    return changed;
}

void msdl_cells_frame(const Uint8 *pixels, int pitch, int w, int h)
{
    int view_h, dx, dy, n;
    if (cells_on < 0) {
        char buf[8];
        cells_on = msdl_env("DIABLO_CELLS", buf, sizeof(buf)) >= 0;
        if (cells_on) msdl_logf(SDL_LOG_PRIORITY_INFO, "cells: measuring changed 32x32 cells every 10th frame");
    }
    if (!cells_on) return;
    frame_no++;
    if (prev == NULL || prev_w != w || prev_h != h) {
        free(prev);
        prev = (Uint8 *)malloc((size_t)w * h);
        prev_w = w;
        prev_h = h;
        have_prev = 0;
        if (prev == NULL) return;
    }
    view_h = h - PANEL_H;
    if (have_prev) {
        int cv, cs, cp, nv, np;
        find_shift(pixels, pitch, w, view_h, &dx, &dy);
        cv = count_cells(pixels, pitch, w, 0, view_h, dx, dy, &nv);
        cs = (dx == 0 && dy == 0) ? cv : count_cells(pixels, pitch, w, 0, view_h, 0, 0, &n);
        cp = count_cells(pixels, pitch, w, view_h, h, 0, 0, &np);
        n_frames++;
        sum_view_shift += cv;
        sum_view_still += cs;
        sum_panel += cp;
        n_cells_view = nv;
        n_cells_panel = np;
        if (dx != 0 || dy != 0) n_moved++;
        have_prev = 0;
    }
    /* keep the frame before every 10th */
    if (frame_no % 10 == 9) {
        int y;
        for (y = 0; y < h; y++) memcpy(prev + y * w, pixels + y * pitch, (size_t)w);
        have_prev = 1;
    }
    {
        unsigned long now = SDL_GetTicks();
        if (t0 == 0) t0 = now;
        if (now - t0 >= 5000 && n_frames > 0) {
            msdl_logf(SDL_LOG_PRIORITY_INFO,
                      "cells: view %lu%% changed with the camera shift (%lu%% without), panel %lu%%, camera moved in %lu of %lu (of %lu+%lu cells)",
                      sum_view_shift * 100 / (n_frames * n_cells_view), sum_view_still * 100 / (n_frames * n_cells_view),
                      sum_panel * 100 / (n_frames * n_cells_panel), n_moved, n_frames, n_cells_view, n_cells_panel);
            n_frames = sum_view_shift = sum_view_still = sum_panel = n_moved = 0;
            t0 = now;
        }
    }
}
