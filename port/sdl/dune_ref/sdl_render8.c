/* Mini-SDL2: the renderer for an 8-bit CLUT screen (the full-screen mode).

   Dune II's graphics are 8-bit with one palette (IBM.PAL), which is the
   screen palette here (MSDL_SetScreenPalette). Pixels are palette indices,
   a frame of 640x480 is 300 KB - a quarter of the 32-bit frame and half of
   the 16-bit one, which is what counts on a Warp, where these loops are
   bound by fast RAM bandwidth.

   Textures, as in the 16-bit renderer:
     OPAQUE  every alpha 255;
     KEYED   alpha 0 or 255: the transparent pixels hold an index `key` that
             no opaque pixel of this texture uses, their own colour is
             `keyidx` (shown by a NONE copy);
     ALPHA   anything else, with an 8-bit alpha plane.
   Colours from outside the palette (32-bit surfaces, other palettes) go to
   the nearest palette entry (inverse table on 15-bit RGB, filled on demand).
   Blending uses tables: for alpha, 15 levels (alpha/16) of 64 KB, each made
   the first time it is needed; ADD has a table for alpha 255.

   VRAM (default; DUNE_NOVRAM turns it off): the window's back buffer and
   every render target of its size (the game's screen texture) are bitmaps
   on the card. Clears and big fills are the card's RectFill, copies from
   VRAM textures or from textures with a VRAM mirror (MSDL_MirrorTexture:
   the terrain cache) are the card's blitter, and the frame goes to the
   screen with one blit. The CPU only writes VRAM (sprites, text, the
   shroud), except for the rare blended pixels. A texture's pixels are px
   with pitch bytes per row; for a VRAM texture px is valid only while it
   is locked (tpx()), and every blit or fill unlocks it (hw_begin()). */
#include <stdio.h>
#include <SDL.h>
#include "msdl_internal.h"
#include "render_names_r8.h"

enum { K_OPAQUE, K_KEYED, K_ALPHA };

struct SDL_Texture {
    int w, h;
    int access;
    Uint8 *px;              /* RAM, or the locked VRAM (see tpx) */
    int pitch;              /* bytes per row of px */
    void *vbm;              /* the texture lives on the card */
    void *mbm;              /* a RAM texture's mirror on the card */
    int mdirty, mx0, my0, mx1, my1;   /* part of the mirror out of date */
    int uses;               /* opaque copies into VRAM (auto mirror) */
    Uint8 *a;
    int a_valid;
    int kind;
    Uint8 key, keyidx;
    SDL_BlendMode blend;
    Uint8 alphamod;
    /* K_KEYED: the opaque runs of every row, each row made on first use
       and dropped when the row is drawn into: rows[y][0] runs, then
       (start, length) pairs */
    Uint16 **rows;
};

/* Copies n bytes: the destination aligned first, then long words (a VRAM
   write costs the same for a byte as for a long, and an unaligned long is
   split into several; the source may be unaligned, it is fast RAM). */
static inline void copy_bytes(Uint8 *d, const Uint8 *s, int n)
{
    while (n > 0 && ((size_t)d & 3) != 0) {
        *d++ = *s++;
        n--;
    }
    while (n >= 16) {
        ((Uint32 *)d)[0] = ((const Uint32 *)s)[0];
        ((Uint32 *)d)[1] = ((const Uint32 *)s)[1];
        ((Uint32 *)d)[2] = ((const Uint32 *)s)[2];
        ((Uint32 *)d)[3] = ((const Uint32 *)s)[3];
        d += 16; s += 16; n -= 16;
    }
    while (n >= 4) {
        *(Uint32 *)d = *(const Uint32 *)s;
        d += 4; s += 4; n -= 4;
    }
    while (n-- > 0) *d++ = *s++;
}

static void spans_free(SDL_Texture *t)
{
    int y;
    if (t->rows == NULL) return;
    for (y = 0; y < t->h; y++) free(t->rows[y]);
    free(t->rows);
    t->rows = NULL;
}

/* Rows y0 .. y0+n-1 were drawn into: their runs are out of date. */
static void spans_rows_free(SDL_Texture *t, int y0, int n)
{
    int y;
    if (t->rows == NULL) return;
    if (y0 < 0) { n += y0; y0 = 0; }
    if (y0 + n > t->h) n = t->h - y0;
    for (y = y0; y < y0 + n; y++) {
        free(t->rows[y]);
        t->rows[y] = NULL;
    }
}

/* The runs of one row of a keyed texture (made when first needed). */
static const Uint16 *span_row(SDL_Texture *t, int y)
{
    const Uint8 *row;
    Uint8 key = t->key;
    int x, n = 0;
    Uint16 *sp;
    if (t->rows == NULL) {
        t->rows = (Uint16 **)calloc(t->h, sizeof(Uint16 *));
        if (t->rows == NULL) return NULL;
    }
    if (t->rows[y] != NULL) return t->rows[y];
    row = t->px + y * t->pitch;
    for (x = 0; x < t->w;) {
        if (row[x] == key) { x++; continue; }
        while (x < t->w && row[x] != key) x++;
        n++;
    }
    sp = (Uint16 *)malloc((1 + 2 * n) * sizeof(Uint16));
    if (sp == NULL) return NULL;
    sp[0] = (Uint16)n;
    n = 1;
    for (x = 0; x < t->w;) {
        int a;
        if (row[x] == key) { x++; continue; }
        a = x;
        while (x < t->w && row[x] != key) x++;
        sp[n++] = (Uint16)a;
        sp[n++] = (Uint16)(x - a);
    }
    t->rows[y] = sp;
    return sp;
}

struct SDL_Renderer {
    SDL_Window *window;
    SDL_Texture *back;
    SDL_Texture *target;
    Uint8 r, g, b, a;
    Uint8 color;            /* palette index of r, g, b */
    SDL_BlendMode draw_blend;
    int logical_w, logical_h;
    SDL_Texture *alias;
    int clear_pending;
    Uint8 clear_color;
    int dirty, dx0, dy0, dx1, dy1;
    /* MSDL_SetRenderOffset / MSDL_SetRenderClip (the terrain cache) */
    int ox, oy;
    int has_clip;
    SDL_Rect uclip;
};

static SDL_Renderer *the_renderer;

/* DUNE_STATS: pixels per RenderCopy path since the last stats line */
unsigned long r8_px_opaque, r8_px_keyed, r8_px_keynone, r8_px_general, r8_copies, r8_updates;

extern void msdl_window_size(SDL_Window *window, int *w, int *h);

/* ---- palette, inverse table, blend tables ---- */
static SDL_Color pal[256];
static Uint32 pal_sum;
static Uint8 inv15[32768];
static Uint8 inv_ok[32768 / 8];
static Uint8 *blend_tab[16];
static Uint8 *add_tab;

Uint8 *blend_level(int lvl);
Uint8 nearest(unsigned r, unsigned g, unsigned b);

void palette_refresh(void)
{
    const SDL_Color *p = msdl_screen_palette();
    Uint32 sum = 0;
    int i;
    for (i = 0; i < 256; i++) sum = sum * 31 + p[i].r * 65536u + p[i].g * 256u + p[i].b;
    if (sum == pal_sum && sum != 0) return;
    memcpy(pal, p, sizeof(pal));
    pal_sum = sum;
    memset(inv_ok, 0, sizeof(inv_ok));
    for (i = 0; i < 16; i++) {
        free(blend_tab[i]);
        blend_tab[i] = NULL;
    }
    free(add_tab);
    add_tab = NULL;
    /* the game's palette: all tables made now, during loading, not at the
       first shadow or track in the game (frames of 0.5-1 s before) */
    if (msdl_screen_palette_set()) {
        unsigned k;
        Uint32 t0 = SDL_GetTicks();
        for (k = 0; k < 32768; k++) {
            inv15[k] = nearest(((k >> 10) << 3) | 4, (((k >> 5) & 31) << 3) | 4, ((k & 31) << 3) | 4);
        }
        memset(inv_ok, 0xFF, sizeof(inv_ok));
        for (i = 1; i < 16; i++) blend_level(i);
        SDL_Log("palette tables: %lu ms", (unsigned long)(SDL_GetTicks() - t0));
    }
}

Uint8 nearest(unsigned r, unsigned g, unsigned b)
{
    int i, best = 0;
    unsigned long bestd = 0xFFFFFFFFul;
    for (i = 0; i < 256; i++) {
        int dr = pal[i].r - (int)r, dg = pal[i].g - (int)g, db = pal[i].b - (int)b;
        unsigned long d = (unsigned long)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
        if (d < bestd) {
            bestd = d;
            best = i;
            if (d == 0) break;
        }
    }
    return (Uint8)best;
}

Uint8 inv(unsigned r, unsigned g, unsigned b)
{
    unsigned k = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    if (!(inv_ok[k >> 3] & (1 << (k & 7)))) {
        inv15[k] = nearest(r, g, b);
        inv_ok[k >> 3] |= (Uint8)(1 << (k & 7));
    }
    return inv15[k];
}

Uint8 *blend_level(int lvl)
{
    if (blend_tab[lvl] == NULL) {
        Uint8 *t = (Uint8 *)malloc(65536);
        int s, d;
        if (t == NULL) return NULL;
        for (s = 0; s < 256; s++) {
            for (d = 0; d < 256; d++) {
                t[(s << 8) | d] = inv((pal[s].r * lvl + pal[d].r * (16 - lvl) + 8) >> 4,
                                      (pal[s].g * lvl + pal[d].g * (16 - lvl) + 8) >> 4,
                                      (pal[s].b * lvl + pal[d].b * (16 - lvl) + 8) >> 4);
            }
        }
        blend_tab[lvl] = t;
    }
    return blend_tab[lvl];
}

Uint8 blend8(Uint8 s, Uint8 d, unsigned a, SDL_BlendMode mode)
{
    switch (mode) {
        case SDL_BLENDMODE_BLEND: {
            int lvl = (int)((a * 16 + 127) / 255);
            Uint8 *t;
            if (lvl >= 16) return s;
            if (lvl <= 0) return d;
            t = blend_level(lvl);
            return t != NULL ? t[(s << 8) | d] : s;
        }
        case SDL_BLENDMODE_ADD: {
            unsigned r, g, b;
            if (a == 255) {
                if (add_tab == NULL && (add_tab = (Uint8 *)malloc(65536)) != NULL) {
                    int i, j;
                    for (i = 0; i < 256; i++) {
                        for (j = 0; j < 256; j++) {
                            r = pal[i].r + pal[j].r; g = pal[i].g + pal[j].g; b = pal[i].b + pal[j].b;
                            add_tab[(i << 8) | j] = inv(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
                        }
                    }
                }
                if (add_tab != NULL) return add_tab[(s << 8) | d];
            }
            r = pal[d].r + ((pal[s].r * a) >> 8);
            g = pal[d].g + ((pal[s].g * a) >> 8);
            b = pal[d].b + ((pal[s].b * a) >> 8);
            return inv(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
        }
        case SDL_BLENDMODE_MOD:
            return inv(pal[s].r * pal[d].r / 255, pal[s].g * pal[d].g / 255, pal[s].b * pal[d].b / 255);
        default:
            return s;
    }
}

/* ---- textures ---- */
void back_materialize(SDL_Renderer *r);
SDL_Texture *texture_new(int w, int h, int access)
{
    SDL_Texture *t;
    if (w <= 0 || h <= 0) {
        SDL_SetError("texture size %dx%d", w, h);
        return NULL;
    }
    palette_refresh();
    t = (SDL_Texture *)calloc(1, sizeof(SDL_Texture));
    if (t == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    t->w = w;
    t->h = h;
    t->access = access;
    t->px = (Uint8 *)calloc((size_t)w * h, 1);
    if (t->px == NULL) {
        free(t);
        SDL_SetError("out of memory");
        return NULL;
    }
    t->pitch = w;
    t->blend = SDL_BLENDMODE_NONE;
    t->alphamod = 255;
    t->kind = K_OPAQUE;
    return t;
}

/* ---- VRAM ---- */
static int vram_mode = -1;
#define AUTO_MIRROR_MIN 4096            /* pixels */
#define AUTO_MIRROR_MAX (16ul << 20)    /* bytes of VRAM for the copies */
static unsigned long mirrored_bytes;
int SDL_MirrorTextureX(SDL_Texture *t);

static int vram_enabled(void)
{
    if (vram_mode < 0) vram_mode = !MSDL_EnvSet("DUNE_NOVRAM");
    return vram_mode;
}

/* The pixels of a texture for the CPU. A VRAM texture is locked (the lock
   stays between drawing calls - a lock/unlock pair costs ~29 us, Settlers
   0.18 - and is taken off by any blit, fill, show, palette change,
   allocation, event or delay: with a bitmap locked no system call may draw
   or allocate on the card, Settlers 0.15/0.18 deadlocked on that). */
static Uint8 *tpx(SDL_Texture *t)
{
    if (t->vbm != NULL) {
        int bpr = 0;
        t->px = msdl_vram_lock(t->vbm, &bpr);
        t->pitch = bpr;
    }
    return t->px;
}

/* Before the blitter or RectFill: nothing stays locked. */
static void hw_begin(void)
{
    msdl_vram_unlock();
}

/* A texture on the card (opaque black); NULL if there is no room. */
static SDL_Texture *texture_new_vram(int w, int h, int access)
{
    SDL_Texture *t;
    void *bm;
    if (!vram_enabled()) return NULL;
    bm = msdl_vram_alloc(w, h);
    if (bm == NULL) return NULL;
    palette_refresh();
    t = (SDL_Texture *)calloc(1, sizeof(SDL_Texture));
    if (t == NULL) {
        msdl_vram_free(bm);
        return NULL;
    }
    t->w = w;
    t->h = h;
    t->access = access;
    t->vbm = bm;
    t->blend = SDL_BLENDMODE_NONE;
    t->alphamod = 255;
    t->kind = K_OPAQUE;
    hw_begin();
    msdl_vram_fill(bm, 0, 0, w, h, 0);
    return t;
}

static void mirror_mark(SDL_Texture *t, const SDL_Rect *r)
{
    int x1 = r->x + r->w, y1 = r->y + r->h;
    if (t->mbm == NULL || r->w <= 0 || r->h <= 0) return;
    if (!t->mdirty) {
        t->mdirty = 1;
        t->mx0 = r->x; t->my0 = r->y; t->mx1 = x1; t->my1 = y1;
        return;
    }
    if (r->x < t->mx0) t->mx0 = r->x;
    if (r->y < t->my0) t->my0 = r->y;
    if (x1 > t->mx1) t->mx1 = x1;
    if (y1 > t->my1) t->my1 = y1;
}

static void mirror_all(SDL_Texture *t)
{
    SDL_Rect r;
    r.x = r.y = 0;
    r.w = t->w;
    r.h = t->h;
    mirror_mark(t, &r);
}

/* The mirror brought up to date (the changed rectangle written to the
   card in long words); returns it. */
static void *mirror_sync(SDL_Texture *t)
{
    if (t->mdirty) {
        int bpr = 0, y;
        Uint8 *d;
        hw_begin();
        d = msdl_vram_lock(t->mbm, &bpr);
        if (d != NULL) {
            int x0 = t->mx0 & ~3, x1 = (t->mx1 + 3) & ~3;
            if (x1 > t->w) x1 = t->w;
            for (y = t->my0; y < t->my1; y++) {
                copy_bytes(d + y * bpr + x0, t->px + y * t->pitch + x0, x1 - x0);
            }
        }
        msdl_vram_unlock();
        t->mdirty = 0;
    }
    return t->mbm;
}

Uint8 *alpha_plane(SDL_Texture *t)
{
    spans_free(t);
    size_t i, n = (size_t)t->w * t->h;
    if (t->a == NULL) {
        t->a = (Uint8 *)malloc(n);
        if (t->a == NULL) return NULL;
        t->a_valid = 0;
    }
    if (!t->a_valid || t->kind != K_ALPHA) {
        if (t->kind == K_KEYED) {
            for (i = 0; i < n; i++) {
                if (t->px[i] == t->key) {
                    t->a[i] = 0;
                    t->px[i] = t->keyidx;
                } else {
                    t->a[i] = 255;
                }
            }
        } else if (!t->a_valid) {
            memset(t->a, 255, n);
        }
        t->kind = K_ALPHA;
        t->a_valid = 1;
    }
    return t->a;
}

/* From indices (already in the screen palette) and their alpha: works out
   the kind. alpha may be NULL (all opaque). */
void load_indexed(SDL_Texture *t, const Uint8 *idx, const Uint8 *alpha)
{
    spans_free(t);
    size_t i, n = (size_t)t->w * t->h;
    if (t->vbm != NULL) {
        /* on the card: opaque or with an alpha plane, rows written */
        int y, opaque = 1;
        Uint8 *d = tpx(t);
        for (i = 0; alpha != NULL && i < n; i++) if (alpha[i] != 255) { opaque = 0; break; }
        if (d != NULL) for (y = 0; y < t->h; y++) copy_bytes(d + y * t->pitch, idx + y * t->w, t->w);
        if (opaque) {
            t->kind = K_OPAQUE;
            t->a_valid = 0;
        } else {
            t->kind = K_ALPHA;
            if (t->a == NULL) t->a = (Uint8 *)malloc(n);
            t->a_valid = t->a != NULL;
            if (t->a != NULL) memcpy(t->a, alpha, n);
        }
        return;
    }
    mirror_all(t);
    Uint8 used[256];
    int keyed = 1, opaque = 1, have_key = 0, k;
    Uint8 kidx = 0;
    memset(used, 0, sizeof(used));
    for (i = 0; i < n; i++) {
        unsigned a = alpha != NULL ? alpha[i] : 255;
        if (a == 255) {
            used[idx[i]] = 1;
            continue;
        }
        opaque = 0;
        if (a != 0) keyed = 0;
        else if (!have_key) { have_key = 1; kidx = idx[i]; }
        else if (idx[i] != kidx) keyed = 0;
    }
    if (opaque) {
        t->kind = K_OPAQUE;
        memcpy(t->px, idx, n);
        return;
    }
    if (keyed) {
        for (k = 255; k >= 0 && used[k]; k--) ;
        if (k >= 0) {
            t->kind = K_KEYED;
            t->key = (Uint8)k;
            t->keyidx = kidx;
            for (i = 0; i < n; i++) t->px[i] = alpha[i] ? idx[i] : (Uint8)k;
            return;
        }
    }
    t->kind = K_ALPHA;
    if (t->a == NULL) t->a = (Uint8 *)malloc(n);
    t->a_valid = t->a != NULL;
    memcpy(t->px, idx, n);
    if (t->a != NULL) memcpy(t->a, alpha, n);
}

/* ABGR8888 pixels -> indices + alpha -> texture. */
int load_abgr(SDL_Texture *t, const Uint32 *src, int pitch_px)
{
    /* the radar is updated every frame: an opaque RAM texture getting opaque
       pixels again is written in place */
    if (t->kind == K_OPAQUE && t->vbm == NULL && t->rows == NULL) {
        int x, y, ok = 1;
        for (y = 0; y < t->h && ok; y++) {
            const Uint32 *sr = src + y * pitch_px;
            for (x = 0; x < t->w; x++) if ((sr[x] >> 24) != 255) { ok = 0; break; }
        }
        if (ok) {
            Uint32 last = 0xFFFFFFFFu;
            Uint8 last_i = 0;
            for (y = 0; y < t->h; y++) {
                const Uint32 *sr = src + y * pitch_px;
                Uint8 *d = t->px + y * t->pitch;
                for (x = 0; x < t->w; x++) {
                    Uint32 p = sr[x] & 0x00FFFFFFu;
                    if (p != last) {
                        last = p;
                        last_i = inv15[((p & 0xF8) << 7) | ((p >> 6) & 0x3E0) | ((p >> 19) & 0x1F)];
                        if (!(inv_ok[(((p & 0xF8) << 7) | ((p >> 6) & 0x3E0) | ((p >> 19) & 0x1F)) >> 3] &
                              (1 << ((((p & 0xF8) << 7) | ((p >> 6) & 0x3E0) | ((p >> 19) & 0x1F)) & 7))))
                            last_i = inv(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
                    }
                    d[x] = last_i;
                }
            }
            mirror_all(t);
            return 0;
        }
    }
    static Uint8 *idx, *al;
    static size_t cap;
    size_t n = (size_t)t->w * t->h;
    int x, y;
    Uint32 last = 0xFFFFFFFFu;
    Uint8 last_i = 0;
    if (cap < n) {
        free(idx);
        free(al);
        idx = (Uint8 *)malloc(n);
        al = (Uint8 *)malloc(n);
        cap = (idx != NULL && al != NULL) ? n : 0;
        if (cap == 0) return SDL_SetError("out of memory");
    }
    for (y = 0; y < t->h; y++) {
        const Uint32 *s = src + y * pitch_px;
        Uint8 *di = idx + y * t->w, *da = al + y * t->w;
        for (x = 0; x < t->w; x++) {
            Uint32 p = s[x];
            if ((p & 0x00FFFFFFu) != last) {
                last = p & 0x00FFFFFFu;
                last_i = inv(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
            }
            di[x] = last_i;
            da[x] = (Uint8)(p >> 24);
        }
    }
    load_indexed(t, idx, al);
    return 0;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
    SDL_Texture *t;
    (void)renderer;
    if (format != SDL_PIXELFORMAT_ABGR8888) {
        SDL_SetError("SDL_CreateTexture(): only ABGR8888");
        return NULL;
    }
    t = NULL;
    if (access == SDL_TEXTUREACCESS_TARGET && the_renderer != NULL && the_renderer->back->vbm != NULL &&
        w == the_renderer->back->w && h == the_renderer->back->h) {
        t = texture_new_vram(w, h, access);   /* the game's screen texture */
    }
    if (t == NULL) t = texture_new(w, h, access);
    if (t != NULL && alpha_plane(t) != NULL) memset(t->a, 0, (size_t)w * h);
    return t;
}

/* Index map of a surface palette onto the screen palette (identity when
   they are the same colours). */
void map_palette(const SDL_Palette *sp, Uint8 *map)
{
    int i;
    for (i = 0; i < 256; i++) {
        if (sp == NULL || i >= sp->ncolors) {
            map[i] = 0;
        } else {
            const SDL_Color *c = &sp->colors[i];
            if (c->r == pal[i].r && c->g == pal[i].g && c->b == pal[i].b) map[i] = (Uint8)i;
            else map[i] = inv(c->r, c->g, c->b);
        }
    }
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
    SDL_Texture *t;
    int x, y;
    (void)renderer;
    if (surface == NULL) {
        SDL_SetError("no surface");
        return NULL;
    }
    t = texture_new(surface->w, surface->h, SDL_TEXTUREACCESS_STATIC);
    if (t == NULL) return NULL;

    if (surface->format->BytesPerPixel == 1) {
        size_t n = (size_t)surface->w * surface->h;
        Uint8 map[256], amap[256];
        Uint8 *idx = (Uint8 *)malloc(n), *al = (Uint8 *)malloc(n);
        const SDL_Palette *sp = surface->format->palette;
        if (idx == NULL || al == NULL) {
            free(idx);
            free(al);
            SDL_DestroyTexture(t);
            SDL_SetError("out of memory");
            return NULL;
        }
        map_palette(sp, map);
        for (x = 0; x < 256; x++) amap[x] = (sp != NULL && x < sp->ncolors) ? sp->colors[x].a : 255;
        if (surface->has_colorkey) amap[surface->colorkey & 0xFF] = 0;
        for (y = 0; y < surface->h; y++) {
            const Uint8 *s = (const Uint8 *)surface->pixels + y * surface->pitch;
            for (x = 0; x < surface->w; x++) {
                idx[y * surface->w + x] = map[s[x]];
                al[y * surface->w + x] = amap[s[x]];
            }
        }
        load_indexed(t, idx, al);
        free(idx);
        free(al);
    } else {
        int has_alpha = surface->format->Amask != 0;
        Uint32 *tmp = (Uint32 *)malloc((size_t)surface->w * surface->h * 4);
        if (tmp == NULL) {
            SDL_DestroyTexture(t);
            SDL_SetError("out of memory");
            return NULL;
        }
        for (y = 0; y < surface->h; y++) {
            const Uint32 *s = (const Uint32 *)((const Uint8 *)surface->pixels + y * surface->pitch);
            for (x = 0; x < surface->w; x++) {
                Uint32 p = s[x];
                if (!has_alpha) p |= 0xFF000000u;
                if (surface->has_colorkey && (p & 0x00FFFFFFu) == (surface->colorkey & 0x00FFFFFFu)) p &= 0x00FFFFFFu;
                tmp[y * surface->w + x] = p;
            }
        }
        load_abgr(t, tmp, surface->w);
        free(tmp);
    }
    t->alphamod = surface->alphamod;
    t->blend = surface->has_colorkey ? SDL_BLENDMODE_BLEND : surface->blendmode;
    return t;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture == NULL) return;
    if (the_renderer != NULL) {
        if (the_renderer->alias == texture) back_materialize(the_renderer);
        if (the_renderer->target == texture) the_renderer->target = NULL;
    }
    spans_free(texture);
    if (texture->mbm != NULL) {
        unsigned long n = (unsigned long)texture->w * texture->h;
        mirrored_bytes = mirrored_bytes > n ? mirrored_bytes - n : 0;
    }
    if (texture->vbm != NULL || texture->mbm != NULL) {
        hw_begin();
        msdl_vram_free(texture->vbm);
        msdl_vram_free(texture->mbm);
    }
    if (texture->vbm == NULL) free(texture->px);
    free(texture->a);
    free(texture);
}

int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h)
{
    if (texture == NULL) return SDL_SetError("no texture");
    if (format != NULL) *format = SDL_PIXELFORMAT_ABGR8888;
    if (access != NULL) *access = texture->access;
    if (w != NULL) *w = texture->w;
    if (h != NULL) *h = texture->h;
    return 0;
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    SDL_Rect r;
    int x, y, opaque = 1;
    if (texture == NULL) return SDL_SetError("no texture");
    r8_updates++;
    if (the_renderer != NULL && the_renderer->alias == texture) back_materialize(the_renderer);
    if (rect == NULL) {
        r.x = r.y = 0;
        r.w = texture->w;
        r.h = texture->h;
    } else {
        r = *rect;
    }
    if (r.x == 0 && r.y == 0 && r.w == texture->w && r.h == texture->h && pitch % 4 == 0) {
        return load_abgr(texture, (const Uint32 *)pixels, pitch / 4);
    }
    for (y = 0; y < r.h && opaque; y++) {
        const Uint32 *s = (const Uint32 *)((const Uint8 *)pixels + y * pitch);
        for (x = 0; x < r.w; x++) {
            if ((s[x] >> 24) != 255) {
                opaque = 0;
                break;
            }
        }
    }
    if (!opaque || texture->kind != K_OPAQUE) alpha_plane(texture);
    mirror_mark(texture, &r);
    if (tpx(texture) == NULL) return SDL_SetError("cannot lock the texture");
    for (y = 0; y < r.h; y++) {
        const Uint32 *s = (const Uint32 *)((const Uint8 *)pixels + y * pitch);
        Uint8 *d = texture->px + (r.y + y) * texture->pitch + r.x;
        for (x = 0; x < r.w; x++) d[x] = inv(s[x] & 0xFF, (s[x] >> 8) & 0xFF, (s[x] >> 16) & 0xFF);
        if (texture->kind == K_ALPHA && texture->a != NULL) {
            Uint8 *a = texture->a + (r.y + y) * texture->w + r.x;
            for (x = 0; x < r.w; x++) a[x] = (Uint8)(s[x] >> 24);
        }
    }
    return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
    (void)texture; (void)rect; (void)pixels; (void)pitch;
    return SDL_SetError("SDL_LockTexture() not supported");
}

void SDL_UnlockTexture(SDL_Texture *texture) { (void)texture; }

int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode)
{
    if (texture == NULL) return SDL_SetError("no texture");
    texture->blend = blendMode;
    return 0;
}

int SDL_SetTextureAlphaMod(SDL_Texture *texture, Uint8 alpha)
{
    if (texture == NULL) return SDL_SetError("no texture");
    texture->alphamod = alpha;
    return 0;
}

int SDL_SetTextureColorMod(SDL_Texture *texture, Uint8 r, Uint8 g, Uint8 b)
{
    (void)texture; (void)r; (void)g; (void)b;
    return 0;
}

/* ---- renderer ---- */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    SDL_Renderer *r;
    int w, h;
    (void)index;
    (void)flags;
    palette_refresh();
    r = (SDL_Renderer *)calloc(1, sizeof(SDL_Renderer));
    if (r == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    msdl_window_size(window, &w, &h);
    r->window = window;
    r->logical_w = w;
    r->logical_h = h;
    r->back = texture_new_vram(w, h, SDL_TEXTUREACCESS_TARGET);
    if (r->back == NULL) r->back = texture_new(w, h, SDL_TEXTUREACCESS_TARGET);
    if (r->back == NULL) {
        free(r);
        return NULL;
    }
    SDL_Log("renderer: frames composed in %s", r->back->vbm != NULL ? "VRAM" : "fast RAM");
    r->a = 255;
    r->dirty = 1;
    r->dx1 = w;
    r->dy1 = h;
    the_renderer = r;
    return r;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (renderer == NULL) return;
    SDL_DestroyTexture(renderer->back);
    if (renderer == the_renderer) the_renderer = NULL;
    free(renderer);
}

int SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info)
{
    (void)renderer;
    memset(info, 0, sizeof(*info));
    info->name = "warp-clut8";
    info->flags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;
    info->num_texture_formats = 1;
    info->texture_formats[0] = SDL_PIXELFORMAT_ABGR8888;
    info->max_texture_width = 4096;
    info->max_texture_height = 4096;
    return 0;
}

int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    msdl_window_size(renderer->window, w, h);
    return 0;
}

int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
    if (renderer->back->w != w || renderer->back->h != h) {
        SDL_Texture *nb = texture_new_vram(w, h, SDL_TEXTUREACCESS_TARGET);
        if (nb == NULL) nb = texture_new(w, h, SDL_TEXTUREACCESS_TARGET);
        if (nb == NULL) return -1;
        if (renderer->target == renderer->back) renderer->target = NULL;
        renderer->alias = NULL;
        SDL_DestroyTexture(renderer->back);
        renderer->back = nb;
    }
    renderer->logical_w = w;
    renderer->logical_h = h;
    renderer->dirty = 1;
    renderer->dx0 = renderer->dy0 = 0;
    renderer->dx1 = w;
    renderer->dy1 = h;
    return 0;
}

void SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h)
{
    if (w != NULL) *w = renderer->logical_w;
    if (h != NULL) *h = renderer->logical_h;
}

int SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    renderer->target = texture;
    return 0;
}

SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *renderer) { return renderer->target; }

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    renderer->color = inv(r, g, b);
    renderer->r = r;
    renderer->g = g;
    renderer->b = b;
    renderer->a = a;
    return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    renderer->draw_blend = blendMode;
    return 0;
}

void back_materialize(SDL_Renderer *r)
{
    SDL_Texture *b = r->back;
    if (r->alias != NULL) {
        SDL_Texture *a = r->alias;
        r->alias = NULL;
        if (a->vbm != NULL && b->vbm != NULL) {
            hw_begin();
            msdl_vram_blit(a->vbm, 0, 0, b->vbm, 0, 0, b->w, b->h);
        } else {
            /* at most one of them is on the card: only that one is locked */
            const Uint8 *s = tpx(a);
            Uint8 *d = tpx(b);
            int y;
            if (s != NULL && d != NULL) {
                for (y = 0; y < b->h; y++) copy_bytes(d + y * b->pitch, s + y * a->pitch, b->w);
            }
        }
    } else if (r->clear_pending) {
        if (b->vbm != NULL) {
            hw_begin();
            msdl_vram_fill(b->vbm, 0, 0, b->w, b->h, r->clear_color);
        } else {
            memset(b->px, r->clear_color, (size_t)b->w * b->h);
        }
    }
    r->clear_pending = 0;
}

SDL_Texture *cur_target(SDL_Renderer *r)
{
    if (r->target == NULL) {
        back_materialize(r);
        return r->back;
    }
    if (r->alias == r->target) back_materialize(r);
    return r->target;
}

void mark(SDL_Renderer *renderer, const SDL_Rect *r)
{
    int x1, y1;
    if (renderer->target != NULL) return;
    x1 = r->x + r->w;
    y1 = r->y + r->h;
    if (!renderer->dirty) {
        renderer->dirty = 1;
        renderer->dx0 = r->x;
        renderer->dy0 = r->y;
        renderer->dx1 = x1;
        renderer->dy1 = y1;
        return;
    }
    if (r->x < renderer->dx0) renderer->dx0 = r->x;
    if (r->y < renderer->dy0) renderer->dy0 = r->y;
    if (x1 > renderer->dx1) renderer->dx1 = x1;
    if (y1 > renderer->dy1) renderer->dy1 = y1;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    SDL_Texture *t;
    SDL_Rect all;
    if (renderer->target == NULL) {
        renderer->alias = NULL;
        renderer->clear_pending = 1;
        renderer->clear_color = renderer->color;
        all.x = all.y = 0;
        all.w = renderer->back->w;
        all.h = renderer->back->h;
        mark(renderer, &all);
        return 0;
    }
    t = cur_target(renderer);
    if (t->vbm != NULL) {
        hw_begin();
        msdl_vram_fill(t->vbm, 0, 0, t->w, t->h, renderer->color);
    } else {
        memset(t->px, renderer->color, (size_t)t->w * t->h);
        mirror_all(t);
    }
    t->kind = K_OPAQUE;
    t->a_valid = 0;
    if (renderer->a != 255) {
        Uint8 *a = alpha_plane(t);
        if (a != NULL) memset(a, renderer->a, (size_t)t->w * t->h);
    }
    return 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    SDL_Texture *t;
    SDL_Rect sr, dr, clip, cd;
    int x, y, kind, plain;
    Uint32 xstep, ystep, sx0, sy;
    unsigned amod;
    SDL_BlendMode mode;
    Uint8 *da;
    static Uint8 *tmp;
    static size_t tmp_n;
    const Uint8 *spx;
    Uint8 *dpx;
    int spitch, dpitch;

    if (texture == NULL) return SDL_SetError("no texture");
    if (srcrect != NULL) {
        SDL_Rect full;
        full.x = full.y = 0;
        full.w = texture->w;
        full.h = texture->h;
        if (!SDL_IntersectRect(srcrect, &full, &sr)) return 0;
    } else {
        sr.x = sr.y = 0;
        sr.w = texture->w;
        sr.h = texture->h;
    }

    if (renderer->target == NULL && dstrect == NULL && srcrect == NULL && !renderer->has_clip &&
        renderer->ox == 0 && renderer->oy == 0 &&
        texture->w == renderer->back->w && texture->h == renderer->back->h &&
        (texture->blend == SDL_BLENDMODE_NONE || texture->kind == K_OPAQUE) && texture->alphamod == 255 &&
        texture != renderer->back) {
        renderer->alias = texture;
        renderer->clear_pending = 0;
        clip.x = clip.y = 0;
        clip.w = texture->w;
        clip.h = texture->h;
        mark(renderer, &clip);
        return 0;
    }

    t = cur_target(renderer);
    if (dstrect != NULL) {
        dr = *dstrect;
    } else {
        dr.x = dr.y = 0;
        dr.w = t->w;
        dr.h = t->h;
    }
    if (dr.w <= 0 || dr.h <= 0) return 0;
    dr.x += renderer->ox;
    dr.y += renderer->oy;
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (renderer->has_clip && !SDL_IntersectRect(&clip, &renderer->uclip, &clip)) return 0;
    if (!SDL_IntersectRect(&dr, &clip, &cd)) return 0;

    xstep = ((Uint32)sr.w << 16) / (Uint32)dr.w;
    ystep = ((Uint32)sr.h << 16) / (Uint32)dr.h;
    sx0 = (Uint32)(cd.x - dr.x) * xstep;
    sy = (Uint32)(cd.y - dr.y) * ystep;
    amod = texture->alphamod;
    mode = texture->blend;
    kind = texture->kind;

    /* keyed into a keyed target with the same key (the shroud cache):
       the pixels as they are, transparency included */
    if (t->kind == K_KEYED && kind == K_KEYED && texture->key == t->key && mode == SDL_BLENDMODE_NONE &&
        texture != t && t->vbm == NULL && texture->vbm == NULL &&
        xstep == 0x10000u && ystep == 0x10000u && amod == 255) {
        const Uint8 *s = texture->px + (sr.y + (sy >> 16)) * texture->pitch + sr.x + (sx0 >> 16);
        Uint8 *d = t->px + cd.y * t->pitch + cd.x;
        for (y = 0; y < cd.h; y++, s += texture->pitch, d += t->pitch) copy_bytes(d, s, cd.w);
        spans_rows_free(t, cd.y, cd.h);
        mirror_mark(t, &cd);
        r8_px_keynone += (unsigned long)cd.w * cd.h;
        r8_copies++;
        return 0;
    }

    da = (t->kind == K_ALPHA && t->a != NULL && t->a_valid) ? t->a : NULL;
    if (mode == SDL_BLENDMODE_NONE && kind != K_OPAQUE && da == NULL && t != renderer->back) da = alpha_plane(t);

    plain = xstep == 0x10000u && ystep == 0x10000u && amod == 255 && da == NULL;
    mirror_mark(t, &cd);
    spans_rows_free(t, cd.y, cd.h);

    /* big static opaque images (panel, top bar, menu backgrounds) copied
       into the frame again and again get a copy on the card on their second
       use, so that the blitter does the copy */
    if (plain && t->vbm != NULL && texture != t && kind == K_OPAQUE && texture->vbm == NULL &&
        texture->mbm == NULL && texture->access != SDL_TEXTUREACCESS_STREAMING &&
        (mode == SDL_BLENDMODE_NONE || mode == SDL_BLENDMODE_BLEND) &&
        texture->w * texture->h >= AUTO_MIRROR_MIN && ++texture->uses >= 2 &&
        mirrored_bytes + (unsigned long)texture->w * texture->h <= AUTO_MIRROR_MAX) {
        if (SDL_MirrorTextureX(texture)) mirrored_bytes += (unsigned long)texture->w * texture->h;
    }

    /* opaque copies between card bitmaps: the blitter */
    if (plain && t->vbm != NULL && texture != t && kind == K_OPAQUE &&
        (mode == SDL_BLENDMODE_NONE || mode == SDL_BLENDMODE_BLEND) &&
        (texture->vbm != NULL || texture->mbm != NULL)) {
        void *sbm = texture->vbm != NULL ? texture->vbm : mirror_sync(texture);
        hw_begin();
        msdl_vram_blit(sbm, sr.x + (sx0 >> 16), sr.y + (sy >> 16), t->vbm, cd.x, cd.y, cd.w, cd.h);
        r8_px_opaque += (unsigned long)cd.w * cd.h;
        r8_copies++;
        mark(renderer, &cd);
        return 0;
    }

    if (texture == t || (texture->vbm != NULL && t->vbm != NULL)) {
        /* onto itself, or card to card with the CPU: from a RAM copy */
        size_t n = (size_t)texture->w * texture->h;
        const Uint8 *s;
        if (tmp_n < n) {
            free(tmp);
            tmp = (Uint8 *)malloc(n);
            tmp_n = tmp != NULL ? n : 0;
            if (tmp == NULL) return SDL_SetError("out of memory");
        }
        s = tpx(texture);
        if (s == NULL) return SDL_SetError("cannot lock the texture");
        for (y = 0; y < texture->h; y++) memcpy(tmp + y * texture->w, s + y * texture->pitch, texture->w);
        spx = tmp;
        spitch = texture->w;
    } else {
        spx = tpx(texture);
        spitch = texture->pitch;
    }
    dpx = tpx(t);
    if (spx == NULL || dpx == NULL) return SDL_SetError("cannot lock the texture");
    dpitch = t->pitch;
    if (plain && kind == K_OPAQUE && (mode == SDL_BLENDMODE_NONE || mode == SDL_BLENDMODE_BLEND)) {
        const Uint8 *s = spx + (sr.y + (sy >> 16)) * spitch + sr.x + (sx0 >> 16);
        Uint8 *d = dpx + cd.y * dpitch + cd.x;
        for (y = 0; y < cd.h; y++, s += spitch, d += dpitch) copy_bytes(d, s, cd.w);
        r8_px_opaque += (unsigned long)cd.w * cd.h;
        r8_copies++;
        mark(renderer, &cd);
        return 0;
    }
    if (plain && kind == K_KEYED && mode == SDL_BLENDMODE_BLEND && spx == texture->px && texture->vbm == NULL &&
        span_row(texture, sr.y + (sy >> 16)) != NULL) {
        int sx = sr.x + (sx0 >> 16), sx1 = sx + cd.w, row = sr.y + (sy >> 16);
        Uint8 *d = dpx + cd.y * dpitch + cd.x - sx;     /* indexed by source x */
        for (y = 0; y < cd.h; y++, row++, d += dpitch) {
            const Uint16 *sp = span_row(texture, row);
            const Uint8 *s = spx + row * spitch;
            int k, cnt;
            if (sp == NULL) continue;
            cnt = *sp++;
            for (k = 0; k < cnt; k++, sp += 2) {
                int a = sp[0], b = a + sp[1];
                if (b <= sx) continue;
                if (a >= sx1) break;
                if (a < sx) a = sx;
                if (b > sx1) b = sx1;
                copy_bytes(d + a, s + a, b - a);
            }
        }
        r8_px_keyed += (unsigned long)cd.w * cd.h;
        r8_copies++;
        mark(renderer, &cd);
        return 0;
    }
    if (plain && kind == K_KEYED && mode == SDL_BLENDMODE_BLEND) {
        const Uint8 *s = spx + (sr.y + (sy >> 16)) * spitch + sr.x + (sx0 >> 16);
        Uint8 *d = dpx + cd.y * dpitch + cd.x;
        Uint8 key = texture->key;
        for (y = 0; y < cd.h; y++, s += spitch, d += dpitch) {
            for (x = 0; x < cd.w; x++) {
                Uint8 p = s[x];
                if (p != key) d[x] = p;
            }
        }
        r8_px_keyed += (unsigned long)cd.w * cd.h;
        r8_copies++;
        mark(renderer, &cd);
        return 0;
    }
    if (plain && kind == K_KEYED && mode == SDL_BLENDMODE_NONE) {
        const Uint8 *s = spx + (sr.y + (sy >> 16)) * spitch + sr.x + (sx0 >> 16);
        Uint8 *d = dpx + cd.y * dpitch + cd.x;
        Uint8 key = texture->key, ki = texture->keyidx;
        for (y = 0; y < cd.h; y++, s += spitch, d += dpitch) {
            for (x = 0; x < cd.w; x++) {
                Uint8 p = s[x];
                d[x] = p == key ? ki : p;
            }
        }
        r8_px_keynone += (unsigned long)cd.w * cd.h;
        r8_copies++;
        mark(renderer, &cd);
        return 0;
    }

    /* general path */
    r8_px_general += (unsigned long)cd.w * cd.h;
    r8_copies++;
    {
        const Uint8 *sa = (kind == K_ALPHA) ? texture->a : NULL;
        Uint8 key = texture->key, ki = texture->keyidx;
        for (y = 0; y < cd.h; y++, sy += ystep) {
            int srow = (sr.y + (sy >> 16)) * spitch + sr.x;
            int arow = (sr.y + (sy >> 16)) * texture->w + sr.x;
            Uint8 *d = dpx + (cd.y + y) * dpitch + cd.x;
            Uint8 *dal = da != NULL ? da + (cd.y + y) * t->w + cd.x : NULL;
            Uint32 sx = sx0;
            for (x = 0; x < cd.w; x++, sx += xstep) {
                int si = srow + (sx >> 16);
                int ai = arow + (sx >> 16);
                Uint8 s = spx[si];
                unsigned a;
                if (kind == K_KEYED) {
                    if (s == key) {
                        a = 0;
                        s = ki;
                    } else {
                        a = 255;
                    }
                } else if (sa != NULL) {
                    a = sa[ai];
                } else {
                    a = 255;
                }
                if (amod != 255) a = (a * amod + 127) / 255;
                switch (mode) {
                    case SDL_BLENDMODE_NONE:
                        d[x] = s;
                        if (dal != NULL) dal[x] = (Uint8)a;
                        break;
                    case SDL_BLENDMODE_BLEND:
                        if (a == 0) break;
                        d[x] = blend8(s, d[x], a, mode);
                        if (dal != NULL) dal[x] = (Uint8)(a + ((dal[x] * (255 - a) * 257 + 32768) >> 16));
                        break;
                    default:
                        d[x] = blend8(s, d[x], a, mode);
                        break;
                }
            }
        }
    }
    mark(renderer, &cd);
    return 0;
}

void fill(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    SDL_Texture *t = cur_target(renderer);
    SDL_Rect clip, r;
    int x, y;
    Uint8 c = renderer->color;
    unsigned a = renderer->a;
    SDL_Rect moved = *rect;
    moved.x += renderer->ox;
    moved.y += renderer->oy;
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (renderer->has_clip && !SDL_IntersectRect(&clip, &renderer->uclip, &clip)) return;
    if (!SDL_IntersectRect(&moved, &clip, &r)) return;
    mirror_mark(t, &r);
    spans_rows_free(t, r.y, r.h);
    if (t->kind == K_KEYED && renderer->draw_blend == SDL_BLENDMODE_NONE && a == 0 && t->vbm == NULL) {
        for (y = 0; y < r.h; y++) memset(t->px + (r.y + y) * t->pitch + r.x, t->key, r.w);
        return;
    }
    if (t->vbm != NULL && (renderer->draw_blend == SDL_BLENDMODE_NONE || a == 255) && r.w * r.h >= 256) {
        hw_begin();
        msdl_vram_fill(t->vbm, r.x, r.y, r.w, r.h, c);
    } else {
        Uint8 *px = tpx(t);
        if (px == NULL) return;
        for (y = 0; y < r.h; y++) {
            Uint8 *d = px + (r.y + y) * t->pitch + r.x;
            if (renderer->draw_blend == SDL_BLENDMODE_NONE || a == 255) {
                memset(d, c, r.w);
            } else {
                for (x = 0; x < r.w; x++) d[x] = blend8(c, d[x], a, renderer->draw_blend);
            }
        }
    }
    if (t->kind == K_ALPHA && t->a != NULL && t->a_valid) {
        for (y = 0; y < r.h; y++) {
            Uint8 *al = t->a + (r.y + y) * t->w + r.x;
            if (renderer->draw_blend == SDL_BLENDMODE_NONE) memset(al, a, r.w);
            else if (renderer->draw_blend == SDL_BLENDMODE_BLEND) {
                for (x = 0; x < r.w; x++) al[x] = (Uint8)(a + ((al[x] * (255 - a) * 257 + 32768) >> 16));
            }
        }
    } else if (renderer->draw_blend == SDL_BLENDMODE_NONE && a != 255 && t != renderer->back) {
        Uint8 *pl = alpha_plane(t);
        if (pl != NULL) {
            for (y = 0; y < r.h; y++) memset(pl + (r.y + y) * t->w + r.x, a, r.w);
        }
    }
    mark(renderer, &r);
}

int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    SDL_Rect all;
    if (rect == NULL) {
        SDL_Texture *t = renderer->target != NULL ? renderer->target : renderer->back;
        all.x = all.y = 0;
        all.w = t->w;
        all.h = t->h;
        rect = &all;
    }
    fill(renderer, rect);
    return 0;
}

int SDL_RenderDrawPoint(SDL_Renderer *renderer, int x, int y)
{
    SDL_Rect r;
    r.x = x;
    r.y = y;
    r.w = r.h = 1;
    fill(renderer, &r);
    return 0;
}

int SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    SDL_Rect r;
    if (rect == NULL) return 0;
    if (rect->w <= 0 || rect->h <= 0) return 0;
    r = *rect; r.h = 1; fill(renderer, &r);
    if (rect->h > 1) { r.y = rect->y + rect->h - 1; fill(renderer, &r); }
    if (rect->h > 2) {
        r.x = rect->x; r.y = rect->y + 1; r.w = 1; r.h = rect->h - 2; fill(renderer, &r);
        if (rect->w > 1) { r.x = rect->x + rect->w - 1; fill(renderer, &r); }
    }
    return 0;
}

int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2)
{
    int dx = x2 > x1 ? x2 - x1 : x1 - x2, sx = x1 < x2 ? 1 : -1;
    int dy = y2 > y1 ? y1 - y2 : y2 - y1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    SDL_Rect r;
    if (y1 == y2) {
        r.x = x1 < x2 ? x1 : x2; r.y = y1; r.w = dx + 1; r.h = 1;
        fill(renderer, &r);
        return 0;
    }
    if (x1 == x2) {
        r.x = x1; r.y = y1 < y2 ? y1 : y2; r.w = 1; r.h = -dy + 1;
        fill(renderer, &r);
        return 0;
    }
    for (;;) {
        SDL_RenderDrawPoint(renderer, x1, y1);
        if (x1 == x2 && y1 == y2) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    }
    return 0;
}

int SDL_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch)
{
    SDL_Texture *t = cur_target(renderer);
    SDL_Rect r, clip;
    int x, y;
    if (format != SDL_PIXELFORMAT_ABGR8888 && format != 0) return SDL_SetError("SDL_RenderReadPixels(): only ABGR8888");
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (rect == NULL) r = clip;
    else if (!SDL_IntersectRect(rect, &clip, &r)) return 0;
    if (tpx(t) == NULL) return SDL_SetError("cannot lock the texture");
    for (y = 0; y < r.h; y++) {
        Uint32 *d = (Uint32 *)((Uint8 *)pixels + y * pitch);
        const Uint8 *s = t->px + (r.y + y) * t->pitch + r.x;
        for (x = 0; x < r.w; x++) {
            unsigned a = 255;
            Uint8 p = s[x];
            if (t->kind == K_KEYED && p == t->key) {
                a = 0;
                p = t->keyidx;
            } else if (t->kind == K_ALPHA && t->a != NULL) {
                a = t->a[(r.y + y) * t->w + r.x + x];
            }
            d[x] = pal[p].r | ((Uint32)pal[p].g << 8) | ((Uint32)pal[p].b << 16) | ((Uint32)a << 24);
        }
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    SDL_Rect d;
    const Uint8 *px;
    if (renderer->target != NULL) return;
    if (renderer->clear_pending) back_materialize(renderer);
    if (renderer->back->vbm != NULL) {
        /* composed on the card: one blit to the screen */
        SDL_Texture *f = renderer->alias != NULL ? renderer->alias : renderer->back;
        if (f->vbm == NULL) {
            back_materialize(renderer);
            f = renderer->back;
        }
        msdl_fps_tick();
        if (msdl_fps_on) msdl_fps_draw8(tpx(f), f->pitch, f->w, f->h, inv(255, 255, 255), inv(0, 0, 0));
        hw_begin();
        msdl_vram_show(f->vbm, f->w, f->h);
        renderer->dirty = 0;
        return;
    }
    px = renderer->alias != NULL ? renderer->alias->px : renderer->back->px;
    msdl_fps_tick();
    if (msdl_fps_on) {
        msdl_fps_draw8((Uint8 *)px, renderer->back->w, renderer->back->w, renderer->back->h, inv(255, 255, 255), inv(0, 0, 0));
        renderer->dirty = 1;
        renderer->dx0 = renderer->dy0 = 0;
        if (renderer->dx1 < 160) renderer->dx1 = 160;
        if (renderer->dy1 < 24) renderer->dy1 = 24;
    }
    if (!renderer->dirty) {
        d.x = d.y = d.w = d.h = 0;
    } else {
        d.x = renderer->dx0;
        d.y = renderer->dy0;
        d.w = renderer->dx1 - renderer->dx0;
        d.h = renderer->dy1 - renderer->dy0;
    }
    msdl_video_present8(px, renderer->back->w, renderer->back->w, renderer->back->h, &d);
    renderer->dirty = 0;
}

/* ---- mini-SDL extensions for the terrain cache (Game.cpp, AMIGA) ---- */
void SDL_SetRenderOffsetX(SDL_Renderer *renderer, int dx, int dy)
{
    renderer->ox = dx;
    renderer->oy = dy;
}

void SDL_SetRenderClipX(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    renderer->has_clip = rect != NULL;
    if (rect != NULL) renderer->uclip = *rect;
}

/* A render target whose alpha is only 0 or 255 becomes a keyed texture,
   so that copying it with BLEND takes the fast path (opaque runs). */
int SDL_MakeTextureKeyedX(SDL_Texture *t)
{
    size_t i, n = (size_t)t->w * t->h;
    Uint8 used[256];
    int k, have = 0;
    Uint8 ki = 0;
    if (t->vbm != NULL) return 0;
    if (t->kind != K_ALPHA || t->a == NULL || !t->a_valid) return t->kind == K_KEYED || t->kind == K_OPAQUE;
    memset(used, 0, sizeof(used));
    for (i = 0; i < n; i++) {
        Uint8 a = t->a[i];
        if (a == 255) used[t->px[i]] = 1;
        else if (a != 0) return 0;
        else if (!have) { have = 1; ki = t->px[i]; }
    }
    for (k = 255; k >= 0 && used[k]; k--) ;
    if (k < 0) return 0;
    for (i = 0; i < n; i++) if (t->a[i] == 0) t->px[i] = (Uint8)k;
    spans_free(t);
    t->kind = K_KEYED;
    t->key = (Uint8)k;
    t->keyidx = ki;
    t->a_valid = 0;
    return 1;
}

/* The terrain cache: a RAM texture that gets a copy on the card; opaque
   copies from it to VRAM targets are then blits. 0: no room / no VRAM. */
int SDL_MirrorTextureX(SDL_Texture *t)
{
    if (t->vbm != NULL || t->mbm != NULL) return 1;
    if (the_renderer == NULL || the_renderer->back->vbm == NULL) return 0;
    t->mbm = msdl_vram_alloc(t->w, t->h);
    if (t->mbm == NULL) return 0;
    mirror_all(t);
    return 1;
}

/* The shroud cache: made a keyed texture with the key of src (the hidden
   tiles), all transparent; keyed copies with NONE then go in unchanged and
   transparent fills write the key - no conversion of the whole texture. */
int SDL_SetTextureKeyLikeX(SDL_Texture *t, SDL_Texture *src)
{
    if (src == NULL || src->kind != K_KEYED || t->vbm != NULL) return 0;
    spans_free(t);
    t->kind = K_KEYED;
    t->key = src->key;
    t->keyidx = src->keyidx;
    t->a_valid = 0;
    memset(t->px, t->key, (size_t)t->pitch * t->h);
    mirror_all(t);
    return 1;
}
