/* Native SDL2 subset: pixel formats, palettes, surfaces, blits, fills and
   conversions. Formats: 8-bit indexed and packed 16/32-bit by masks.
   The game draws in 8 bits; the 16/32-bit paths are for the odd
   conversion (cursor, PNG) and need not be fast. */
#include <stdlib.h>
#include <string.h>

#include "msdl_internal.h"

/* ---- pixel formats ---- */
static void mask_shift_loss(Uint32 mask, Uint8 *shift, Uint8 *loss)
{
    int s = 0, bits = 0;
    if (mask == 0) {
        *shift = 0;
        *loss = 8;
        return;
    }
    while (((mask >> s) & 1) == 0) s++;
    while (((mask >> (s + bits)) & 1) != 0 && s + bits < 32) bits++;
    *shift = (Uint8)s;
    *loss = (Uint8)(bits >= 8 ? 0 : 8 - bits);
}

static int format_masks(Uint32 format, int *bpp, Uint32 *r, Uint32 *g, Uint32 *b, Uint32 *a)
{
    *r = *g = *b = *a = 0;
    switch (format) {
        case SDL_PIXELFORMAT_INDEX8: *bpp = 8; return 1;
        case SDL_PIXELFORMAT_RGB565: *bpp = 16; *r = 0xF800; *g = 0x07E0; *b = 0x001F; return 1;
        case SDL_PIXELFORMAT_RGB888: *bpp = 32; *r = 0x00FF0000; *g = 0x0000FF00; *b = 0x000000FF; return 1;
        case SDL_PIXELFORMAT_BGR888: *bpp = 32; *r = 0x000000FF; *g = 0x0000FF00; *b = 0x00FF0000; return 1;
        case SDL_PIXELFORMAT_ARGB8888: *bpp = 32; *a = 0xFF000000; *r = 0x00FF0000; *g = 0x0000FF00; *b = 0x000000FF; return 1;
        case SDL_PIXELFORMAT_ABGR8888: *bpp = 32; *a = 0xFF000000; *b = 0x00FF0000; *g = 0x0000FF00; *r = 0x000000FF; return 1;
        case SDL_PIXELFORMAT_RGBA8888: *bpp = 32; *r = 0xFF000000; *g = 0x00FF0000; *b = 0x0000FF00; *a = 0x000000FF; return 1;
        case SDL_PIXELFORMAT_BGRA8888: *bpp = 32; *b = 0xFF000000; *g = 0x00FF0000; *r = 0x0000FF00; *a = 0x000000FF; return 1;
        default: return 0;
    }
}

static Uint32 format_from_masks(int depth, Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
    static const Uint32 known[] = {
        SDL_PIXELFORMAT_RGB565, SDL_PIXELFORMAT_RGB888, SDL_PIXELFORMAT_BGR888, SDL_PIXELFORMAT_ARGB8888,
        SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_BGRA8888
    };
    unsigned i;
    if (depth == 8) return SDL_PIXELFORMAT_INDEX8;
    if (depth == 32 && r == 0 && g == 0 && b == 0) return a != 0 ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB888;
    if (depth == 16 && r == 0 && g == 0 && b == 0) return SDL_PIXELFORMAT_RGB565;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        int kb;
        Uint32 kr, kg, kbm, ka;
        format_masks(known[i], &kb, &kr, &kg, &kbm, &ka);
        if (kb == depth && kr == r && kg == g && kbm == b && ka == a) return known[i];
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}

SDL_PixelFormat *msdl_alloc_format(Uint32 format)
{
    SDL_PixelFormat *f;
    int bpp;
    Uint32 r, g, b, a;
    if (!format_masks(format, &bpp, &r, &g, &b, &a)) {
        SDL_SetError("unsupported pixel format 0x%08lx", (unsigned long)format);
        return NULL;
    }
    f = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (f == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    f->format = format;
    f->BitsPerPixel = (Uint8)bpp;
    f->BytesPerPixel = (Uint8)((bpp + 7) / 8);
    f->Rmask = r;
    f->Gmask = g;
    f->Bmask = b;
    f->Amask = a;
    mask_shift_loss(r, &f->Rshift, &f->Rloss);
    mask_shift_loss(g, &f->Gshift, &f->Gloss);
    mask_shift_loss(b, &f->Bshift, &f->Bloss);
    mask_shift_loss(a, &f->Ashift, &f->Aloss);
    f->refcount = 1;
    return f;
}

void msdl_free_format(SDL_PixelFormat *f)
{
    if (f == NULL) return;
    if (f->palette != NULL) SDL_FreePalette(f->palette);
    free(f);
}

/* ---- palettes ---- */
SDL_Palette *SDL_AllocPalette(int ncolors)
{
    SDL_Palette *p;
    int i;
    if (ncolors < 1) {
        SDL_SetError("bad palette size");
        return NULL;
    }
    p = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
    if (p == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    p->colors = (SDL_Color *)malloc(sizeof(SDL_Color) * ncolors);
    if (p->colors == NULL) {
        free(p);
        SDL_SetError("out of memory");
        return NULL;
    }
    for (i = 0; i < ncolors; i++) {
        p->colors[i].r = p->colors[i].g = p->colors[i].b = 255;
        p->colors[i].a = 255;
    }
    p->ncolors = ncolors;
    p->version = 1;
    p->refcount = 1;
    return p;
}

void SDL_FreePalette(SDL_Palette *p)
{
    if (p == NULL) return;
    if (--p->refcount > 0) return;
    free(p->colors);
    free(p);
}

int SDL_SetPaletteColors(SDL_Palette *p, const SDL_Color *colors, int first, int ncolors)
{
    if (p == NULL) return SDL_SetError("no palette");
    if (first < 0 || first >= p->ncolors) return SDL_SetError("bad first colour");
    if (first + ncolors > p->ncolors) ncolors = p->ncolors - first;
    if (colors != p->colors + first) memcpy(p->colors + first, colors, sizeof(SDL_Color) * ncolors);
    if (++p->version == 0) p->version = 1;
    return 0;
}

int SDL_SetSurfacePalette(SDL_Surface *s, SDL_Palette *p)
{
    if (s == NULL || s->format == NULL) return SDL_SetError("no surface");
    if (s->format->palette == p) return 0;
    if (p != NULL) p->refcount++;
    if (s->format->palette != NULL) SDL_FreePalette(s->format->palette);
    s->format->palette = p;
    return 0;
}

/* ---- surfaces ---- */
static SDL_Surface *new_surface(int w, int h, Uint32 format, void *pixels, int pitch)
{
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (s == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    s->format = msdl_alloc_format(format);
    s->map = (SDL_BlitMap *)calloc(1, sizeof(SDL_BlitMap));
    if (s->format == NULL || s->map == NULL) {
        msdl_free_format(s->format);
        free(s->map);
        free(s);
        return NULL;
    }
    if (format == SDL_PIXELFORMAT_INDEX8) {
        s->format->palette = SDL_AllocPalette(256);
        if (s->format->palette == NULL) {
            msdl_free_format(s->format);
            free(s->map);
            free(s);
            return NULL;
        }
    }
    s->w = w;
    s->h = h;
    if (pixels != NULL) {
        s->pixels = pixels;
        s->pitch = pitch;
        s->flags = SDL_PREALLOC;
    } else {
        /* rows padded to 4 bytes so they can be moved as long words */
        s->pitch = (w * s->format->BytesPerPixel + 3) & ~3;
        if (w > 0 && h > 0) {
            s->pixels = calloc(1, (size_t)s->pitch * h);
            if (s->pixels == NULL) {
                SDL_FreeSurface(s);
                SDL_SetError("out of memory for a %dx%d surface", w, h);
                return NULL;
            }
        }
    }
    s->clip_rect.w = w;
    s->clip_rect.h = h;
    s->refcount = 1;
    return s;
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format)
{
    (void)flags;
    (void)depth;
    return new_surface(width, height, format, NULL, 0);
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int width, int height, int depth, int pitch, Uint32 format)
{
    (void)depth;
    return new_surface(width, height, format, pixels, pitch);
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask)
{
    Uint32 format = format_from_masks(depth, Rmask, Gmask, Bmask, Amask);
    (void)flags;
    if (format == SDL_PIXELFORMAT_UNKNOWN) {
        SDL_SetError("unsupported surface depth %d / masks", depth);
        return NULL;
    }
    return new_surface(width, height, format, NULL, 0);
}

void SDL_FreeSurface(SDL_Surface *s)
{
    if (s == NULL || (s->flags & SDL_DONTFREE)) return;
    if (--s->refcount > 0) return;
    if (!(s->flags & SDL_PREALLOC)) free(s->pixels);
    msdl_free_format(s->format);
    free(s->map);
    free(s);
}

SDL_bool SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *rect)
{
    SDL_Rect full;
    if (s == NULL) return SDL_FALSE;
    full.x = 0;
    full.y = 0;
    full.w = s->w;
    full.h = s->h;
    if (rect == NULL) {
        s->clip_rect = full;
        return SDL_TRUE;
    }
    return SDL_IntersectRect(rect, &full, &s->clip_rect);
}

int SDL_SetColorKey(SDL_Surface *s, int flag, Uint32 key)
{
    if (s == NULL) return SDL_SetError("no surface");
    s->map->has_colorkey = flag != 0;
    s->map->colorkey = key;
    return 0;
}

/* ---- pixel access for the slow paths ---- */
static Uint32 get_pixel(const SDL_Surface *s, int x, int y)
{
    const Uint8 *p = (const Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
        case 1: return *p;
        case 2: return *(const Uint16 *)p;
        default: return *(const Uint32 *)p;
    }
}

static void put_pixel(SDL_Surface *s, int x, int y, Uint32 v)
{
    Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
        case 1: *p = (Uint8)v; break;
        case 2: *(Uint16 *)p = (Uint16)v; break;
        default: *(Uint32 *)p = v; break;
    }
}

static void to_rgba(const SDL_PixelFormat *f, Uint32 v, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    if (f->palette != NULL) {
        const SDL_Color *c = &f->palette->colors[v & 0xFF];
        *r = c->r;
        *g = c->g;
        *b = c->b;
        *a = 255;
        return;
    }
    *r = (Uint8)(((v & f->Rmask) >> f->Rshift) << f->Rloss);
    *g = (Uint8)(((v & f->Gmask) >> f->Gshift) << f->Gloss);
    *b = (Uint8)(((v & f->Bmask) >> f->Bshift) << f->Bloss);
    *a = f->Amask != 0 ? (Uint8)(((v & f->Amask) >> f->Ashift) << f->Aloss) : 255;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (f->palette != NULL) {
        int i, best = 0;
        long best_d = 0x7FFFFFFFL;
        for (i = 0; i < f->palette->ncolors; i++) {
            const SDL_Color *c = &f->palette->colors[i];
            long dr = (long)c->r - r, dg = (long)c->g - g, db = (long)c->b - b;
            long d = dr * dr + dg * dg + db * db;
            if (d < best_d) {
                best_d = d;
                best = i;
                if (d == 0) break;
            }
        }
        return (Uint32)best;
    }
    return ((Uint32)(r >> f->Rloss) << f->Rshift) | ((Uint32)(g >> f->Gloss) << f->Gshift) |
           ((Uint32)(b >> f->Bloss) << f->Bshift) | (f->Amask != 0 ? (((Uint32)(a >> f->Aloss) << f->Ashift) & f->Amask) : 0);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) { return SDL_MapRGBA(f, r, g, b, 255); }

/* ---- fill ---- */
int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    SDL_Rect r;
    int y;
    if (dst == NULL) return SDL_SetError("no surface");
    if (rect == NULL) {
        r = dst->clip_rect;
    } else if (!SDL_IntersectRect(rect, &dst->clip_rect, &r)) {
        return 0;
    }
    for (y = r.y; y < r.y + r.h; y++) {
        Uint8 *row = (Uint8 *)dst->pixels + y * dst->pitch;
        int x;
        switch (dst->format->BytesPerPixel) {
            case 1: memset(row + r.x, (int)(color & 0xFF), (size_t)r.w); break;
            case 2: for (x = r.x; x < r.x + r.w; x++) ((Uint16 *)row)[x] = (Uint16)color; break;
            default: for (x = r.x; x < r.x + r.w; x++) ((Uint32 *)row)[x] = color; break;
        }
    }
    return 0;
}

/* ---- blits ---- */
/* Same pixel size and no conversion needed: rows copied, the colour key
   skipped. */
static int same_format(const SDL_Surface *src, const SDL_Surface *dst)
{
    return src->format->format == dst->format->format &&
           (src->format->palette == NULL || src->format->palette == dst->format->palette ||
            src->format->format == SDL_PIXELFORMAT_INDEX8);
}

static void blit_rect(SDL_Surface *src, const SDL_Rect *sr, SDL_Surface *dst, const SDL_Rect *dr)
{
    int y, w = dr->w, h = dr->h;
    int key = src->map->has_colorkey;
    Uint32 kv = src->map->colorkey;

    if (same_format(src, dst)) {
        int bpp = src->format->BytesPerPixel;
        for (y = 0; y < h; y++) {
            const Uint8 *s = (const Uint8 *)src->pixels + (sr->y + y) * src->pitch + sr->x * bpp;
            Uint8 *d = (Uint8 *)dst->pixels + (dr->y + y) * dst->pitch + dr->x * bpp;
            if (!key) {
                memmove(d, s, (size_t)w * bpp);
            } else if (bpp == 1) {
                int x;
                Uint8 k = (Uint8)kv;
                for (x = 0; x < w; x++) {
                    if (s[x] != k) d[x] = s[x];
                }
            } else {
                int x;
                for (x = 0; x < w; x++) {
                    Uint32 v = get_pixel(src, sr->x + x, sr->y + y);
                    if (v != kv) put_pixel(dst, dr->x + x, dr->y + y, v);
                }
            }
        }
        return;
    }
    /* conversion pixel by pixel */
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            Uint8 r, g, b, a;
            Uint32 v = get_pixel(src, sr->x + x, sr->y + y);
            if (key && v == kv) continue;
            to_rgba(src->format, v, &r, &g, &b, &a);
            put_pixel(dst, dr->x + x, dr->y + y, SDL_MapRGBA(dst->format, r, g, b, a));
        }
    }
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    SDL_Rect sr, dr, clip;
    int dx, dy;
    if (src == NULL || dst == NULL) return SDL_SetError("SDL_UpperBlit: passed a NULL surface");
    if (srcrect != NULL) {
        sr = *srcrect;
    } else {
        sr.x = sr.y = 0;
        sr.w = src->w;
        sr.h = src->h;
    }
    dx = dstrect != NULL ? dstrect->x : 0;
    dy = dstrect != NULL ? dstrect->y : 0;
    /* clip the source to the source surface */
    if (sr.x < 0) { dx -= sr.x; sr.w += sr.x; sr.x = 0; }
    if (sr.y < 0) { dy -= sr.y; sr.h += sr.y; sr.y = 0; }
    if (sr.x + sr.w > src->w) sr.w = src->w - sr.x;
    if (sr.y + sr.h > src->h) sr.h = src->h - sr.y;
    /* clip to the destination's clip rectangle */
    clip = dst->clip_rect;
    if (dx < clip.x) { sr.x += clip.x - dx; sr.w -= clip.x - dx; dx = clip.x; }
    if (dy < clip.y) { sr.y += clip.y - dy; sr.h -= clip.y - dy; dy = clip.y; }
    if (dx + sr.w > clip.x + clip.w) sr.w = clip.x + clip.w - dx;
    if (dy + sr.h > clip.y + clip.h) sr.h = clip.y + clip.h - dy;
    dr.x = dx;
    dr.y = dy;
    dr.w = sr.w > 0 ? sr.w : 0;
    dr.h = sr.h > 0 ? sr.h : 0;
    if (dstrect != NULL) *dstrect = dr;
    if (dr.w == 0 || dr.h == 0) return 0;
    blit_rect(src, &sr, dst, &dr);
    return 0;
}

/* Nearest-pixel scaling. */
int SDL_UpperBlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    SDL_Rect sr, dr, full, cr;
    int x, y;
    int key, convert;
    if (src == NULL || dst == NULL) return SDL_SetError("SDL_UpperBlitScaled: passed a NULL surface");
    if (srcrect != NULL) {
        sr = *srcrect;
    } else {
        sr.x = sr.y = 0;
        sr.w = src->w;
        sr.h = src->h;
    }
    if (dstrect != NULL) {
        dr = *dstrect;
    } else {
        dr.x = dr.y = 0;
        dr.w = dst->w;
        dr.h = dst->h;
    }
    if (sr.w <= 0 || sr.h <= 0 || dr.w <= 0 || dr.h <= 0) return 0;
    if (sr.w == dr.w && sr.h == dr.h) return SDL_UpperBlit(src, &sr, dst, dstrect);
    full = dst->clip_rect;
    if (!SDL_IntersectRect(&dr, &full, &cr)) return 0;
    key = src->map->has_colorkey;
    convert = !same_format(src, dst);
    for (y = cr.y; y < cr.y + cr.h; y++) {
        int sy = sr.y + (int)((long)(y - dr.y) * sr.h / dr.h);
        if (sy < 0 || sy >= src->h) continue;
        for (x = cr.x; x < cr.x + cr.w; x++) {
            int sx = sr.x + (int)((long)(x - dr.x) * sr.w / dr.w);
            Uint32 v;
            if (sx < 0 || sx >= src->w) continue;
            v = get_pixel(src, sx, sy);
            if (key && v == src->map->colorkey) continue;
            if (convert) {
                Uint8 r, g, b, a;
                to_rgba(src->format, v, &r, &g, &b, &a);
                v = SDL_MapRGBA(dst->format, r, g, b, a);
            }
            put_pixel(dst, x, y, v);
        }
    }
    if (dstrect != NULL) *dstrect = cr;
    return 0;
}

SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags)
{
    SDL_Surface *d;
    SDL_Rect r;
    int key;
    (void)flags;
    if (src == NULL) {
        SDL_SetError("no surface");
        return NULL;
    }
    d = new_surface(src->w, src->h, pixel_format, NULL, 0);
    if (d == NULL) return NULL;
    if (pixel_format == SDL_PIXELFORMAT_INDEX8 && src->format->palette != NULL) {
        SDL_SetPaletteColors(d->format->palette, src->format->palette->colors, 0, src->format->palette->ncolors);
    }
    key = src->map->has_colorkey;
    src->map->has_colorkey = 0;
    r.x = r.y = 0;
    r.w = src->w;
    r.h = src->h;
    blit_rect(src, &r, d, &r);
    src->map->has_colorkey = key;
    if (key) {
        if (d->format->Amask != 0) {
            /* the key becomes transparent pixels, as in SDL */
            int x, y;
            for (y = 0; y < src->h; y++) {
                for (x = 0; x < src->w; x++) {
                    if (get_pixel(src, x, y) == src->map->colorkey) put_pixel(d, x, y, 0);
                }
            }
        } else {
            Uint8 cr, cg, cb, ca;
            to_rgba(src->format, src->map->colorkey, &cr, &cg, &cb, &ca);
            SDL_SetColorKey(d, 1, SDL_MapRGBA(d->format, cr, cg, cb, ca));
        }
    }
    return d;
}
