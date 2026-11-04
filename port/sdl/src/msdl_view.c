/* What changed in the view since the last present (stage 2 of the cell
   renderer, docs/RENDERER_PLAN.md).

   The game's scene renderer (amiga_cells.cpp) reports per frame the camera
   move and the dirty rectangles of the scene; everything drawn on top of the
   scene (texts, health bars, cursor, boxes) reports its rectangle as an
   overlay. What the card shows after the next present is then:

     shift(last frame, camera move)
       + the dirty scene rectangles
       + the overlay rectangles of the last frame, moved with the camera
         (their pixels went along with the shift and must be repaired)
       + the overlay rectangles of this frame

   ENV:DIABLO_VERIFY_PRESENT keeps that model of the card in Fast RAM and
   compares it with the real frame after every present: a difference means a
   drawing call that does not report its overlay. */
#include <stdlib.h>
#include <string.h>

#include "msdl_internal.h"

#define MAXR 512

typedef struct { int x, y, w, h; } VRect;

static VRect scene_r[MAXR], ov_cur[MAXR], ov_prev[MAXR];
/* everything drawn on the game's buffer outside the scene passes since the
   scene was last copied into it (stage 2c: copy back only these) */
static VRect dmg[MAXR];
static int n_dmg, dmg_all = 1;
static int n_scene, n_cur, n_prev;
static int cur_all, prev_all;
static int have_scene, scene_full, view_dx, view_dy;
static int view_w, view_h;

static Uint8 *model;
static int model_w, model_h, model_valid;
static int verify = -1;
static unsigned long v_frames, v_bad;

void msdl_view_scene(int dx, int dy, int vw, int vh, const int *rects, int n, int full)
{
    int i;
    view_dx = dx;
    view_dy = dy;
    view_w = vw;
    view_h = vh;
    scene_full = full;
    n_scene = 0;
    for (i = 0; i < n && n_scene < MAXR; i++) {
        scene_r[n_scene].x = rects[i * 4];
        scene_r[n_scene].y = rects[i * 4 + 1];
        scene_r[n_scene].w = rects[i * 4 + 2];
        scene_r[n_scene].h = rects[i * 4 + 3];
        n_scene++;
    }
    if (i < n) scene_full = 1;
    have_scene = 1;
}

void msdl_view_overlay(int x, int y, int w, int h)
{
    int x1, y1;
    if (view_h <= 0) return;
    if (cur_all) {
        dmg_all = 1;
        return;
    }
    x1 = x + w;
    y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > view_w) x1 = view_w;
    if (y1 > view_h) y1 = view_h;
    if (x1 <= x || y1 <= y) return;
    if (n_cur > 0) {
        /* the same rectangle again (a text drawn letter by letter is many
           calls next to each other): grow the last one when it touches */
        VRect *l = &ov_cur[n_cur - 1];
        if (y == l->y && y1 - y == l->h && x <= l->x + l->w && x1 >= l->x) {
            int nx = x < l->x ? x : l->x;
            int nx1 = x1 > l->x + l->w ? x1 : l->x + l->w;
            l->x = nx;
            l->w = nx1 - nx;
            if (n_dmg > 0) {
                VRect *d = &dmg[n_dmg - 1];
                int dx0 = x < d->x ? x : d->x, dx1 = x1 > d->x + d->w ? x1 : d->x + d->w;
                if (d->y == y && d->h == y1 - y) {
                    d->x = dx0;
                    d->w = dx1 - dx0;
                    return;
                }
            }
            if (n_dmg < MAXR) {
                dmg[n_dmg].x = x; dmg[n_dmg].y = y; dmg[n_dmg].w = x1 - x; dmg[n_dmg].h = y1 - y;
                n_dmg++;
            } else {
                dmg_all = 1;
            }
            return;
        }
    }
    if (n_dmg < MAXR) {
        dmg[n_dmg].x = x;
        dmg[n_dmg].y = y;
        dmg[n_dmg].w = x1 - x;
        dmg[n_dmg].h = y1 - y;
        n_dmg++;
    } else {
        dmg_all = 1;
    }
    if (n_cur >= MAXR) {
        cur_all = 1;
        return;
    }
    ov_cur[n_cur].x = x;
    ov_cur[n_cur].y = y;
    ov_cur[n_cur].w = x1 - x;
    ov_cur[n_cur].h = y1 - y;
    n_cur++;
}

void msdl_view_overlay_all(void)
{
    cur_all = 1;
    dmg_all = 1;
}

/* The overlay damage since the last call (the scene copy): rectangles as
   x, y, w, h in out4; -1 when everything must be copied. */
int msdl_view_take_damage(int *out4, int max)
{
    int i, n = n_dmg;
    int all = dmg_all || n > max;
    for (i = 0; !all && i < n; i++) {
        out4[i * 4] = dmg[i].x;
        out4[i * 4 + 1] = dmg[i].y;
        out4[i * 4 + 2] = dmg[i].w;
        out4[i * 4 + 3] = dmg[i].h;
    }
    n_dmg = 0;
    dmg_all = 0;
    return all ? -1 : n;
}

/* ---- the model of the card (verify mode) ---- */
static void model_copy(const Uint8 *pix, int pitch, int x, int y, int w, int h)
{
    int j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > model_w) w = model_w - x;
    if (y + h > model_h) h = model_h - y;
    if (w <= 0 || h <= 0) return;
    for (j = 0; j < h; j++) memcpy(model + (y + j) * model_w + x, pix + (y + j) * pitch + x, (size_t)w);
}

static void model_shift(int dx, int dy)
{
    int y;
    int w = model_w - (dx < 0 ? -dx : dx);
    int dstX = dx < 0 ? -dx : 0, srcX = dx > 0 ? dx : 0;
    if (w <= 0 || dy >= model_h || -dy >= model_h) return;
    if (dy >= 0) {
        for (y = 0; y + dy < model_h; y++) memmove(model + y * model_w + dstX, model + (y + dy) * model_w + srcX, (size_t)w);
    } else {
        for (y = model_h - 1; y + dy >= 0; y--) memmove(model + y * model_w + dstX, model + (y + dy) * model_w + srcX, (size_t)w);
    }
}

static void verify_model(const Uint8 *pix, int pitch)
{
    int i, y, bad = 0, fx = -1, fy = -1, bx0 = model_w, by0 = model_h, bx1 = -1, by1 = -1;
    if (model == NULL || model_w != view_w || model_h != view_h) {
        free(model);
        model_w = view_w;
        model_h = view_h;
        model = (Uint8 *)malloc((size_t)model_w * model_h);
        model_valid = 0;
        if (model == NULL) return;
    }
    if (!model_valid || scene_full || prev_all || cur_all) {
        model_copy(pix, pitch, 0, 0, model_w, model_h);
        model_valid = 1;
        return;
    }
    model_shift(view_dx, view_dy);
    for (i = 0; i < n_scene; i++) model_copy(pix, pitch, scene_r[i].x, scene_r[i].y, scene_r[i].w, scene_r[i].h);
    for (i = 0; i < n_prev; i++)
        model_copy(pix, pitch, ov_prev[i].x - view_dx, ov_prev[i].y - view_dy, ov_prev[i].w, ov_prev[i].h);
    for (i = 0; i < n_cur; i++) model_copy(pix, pitch, ov_cur[i].x, ov_cur[i].y, ov_cur[i].w, ov_cur[i].h);
    v_frames++;
    for (y = 0; y < model_h; y++) {
        const Uint8 *a = model + y * model_w;
        const Uint8 *b = pix + y * pitch;
        int x;
        if (memcmp(a, b, (size_t)model_w) == 0) continue;
        for (x = 0; x < model_w; x++) {
            if (a[x] != b[x]) {
                if (fx < 0) { fx = x; fy = y; }
                if (x < bx0) bx0 = x;
                if (x > bx1) bx1 = x;
                if (y < by0) by0 = y;
                if (y > by1) by1 = y;
                bad++;
            }
        }
    }
    if (bad != 0) {
        v_bad++;
        if (v_bad <= 40 || (v_bad % 100) == 0)
            msdl_logf(SDL_LOG_PRIORITY_INFO,
                      "view: VERIFY frame %lu: %d pixels differ in %d,%d-%d,%d (first %d,%d), camera moved %d,%d, %d scene / %d+%d overlay rects",
                      v_frames, bad, bx0, by0, bx1, by1, fx, fy, view_dx, view_dy, n_scene, n_prev, n_cur);
        model_copy(pix, pitch, 0, 0, model_w, model_h);
    }
    if ((v_frames % 500) == 0)
        msdl_logf(SDL_LOG_PRIORITY_INFO, "view: verify %lu frames, %lu with differences", v_frames, v_bad);
}

/* For the present: can the view be updated incrementally this frame? Then
   shift(dx, dy) moves the view on the card and send() writes the changed
   rectangles; returns the view height handled (0: send everything). */
int msdl_view_upload(void (*shift)(int dx, int dy, int vw, int vh), void (*send)(int x, int y, int w, int h))
{
    int i;
    static int last_valid;
    if (!have_scene || scene_full || prev_all || cur_all || !last_valid) {
        /* the caller sends the whole frame: the card then shows this
           frame, a base for the next one when it has a scene */
        last_valid = have_scene;
        return 0;
    }
    if (view_dx != 0 || view_dy != 0) shift(view_dx, view_dy, view_w, view_h);
    for (i = 0; i < n_scene; i++) send(scene_r[i].x, scene_r[i].y, scene_r[i].w, scene_r[i].h);
    for (i = 0; i < n_prev; i++) send(ov_prev[i].x - view_dx, ov_prev[i].y - view_dy, ov_prev[i].w, ov_prev[i].h);
    for (i = 0; i < n_cur; i++) send(ov_cur[i].x, ov_cur[i].y, ov_cur[i].w, ov_cur[i].h);
    last_valid = 1;
    return view_h;
}

int msdl_view_verify_on(void) { return verify > 0; }

/* Called by present with the finished frame; then the overlay lists roll over. */
void msdl_view_present(const Uint8 *pix, int pitch, int w, int h)
{
    (void)w;
    (void)h;
    if (verify < 0) {
        char buf[8];
        verify = msdl_env("DIABLO_VERIFY_PRESENT", buf, sizeof(buf)) >= 0;
        if (verify) msdl_logf(SDL_LOG_PRIORITY_INFO, "view: DIABLO_VERIFY_PRESENT");
    }
    if (verify) {
        if (have_scene) verify_model(pix, pitch);
        else model_valid = 0;
    }
    if (!have_scene) dmg_all = 1;   /* a menu or other screen drew the frame */
    memcpy(ov_prev, ov_cur, sizeof(VRect) * (size_t)n_cur);
    n_prev = n_cur;
    prev_all = cur_all;
    n_cur = 0;
    cur_all = 0;
    have_scene = 0;
}
