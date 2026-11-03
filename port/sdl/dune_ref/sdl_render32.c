/* Mini-SDL2: the software renderer. Textures are ABGR8888 in fast RAM; the
   window's back buffer is a texture of the logical size, sent to the
   display by SDL_RenderPresent (sdl_video_amiga.c) with the rectangle
   that changed since the last present.

   This is the reference renderer: SDL 2's blending rules, plain C. Faster
   paths come later, each checked against it. */
#include <stdio.h>
#include <SDL.h>
#include "msdl_internal.h"

struct SDL_Texture {
    int w, h;
    int pitch;              /* bytes */
    int access;
    Uint32 *pixels;
    SDL_BlendMode blend;
    Uint8 alphamod;
    Uint8 r_mod, g_mod, b_mod;
};

struct SDL_Renderer {
    SDL_Window *window;
    SDL_Texture *back;      /* the window, logical size */
    SDL_Texture *target;    /* NULL: back */
    Uint8 r, g, b, a;
    SDL_BlendMode draw_blend;
    int logical_w, logical_h;
    /* changed part of back since the last present */
    int dirty, dx0, dy0, dx1, dy1;
};

extern void msdl_window_size(SDL_Window *window, int *w, int *h);

/* ---- textures ---- */
SDL_Texture *texture_new(int w, int h, int access)
{
    SDL_Texture *t;
    if (w <= 0 || h <= 0) {
        SDL_SetError("texture size %dx%d", w, h);
        return NULL;
    }
    t = (SDL_Texture *)calloc(1, sizeof(SDL_Texture));
    if (t == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    t->w = w;
    t->h = h;
    t->pitch = w * 4;
    t->access = access;
    t->pixels = (Uint32 *)calloc((size_t)w * h, 4);
    if (t->pixels == NULL) {
        free(t);
        SDL_SetError("out of memory");
        return NULL;
    }
    t->blend = SDL_BLENDMODE_NONE;
    t->alphamod = 255;
    t->r_mod = t->g_mod = t->b_mod = 255;
    return t;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
    (void)renderer;
    if (format != SDL_PIXELFORMAT_ABGR8888) {
        SDL_SetError("SDL_CreateTexture(): only ABGR8888");
        return NULL;
    }
    return texture_new(w, h, access);
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
        Uint32 lut[256];
        const SDL_Palette *pal = surface->format->palette;
        for (x = 0; x < 256; x++) {
            if (pal != NULL && x < pal->ncolors) {
                const SDL_Color *c = &pal->colors[x];
                lut[x] = (Uint32)c->r | ((Uint32)c->g << 8) | ((Uint32)c->b << 16) | ((Uint32)c->a << 24);
            } else {
                lut[x] = 0xFF000000u;
            }
        }
        if (surface->has_colorkey) lut[surface->colorkey & 0xFF] &= 0x00FFFFFFu;
        for (y = 0; y < surface->h; y++) {
            const Uint8 *s = (const Uint8 *)surface->pixels + y * surface->pitch;
            Uint32 *d = t->pixels + y * t->w;
            for (x = 0; x < surface->w; x++) d[x] = lut[s[x]];
        }
    } else {
        int has_alpha = surface->format->Amask != 0;
        for (y = 0; y < surface->h; y++) {
            const Uint32 *s = (const Uint32 *)((const Uint8 *)surface->pixels + y * surface->pitch);
            Uint32 *d = t->pixels + y * t->w;
            for (x = 0; x < surface->w; x++) {
                Uint32 p = s[x];
                if (!has_alpha) p |= 0xFF000000u;
                if (surface->has_colorkey && (p & 0x00FFFFFFu) == (surface->colorkey & 0x00FFFFFFu)) {
                    p &= 0x00FFFFFFu;
                }
                d[x] = p;
            }
        }
    }
    t->alphamod = surface->alphamod;
    t->blend = surface->has_colorkey ? SDL_BLENDMODE_BLEND : surface->blendmode;
    return t;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture == NULL) return;
    free(texture->pixels);
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
    int y;
    if (texture == NULL) return SDL_SetError("no texture");
    if (rect == NULL) {
        r.x = r.y = 0;
        r.w = texture->w;
        r.h = texture->h;
    } else {
        r = *rect;
    }
    for (y = 0; y < r.h; y++) {
        memcpy(texture->pixels + (r.y + y) * texture->w + r.x,
               (const Uint8 *)pixels + y * pitch, r.w * 4);
    }
    return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
    int x = rect != NULL ? rect->x : 0, y = rect != NULL ? rect->y : 0;
    *pixels = texture->pixels + y * texture->w + x;
    *pitch = texture->pitch;
    return 0;
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
    if (texture == NULL) return SDL_SetError("no texture");
    texture->r_mod = r;
    texture->g_mod = g;
    texture->b_mod = b;
    return 0;
}

/* ---- renderer ---- */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    SDL_Renderer *r;
    int w, h;
    (void)index;
    (void)flags;
    r = (SDL_Renderer *)calloc(1, sizeof(SDL_Renderer));
    if (r == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    msdl_window_size(window, &w, &h);
    r->window = window;
    r->logical_w = w;
    r->logical_h = h;
    r->back = texture_new(w, h, SDL_TEXTUREACCESS_TARGET);
    if (r->back == NULL) {
        free(r);
        return NULL;
    }
    r->a = 255;
    r->dirty = 1;
    r->dx0 = r->dy0 = 0;
    r->dx1 = w;
    r->dy1 = h;
    return r;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (renderer == NULL) return;
    SDL_DestroyTexture(renderer->back);
    free(renderer);
}

int SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info)
{
    (void)renderer;
    memset(info, 0, sizeof(*info));
    info->name = "warp-software";
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

/* The back buffer has the logical size; sdl_video_amiga.c scales it to the
   window when they differ. */
int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
    if (renderer->back->w != w || renderer->back->h != h) {
        SDL_Texture *nb = texture_new(w, h, SDL_TEXTUREACCESS_TARGET);
        if (nb == NULL) return -1;
        if (renderer->target == renderer->back) renderer->target = NULL;
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

SDL_Texture *cur_target(SDL_Renderer *renderer)
{
    return renderer->target != NULL ? renderer->target : renderer->back;
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

/* One pixel of src over dst with SDL's blend modes (src alpha already
   multiplied by the alpha modulation). */
static inline Uint32 blend_one(Uint32 s, Uint32 d, unsigned a, SDL_BlendMode mode)
{
    unsigned ia;
    Uint32 rb, g, da;
    switch (mode) {
        case SDL_BLENDMODE_NONE:
            return s;
        case SDL_BLENDMODE_BLEND:
            if (a == 255) return s | 0xFF000000u;
            if (a == 0) return d;
            ia = 255 - a;
            rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia + 0x00800080u) >> 8) & 0x00FF00FFu;
            g = (((s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia + 0x00008000u) >> 8) & 0x0000FF00u;
            da = a + (((d >> 24) * ia * 257 + 32768) >> 16);
            if (da > 255) da = 255;
            return rb | g | (da << 24);
        case SDL_BLENDMODE_ADD: {
            unsigned r = (d & 0xFF) + (((s & 0xFF) * a) >> 8);
            unsigned gg = ((d >> 8) & 0xFF) + ((((s >> 8) & 0xFF) * a) >> 8);
            unsigned b = ((d >> 16) & 0xFF) + ((((s >> 16) & 0xFF) * a) >> 8);
            if (r > 255) r = 255;
            if (gg > 255) gg = 255;
            if (b > 255) b = 255;
            return (d & 0xFF000000u) | r | (gg << 8) | (b << 16);
        }
        case SDL_BLENDMODE_MOD:
        default: {
            unsigned r = ((d & 0xFF) * (s & 0xFF)) / 255;
            unsigned gg = (((d >> 8) & 0xFF) * ((s >> 8) & 0xFF)) / 255;
            unsigned b = (((d >> 16) & 0xFF) * ((s >> 16) & 0xFF)) / 255;
            return (d & 0xFF000000u) | r | (gg << 8) | (b << 16);
        }
    }
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    SDL_Texture *t = cur_target(renderer);
    Uint32 c = (Uint32)renderer->r | ((Uint32)renderer->g << 8) | ((Uint32)renderer->b << 16) | ((Uint32)renderer->a << 24);
    size_t i, n = (size_t)t->w * t->h;
    SDL_Rect all;
    Uint32 *p = t->pixels;
    for (i = 0; i < n; i++) p[i] = c;
    all.x = all.y = 0;
    all.w = t->w;
    all.h = t->h;
    mark(renderer, &all);
    return 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    SDL_Texture *t = cur_target(renderer);
    SDL_Rect sr, dr, clip, cd;
    int x, y;
    Uint32 xstep, ystep, sx0, sy;
    unsigned amod;
    SDL_BlendMode mode;

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
    if (dstrect != NULL) {
        dr = *dstrect;
    } else {
        dr.x = dr.y = 0;
        dr.w = t->w;
        dr.h = t->h;
    }
    if (dr.w <= 0 || dr.h <= 0) return 0;
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (!SDL_IntersectRect(&dr, &clip, &cd)) return 0;

    /* 16.16 steps from destination to source (nearest pixel) */
    xstep = ((Uint32)sr.w << 16) / (Uint32)dr.w;
    ystep = ((Uint32)sr.h << 16) / (Uint32)dr.h;
    sx0 = (Uint32)(cd.x - dr.x) * xstep;
    sy = (Uint32)(cd.y - dr.y) * ystep;
    amod = texture->alphamod;
    mode = texture->blend;

    if (texture == t) {
        /* copying a texture onto itself (shimmer effects): work from a copy */
        static Uint32 *tmp;
        static size_t tmp_n;
        size_t n = (size_t)texture->w * texture->h;
        if (tmp_n < n) {
            free(tmp);
            tmp = (Uint32 *)malloc(n * 4);
            tmp_n = tmp != NULL ? n : 0;
            if (tmp == NULL) return SDL_SetError("out of memory");
        }
        memcpy(tmp, texture->pixels, n * 4);
        for (y = 0; y < cd.h; y++, sy += ystep) {
            const Uint32 *srow = tmp + (sr.y + (sy >> 16)) * texture->w + sr.x;
            Uint32 *d = t->pixels + (cd.y + y) * t->w + cd.x;
            Uint32 sx = sx0;
            for (x = 0; x < cd.w; x++, sx += xstep) {
                Uint32 s = srow[sx >> 16];
                unsigned a = s >> 24;
                if (amod != 255) a = (a * amod + 127) / 255;
                d[x] = blend_one(s, d[x], a, mode);
            }
        }
        mark(renderer, &cd);
        return 0;
    }

    for (y = 0; y < cd.h; y++, sy += ystep) {
        const Uint32 *srow = texture->pixels + (sr.y + (sy >> 16)) * texture->w + sr.x;
        Uint32 *d = t->pixels + (cd.y + y) * t->w + cd.x;
        Uint32 sx = sx0;
        if (mode == SDL_BLENDMODE_NONE && xstep == 0x10000u) {
            memcpy(d, srow + (sx >> 16), cd.w * 4);
            continue;
        }
        for (x = 0; x < cd.w; x++, sx += xstep) {
            Uint32 s = srow[sx >> 16];
            unsigned a = s >> 24;
            if (mode == SDL_BLENDMODE_BLEND && a == 0) continue;
            if (amod != 255) a = (a * amod + 127) / 255;
            d[x] = blend_one(s, d[x], a, mode);
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
    Uint32 c = (Uint32)renderer->r | ((Uint32)renderer->g << 8) | ((Uint32)renderer->b << 16) | ((Uint32)renderer->a << 24);
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (!SDL_IntersectRect(rect, &clip, &r)) return;
    for (y = 0; y < r.h; y++) {
        Uint32 *d = t->pixels + (r.y + y) * t->w + r.x;
        if (renderer->draw_blend == SDL_BLENDMODE_NONE) {
            for (x = 0; x < r.w; x++) d[x] = c;
        } else {
            for (x = 0; x < r.w; x++) d[x] = blend_one(c, d[x], renderer->a, renderer->draw_blend);
        }
    }
    mark(renderer, &r);
}

int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    SDL_Rect all;
    if (rect == NULL) {
        SDL_Texture *t = cur_target(renderer);
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
    int y;
    if (format != SDL_PIXELFORMAT_ABGR8888 && format != 0) return SDL_SetError("SDL_RenderReadPixels(): only ABGR8888");
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (rect == NULL) r = clip;
    else if (!SDL_IntersectRect(rect, &clip, &r)) return 0;
    for (y = 0; y < r.h; y++) {
        memcpy((Uint8 *)pixels + y * pitch, t->pixels + (r.y + y) * t->w + r.x, r.w * 4);
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    SDL_Rect d;
    /* The game presents once with its screen texture as the target and once
       more after copying it to the window: only the second shows. */
    if (renderer->target != NULL) return;
    if (!renderer->dirty) {
        d.x = d.y = d.w = d.h = 0;   /* nothing changed: events and pacing only */
        msdl_video_present(renderer->back->pixels, renderer->back->pitch,
                           renderer->back->w, renderer->back->h, &d);
        return;
    }
    d.x = renderer->dx0;
    d.y = renderer->dy0;
    d.w = renderer->dx1 - renderer->dx0;
    d.h = renderer->dy1 - renderer->dy0;
    msdl_video_present(renderer->back->pixels, renderer->back->pitch,
                       renderer->back->w, renderer->back->h, &d);
    renderer->dirty = 0;
}
