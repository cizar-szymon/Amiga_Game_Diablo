/* Mini-SDL2: palettes, pixel formats and software surfaces.
   Formats: INDEX8 and ABGR8888 (the only ones the game creates). Blits
   follow SDL 2's rules: colour key, per-pixel alpha with
   SDL_BLENDMODE_BLEND, clipping to the destination's clip rectangle. */
#include <stdio.h>
#include <SDL.h>
#include "msdl_internal.h"

/* ---- palettes ---- */
SDL_Palette *SDL_AllocPalette(int ncolors)
{
    SDL_Palette *p;
    if (ncolors < 1) {
        SDL_SetError("SDL_AllocPalette(): ncolors < 1");
        return NULL;
    }
    p = (SDL_Palette *)malloc(sizeof(SDL_Palette));
    if (p == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    p->colors = (SDL_Color *)malloc(ncolors * sizeof(SDL_Color));
    if (p->colors == NULL) {
        free(p);
        SDL_SetError("out of memory");
        return NULL;
    }
    p->ncolors = ncolors;
    p->version = 1;
    p->refcount = 1;
    memset(p->colors, 0xFF, ncolors * sizeof(SDL_Color));
    return p;
}

void SDL_FreePalette(SDL_Palette *palette)
{
    if (palette == NULL) return;
    if (--palette->refcount > 0) return;
    free(palette->colors);
    free(palette);
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors)
{
    if (palette == NULL) return SDL_SetError("no palette");
    if (firstcolor < 0 || firstcolor >= palette->ncolors) return -1;
    if (ncolors > palette->ncolors - firstcolor) ncolors = palette->ncolors - firstcolor;
    if (colors != palette->colors + firstcolor) {
        memcpy(palette->colors + firstcolor, colors, ncolors * sizeof(SDL_Color));
    }
    palette->version++;
    if (palette->version == 0) palette->version = 1;
    return 0;
}

/* ---- formats ---- */
int msdl_bpp_of_format(Uint32 format)
{
    switch (format) {
        case SDL_PIXELFORMAT_INDEX8: return 8;
        case SDL_PIXELFORMAT_RGB565: return 16;
        case SDL_PIXELFORMAT_ABGR8888: return 32;
        default: return 0;
    }
}

SDL_PixelFormat *alloc_format(Uint32 format)
{
    SDL_PixelFormat *f = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (f == NULL) return NULL;
    f->format = format;
    f->refcount = 1;
    if (format == SDL_PIXELFORMAT_INDEX8) {
        f->BitsPerPixel = 8;
        f->BytesPerPixel = 1;
        f->Rloss = f->Gloss = f->Bloss = f->Aloss = 8;
    } else {
        f->BitsPerPixel = 32;
        f->BytesPerPixel = 4;
        f->Rmask = 0x000000FFu; f->Rshift = 0;
        f->Gmask = 0x0000FF00u; f->Gshift = 8;
        f->Bmask = 0x00FF0000u; f->Bshift = 16;
        f->Amask = 0xFF000000u; f->Ashift = 24;
    }
    return f;
}

/* Nearest palette entry (only for 32 -> 8 bit blits and MapRGBA on 8-bit
   surfaces, both rare). */
Uint8 nearest_index(const SDL_Palette *pal, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    int i, best = 0;
    unsigned long bestd = 0xFFFFFFFFul;
    for (i = 0; i < pal->ncolors; i++) {
        const SDL_Color *c = &pal->colors[i];
        int dr = c->r - r, dg = c->g - g, db = c->b - b, da = c->a - a;
        unsigned long d = (unsigned long)(dr * dr + dg * dg + db * db + da * da);
        if (d < bestd) {
            bestd = d;
            best = i;
            if (d == 0) break;
        }
    }
    return (Uint8)best;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (format->BytesPerPixel == 1) {
        return format->palette != NULL ? nearest_index(format->palette, r, g, b, a) : 0;
    }
    return (Uint32)r | ((Uint32)g << 8) | ((Uint32)b << 16) | ((Uint32)a << 24);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapRGBA(format, r, g, b, 255);
}

void SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormat *format, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    if (format->BytesPerPixel == 1) {
        if (format->palette != NULL && pixel < (Uint32)format->palette->ncolors) {
            const SDL_Color *c = &format->palette->colors[pixel];
            *r = c->r; *g = c->g; *b = c->b; *a = c->a;
        } else {
            *r = *g = *b = 0; *a = 255;
        }
        return;
    }
    *r = (Uint8)pixel;
    *g = (Uint8)(pixel >> 8);
    *b = (Uint8)(pixel >> 16);
    *a = (Uint8)(pixel >> 24);
}

/* ---- rects ---- */
SDL_bool SDL_IntersectRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result)
{
    int x0 = A->x > B->x ? A->x : B->x;
    int y0 = A->y > B->y ? A->y : B->y;
    int x1 = (A->x + A->w) < (B->x + B->w) ? (A->x + A->w) : (B->x + B->w);
    int y1 = (A->y + A->h) < (B->y + B->h) ? (A->y + A->h) : (B->y + B->h);
    result->x = x0;
    result->y = y0;
    result->w = x1 - x0;
    result->h = y1 - y0;
    if (result->w <= 0 || result->h <= 0) {
        result->w = result->h = 0;
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

/* ---- surfaces ---- */
/* Load-time trace: a log line every 256 surfaces (temporary). */
unsigned long msdl_trace_surfaces, msdl_trace_blits, msdl_trace_pixels;

SDL_Surface *create_surface(int width, int height, Uint32 format)
{
    SDL_Surface *s;
    int bpp = msdl_bpp_of_format(format);
    if ((++msdl_trace_surfaces & 255) == 0) {
        SDL_Log("trace: %lu surfaces (%dx%d), %lu blits, %lu Kpixels blitted",
                msdl_trace_surfaces, width, height, msdl_trace_blits, msdl_trace_pixels >> 10);
    }
    if (bpp != 8 && bpp != 32) {
        SDL_SetError("unsupported pixel format");
        return NULL;
    }
    if (width < 0 || height < 0) {
        SDL_SetError("negative surface size");
        return NULL;
    }
    s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (s == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    s->format = alloc_format(format);
    if (s->format == NULL) {
        free(s);
        SDL_SetError("out of memory");
        return NULL;
    }
    if (bpp == 8) {
        s->format->palette = SDL_AllocPalette(256);
        if (s->format->palette == NULL) {
            free(s->format);
            free(s);
            return NULL;
        }
    }
    s->w = width;
    s->h = height;
    s->pitch = ((width * (bpp / 8)) + 3) & ~3;
    if (s->pitch > 0 && height > 0) {
        s->pixels = calloc(1, (size_t)s->pitch * height);
        if (s->pixels == NULL) {
            SDL_FreeSurface(s);
            SDL_SetError("out of memory");
            return NULL;
        }
    }
    s->clip_rect.w = width;
    s->clip_rect.h = height;
    s->refcount = 1;
    s->blendmode = (bpp == 32) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    s->alphamod = 255;
    return s;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    (void)flags;
    if (depth == 8) return create_surface(width, height, SDL_PIXELFORMAT_INDEX8);
    if (depth == 32 && Rmask == 0x000000FFu && Gmask == 0x0000FF00u && Bmask == 0x00FF0000u) {
        SDL_Surface *s = create_surface(width, height, SDL_PIXELFORMAT_ABGR8888);
        if (s != NULL && Amask == 0) {
            s->format->Amask = 0;
            s->blendmode = SDL_BLENDMODE_NONE;
        }
        return s;
    }
    SDL_SetError("SDL_CreateRGBSurface(): unsupported depth/masks");
    return NULL;
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format)
{
    (void)flags;
    (void)depth;
    return create_surface(width, height, format);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (surface == NULL) return;
    if (--surface->refcount > 0) return;
    if (surface->format != NULL) {
        SDL_FreePalette(surface->format->palette);
        free(surface->format);
    }
    if (!(surface->flags & SDL_PREALLOC)) free(surface->pixels);
    free(surface);
}

int SDL_LockSurface(SDL_Surface *surface) { surface->locked++; return 0; }
void SDL_UnlockSurface(SDL_Surface *surface) { if (surface->locked > 0) surface->locked--; }

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
    if (surface->format->BytesPerPixel != 1) return SDL_SetError("not a palettized surface");
    if (palette == surface->format->palette) return 0;
    SDL_FreePalette(surface->format->palette);
    surface->format->palette = palette;
    if (palette != NULL) palette->refcount++;
    return 0;
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    if (surface == NULL) return SDL_SetError("no surface");
    surface->has_colorkey = flag != 0;
    surface->colorkey = key;
    return 0;
}

int SDL_GetColorKey(SDL_Surface *surface, Uint32 *key)
{
    if (surface == NULL || !surface->has_colorkey) return -1;
    if (key != NULL) *key = surface->colorkey;
    return 0;
}

SDL_bool SDL_HasColorKey(SDL_Surface *surface)
{
    return (surface != NULL && surface->has_colorkey) ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetSurfaceRLE(SDL_Surface *surface, int flag) { (void)surface; (void)flag; return 0; }

int SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode)
{
    if (surface == NULL) return SDL_SetError("no surface");
    surface->blendmode = blendMode;
    return 0;
}

int SDL_GetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode *blendMode)
{
    if (surface == NULL) return SDL_SetError("no surface");
    if (blendMode != NULL) *blendMode = surface->blendmode;
    return 0;
}

int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha)
{
    surface->alphamod = alpha;
    return 0;
}

SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    SDL_Rect full;
    full.x = full.y = 0;
    full.w = surface->w;
    full.h = surface->h;
    if (rect == NULL) {
        surface->clip_rect = full;
        return SDL_TRUE;
    }
    return SDL_IntersectRect(rect, &full, &surface->clip_rect);
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    SDL_Rect r;
    int x, y;
    if (dst == NULL) return SDL_SetError("no surface");
    if (rect == NULL) {
        r = dst->clip_rect;
    } else if (!SDL_IntersectRect(rect, &dst->clip_rect, &r)) {
        return 0;
    }
    if (dst->format->BytesPerPixel == 1) {
        Uint8 *row = (Uint8 *)dst->pixels + r.y * dst->pitch + r.x;
        for (y = 0; y < r.h; y++, row += dst->pitch) memset(row, (int)(color & 0xFF), r.w);
    } else {
        Uint8 *row = (Uint8 *)dst->pixels + r.y * dst->pitch + r.x * 4;
        for (y = 0; y < r.h; y++, row += dst->pitch) {
            Uint32 *p = (Uint32 *)row;
            for (x = 0; x < r.w; x++) p[x] = color;
        }
    }
    return 0;
}

/* ---- blitting ---- */

/* SDL's blend for one ABGR8888 pixel:
   dstRGB = srcRGB * srcA + dstRGB * (1 - srcA), dstA = srcA + dstA * (1 - srcA) */
static inline Uint32 blend_pixel(Uint32 s, Uint32 d, unsigned a)
{
    unsigned ia;
    Uint32 rb, g, da;
    if (a == 255) return s;
    if (a == 0) return d;
    ia = 255 - a;
    rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia + 0x00800080u) >> 8) & 0x00FF00FFu;
    g = (((s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia + 0x00008000u) >> 8) & 0x0000FF00u;
    da = a + (((d >> 24) * ia + 127) / 255);
    if (da > 255) da = 255;
    return rb | g | (da << 24);
}

/* 256 ABGR8888 entries of a palette, alpha from the palette. */
void palette_to_abgr(const SDL_Palette *pal, Uint32 *lut)
{
    int i;
    for (i = 0; i < 256; i++) {
        if (pal != NULL && i < pal->ncolors) {
            const SDL_Color *c = &pal->colors[i];
            lut[i] = (Uint32)c->r | ((Uint32)c->g << 8) | ((Uint32)c->b << 16) | ((Uint32)c->a << 24);
        } else {
            lut[i] = 0xFF000000u;
        }
    }
}

int palettes_equal(const SDL_Palette *a, const SDL_Palette *b)
{
    if (a == b) return 1;
    if (a == NULL || b == NULL || a->ncolors != b->ncolors) return 0;
    return memcmp(a->colors, b->colors, a->ncolors * sizeof(SDL_Color)) == 0;
}

/* The last few 8 -> 8 bit palette maps, keyed by the contents of both
   palettes (the game edits palette entries in place, so pointers and
   versions are not enough). Building a map searches the nearest colour for
   every differing entry: half the start-up time before this cache. */
#define MAP_CACHE 8
static struct { Uint32 hs, hd; int valid; Uint8 map[256]; } map_cache[MAP_CACHE];
static int map_cache_next;
static Uint32 map_key_s, map_key_d;

static Uint32 palette_hash(const SDL_Palette *p)
{
    Uint32 h = 2166136261u;
    int i, n;
    const Uint32 *c;
    if (p == NULL) return 0;
    n = p->ncolors;
    c = (const Uint32 *)p->colors;    /* SDL_Color is 4 bytes */
    for (i = 0; i < n; i++) h = (h ^ c[i]) * 16777619u;
    return h ^ (Uint32)n;
}

static int map_cache_get(const SDL_Palette *sp, const SDL_Palette *dp, Uint8 *map)
{
    int i;
    map_key_s = palette_hash(sp);
    map_key_d = palette_hash(dp);
    for (i = 0; i < MAP_CACHE; i++) {
        if (map_cache[i].valid && map_cache[i].hs == map_key_s && map_cache[i].hd == map_key_d) {
            memcpy(map, map_cache[i].map, 256);
            return 1;
        }
    }
    return 0;
}

static void map_cache_put(const Uint8 *map)
{
    int i = map_cache_next;
    map_cache_next = (map_cache_next + 1) % MAP_CACHE;
    map_cache[i].hs = map_key_s;
    map_cache[i].hd = map_key_d;
    map_cache[i].valid = 1;
    memcpy(map_cache[i].map, map, 256);
}

void blit_rect(SDL_Surface *src, const SDL_Rect *sr, SDL_Surface *dst, const SDL_Rect *dr)
{
    int x, y, w = sr->w, h = sr->h;
    int sbpp = src->format->BytesPerPixel, dbpp = dst->format->BytesPerPixel;
    const Uint8 *srow = (const Uint8 *)src->pixels + sr->y * src->pitch + sr->x * sbpp;
    Uint8 *drow = (Uint8 *)dst->pixels + dr->y * dst->pitch + dr->x * dbpp;
    int key = src->has_colorkey;
    Uint32 ckey = src->colorkey;

    if (sbpp == 1 && dbpp == 1) {
        Uint8 map[256];
        int identity = palettes_equal(src->format->palette, dst->format->palette);
        if (!identity && !map_cache_get(src->format->palette, dst->format->palette, map)) {
            /* as SDL_MapPalette: the same colour at the same index maps to
               itself, only the others are searched */
            const SDL_Palette *sp = src->format->palette, *dp = dst->format->palette;
            for (x = 0; x < 256; x++) {
                const SDL_Color *c = (sp != NULL && x < sp->ncolors) ? &sp->colors[x] : NULL;
                if (c == NULL) {
                    map[x] = 0;
                } else if (dp != NULL && x < dp->ncolors && memcmp(c, &dp->colors[x], sizeof(SDL_Color)) == 0) {
                    map[x] = (Uint8)x;
                } else {
                    map[x] = dp != NULL ? nearest_index(dp, c->r, c->g, c->b, c->a) : 0;
                }
            }
            map_cache_put(map);
        }
        for (y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
            if (!key && identity) {
                memmove(drow, srow, w);
            } else {
                for (x = 0; x < w; x++) {
                    Uint8 p = srow[x];
                    if (key && p == (Uint8)ckey) continue;
                    drow[x] = identity ? p : map[p];
                }
            }
        }
        return;
    }

    if (sbpp == 1 && dbpp == 4) {
        Uint32 lut[256];
        int blend = src->blendmode == SDL_BLENDMODE_BLEND;
        palette_to_abgr(src->format->palette, lut);
        if (src->alphamod != 255) {
            for (x = 0; x < 256; x++) {
                unsigned a = ((lut[x] >> 24) * src->alphamod + 127) / 255;
                lut[x] = (lut[x] & 0x00FFFFFFu) | (a << 24);
            }
        }
        for (y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
            Uint32 *d = (Uint32 *)drow;
            for (x = 0; x < w; x++) {
                Uint8 p = srow[x];
                if (key && p == (Uint8)ckey) continue;
                d[x] = blend ? blend_pixel(lut[p], d[x], lut[p] >> 24) : lut[p];
            }
        }
        return;
    }

    if (sbpp == 4 && dbpp == 4) {
        int blend = src->blendmode == SDL_BLENDMODE_BLEND && src->format->Amask != 0;
        unsigned amod = src->alphamod;
        for (y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
            const Uint32 *s = (const Uint32 *)srow;
            Uint32 *d = (Uint32 *)drow;
            if (!key && !blend && amod == 255 && src != dst) {
                memcpy(d, s, w * 4);
                continue;
            }
            for (x = 0; x < w; x++) {
                Uint32 p = s[x];
                unsigned a;
                if (key && (p & 0x00FFFFFFu) == (ckey & 0x00FFFFFFu)) continue;
                if (!blend) {
                    d[x] = src->format->Amask ? p : (p | 0xFF000000u);
                    continue;
                }
                a = p >> 24;
                if (amod != 255) a = (a * amod + 127) / 255;
                d[x] = blend_pixel(p, d[x], a);
            }
        }
        return;
    }

    /* 32 -> 8: nearest palette colour, transparent pixels skipped when
       blending. */
    {
        const SDL_Palette *pal = dst->format->palette;
        int blend = src->blendmode == SDL_BLENDMODE_BLEND && src->format->Amask != 0;
        Uint32 last = 0;
        Uint8 last_idx = 0;
        int have_last = 0;
        for (y = 0; y < h; y++, srow += src->pitch, drow += dst->pitch) {
            const Uint32 *s = (const Uint32 *)srow;
            for (x = 0; x < w; x++) {
                Uint32 p = s[x];
                if (key && (p & 0x00FFFFFFu) == (ckey & 0x00FFFFFFu)) continue;
                if (blend && (p >> 24) < 128) continue;
                if (!have_last || p != last) {
                    last = p;
                    last_idx = pal != NULL ? nearest_index(pal, (Uint8)p, (Uint8)(p >> 8), (Uint8)(p >> 16), 255) : 0;
                    have_last = 1;
                }
                drow[x] = last_idx;
            }
        }
    }
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    SDL_Rect sr, dr, full;
    int dx, dy;

    if (src == NULL || dst == NULL) return SDL_SetError("SDL_UpperBlit: passed a NULL surface");

    /* source rectangle clipped to the source surface */
    if (srcrect != NULL) {
        sr = *srcrect;
    } else {
        sr.x = sr.y = 0;
        sr.w = src->w;
        sr.h = src->h;
    }
    dx = dstrect != NULL ? dstrect->x : 0;
    dy = dstrect != NULL ? dstrect->y : 0;
    if (sr.x < 0) { dx -= sr.x; sr.w += sr.x; sr.x = 0; }
    if (sr.y < 0) { dy -= sr.y; sr.h += sr.y; sr.y = 0; }
    if (sr.x + sr.w > src->w) sr.w = src->w - sr.x;
    if (sr.y + sr.h > src->h) sr.h = src->h - sr.y;

    /* destination clipped to the clip rectangle */
    full = dst->clip_rect;
    dr.x = dx;
    dr.y = dy;
    dr.w = sr.w;
    dr.h = sr.h;
    if (dr.x < full.x) { int d = full.x - dr.x; sr.x += d; sr.w -= d; dr.w -= d; dr.x = full.x; }
    if (dr.y < full.y) { int d = full.y - dr.y; sr.y += d; sr.h -= d; dr.h -= d; dr.y = full.y; }
    if (dr.x + dr.w > full.x + full.w) { dr.w = full.x + full.w - dr.x; sr.w = dr.w; }
    if (dr.y + dr.h > full.y + full.h) { dr.h = full.y + full.h - dr.y; sr.h = dr.h; }

    if (dr.w <= 0 || dr.h <= 0 || sr.w <= 0 || sr.h <= 0) {
        if (dstrect != NULL) { dstrect->w = dstrect->h = 0; }
        return 0;
    }
    msdl_trace_blits++;
    msdl_trace_pixels += (unsigned long)dr.w * dr.h;
    blit_rect(src, &sr, dst, &dr);
    if (dstrect != NULL) *dstrect = dr;
    return 0;
}

SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags)
{
    SDL_Surface *d;
    SDL_Rect r;
    SDL_BlendMode bm;
    Uint8 am;
    (void)flags;
    if (src == NULL) {
        SDL_SetError("no surface");
        return NULL;
    }
    d = create_surface(src->w, src->h, pixel_format);
    if (d == NULL) return NULL;
    if (d->format->BytesPerPixel == 1 && src->format->BytesPerPixel == 1 && src->format->palette != NULL) {
        SDL_SetPaletteColors(d->format->palette, src->format->palette->colors, 0, src->format->palette->ncolors);
    }

    /* plain copy: no key, no blending */
    bm = src->blendmode;
    am = src->alphamod;
    src->blendmode = SDL_BLENDMODE_NONE;
    src->alphamod = 255;
    {
        int key = src->has_colorkey;
        src->has_colorkey = 0;
        r.x = r.y = 0;
        r.w = src->w;
        r.h = src->h;
        SDL_UpperBlit(src, NULL, d, &r);
        src->has_colorkey = key;
    }
    src->blendmode = bm;
    src->alphamod = am;

    if (src->has_colorkey) {
        if (src->format->BytesPerPixel == 1 && d->format->BytesPerPixel == 4) {
            /* SDL 2: the key becomes the colour and transparent pixels */
            Uint32 kc;
            int x, y;
            const SDL_Color *c = &src->format->palette->colors[src->colorkey & 0xFF];
            kc = (Uint32)c->r | ((Uint32)c->g << 8) | ((Uint32)c->b << 16);
            for (y = 0; y < src->h; y++) {
                const Uint8 *s = (const Uint8 *)src->pixels + y * src->pitch;
                Uint32 *p = (Uint32 *)((Uint8 *)d->pixels + y * d->pitch);
                for (x = 0; x < src->w; x++) {
                    if (s[x] == (Uint8)src->colorkey) p[x] = kc;   /* alpha 0 */
                }
            }
            SDL_SetColorKey(d, 1, kc | 0xFF000000u);
            d->blendmode = SDL_BLENDMODE_BLEND;
        } else {
            SDL_SetColorKey(d, 1, src->colorkey);
        }
    }
    if (d->format->BytesPerPixel == 4 && src->format->BytesPerPixel == 4) {
        d->blendmode = src->blendmode;
    }
    return d;
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags)
{
    SDL_Surface *d = SDL_ConvertSurfaceFormat(src, fmt->format, flags);
    if (d != NULL && fmt->BytesPerPixel == 1 && fmt->palette != NULL) {
        SDL_SetPaletteColors(d->format->palette, fmt->palette->colors, 0, fmt->palette->ncolors);
    }
    if (d != NULL && fmt->BytesPerPixel == 4 && fmt->Amask == 0) {
        d->format->Amask = 0;
    }
    if (d != NULL) d->blendmode = src->blendmode;
    return d;
}
