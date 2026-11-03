/* Mini-SDL2: the software renderer in R5G6B5, the Workbench's format.

   The game composes every frame from scratch; at 640x480 a 32-bit frame is
   1.2 MB per pass, ~16 ms of writes on a Warp's fast RAM, and the renderer
   made several passes (clear, draw, copy, convert). Here pixels are 16-bit
   and a texture is one of:
     OPAQUE  every alpha 255;
     KEYED   alpha 0 or 255, the transparent pixels hold KEY565 (opaque pixels
             of that value are moved by one step of blue), their RGB in
             keyrgb (SDL keeps the colour of a transparent pixel, a NONE copy
             shows it);
     ALPHA   anything else, with an 8-bit alpha plane.
   Render targets get an alpha plane only when something with alpha < 255
   is written into them (a_valid).

   Blending follows SDL 2 on 8-bit channels (565 expanded and packed again),
   so results differ from the 32-bit reference (sdl_render32.c) only by the
   565 rounding. */
#include <stdio.h>
#include <SDL.h>
#include "msdl_internal.h"
#include "render_names_r16.h"

#define KEY565 0xF81Fu

enum { K_OPAQUE, K_KEYED, K_ALPHA };

struct SDL_Texture {
    int w, h;
    int access;
    Uint16 *px;
    Uint8 *a;               /* alpha plane, K_ALPHA (or a target with a_valid) */
    int a_valid;            /* targets: the plane holds the alpha */
    int kind;
    Uint16 keyrgb;          /* K_KEYED: colour of the transparent pixels */
    SDL_BlendMode blend;
    Uint8 alphamod;
};

struct SDL_Renderer {
    SDL_Window *window;
    SDL_Texture *back;      /* the window, logical size */
    SDL_Texture *target;    /* NULL: back */
    Uint8 r, g, b, a;
    SDL_BlendMode draw_blend;
    int logical_w, logical_h;
    /* back is only a full copy of this texture (not made yet) */
    SDL_Texture *alias;
    /* back is to be cleared with this colour (not done yet) */
    int clear_pending;
    Uint16 clear_color;
    int dirty, dx0, dy0, dx1, dy1;
    /* MSDL_SetRenderOffset / MSDL_SetRenderClip (the terrain cache) */
    int ox, oy;
    int has_clip;
    SDL_Rect uclip;
};

static SDL_Renderer *the_renderer;

extern void msdl_window_size(SDL_Window *window, int *w, int *h);

/* ---- pixels ---- */
static inline Uint16 pack565(unsigned r, unsigned g, unsigned b)
{
    return (Uint16)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}
static inline unsigned r8(Uint16 p) { unsigned r = p >> 11; return (r << 3) | (r >> 2); }
static inline unsigned g8(Uint16 p) { unsigned g = (p >> 5) & 0x3F; return (g << 2) | (g >> 4); }
static inline unsigned b8(Uint16 p) { unsigned b = p & 0x1F; return (b << 3) | (b >> 2); }

static inline Uint16 abgr_to565(Uint32 p)
{
    return pack565(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
}

/* An opaque pixel must not look like the key. */
static inline Uint16 not_key(Uint16 p) { return p == KEY565 ? (Uint16)(KEY565 ^ 1) : p; }

/* ---- textures ---- */
static SDL_Texture *texture_new(int w, int h, int access)
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
    t->access = access;
    t->px = (Uint16 *)calloc((size_t)w * h, 2);
    if (t->px == NULL) {
        free(t);
        SDL_SetError("out of memory");
        return NULL;
    }
    t->blend = SDL_BLENDMODE_NONE;
    t->alphamod = 255;
    t->kind = K_OPAQUE;
    return t;
}

/* The alpha plane of a texture (made on demand; 255 everywhere, or 0 where
   the key is). */
static Uint8 *alpha_plane(SDL_Texture *t)
{
    size_t i, n = (size_t)t->w * t->h;
    if (t->a == NULL) {
        t->a = (Uint8 *)malloc(n);
        if (t->a == NULL) return NULL;
        t->a_valid = 0;
    }
    if (!t->a_valid || t->kind != K_ALPHA) {
        if (t->kind == K_KEYED) {
            for (i = 0; i < n; i++) {
                if (t->px[i] == KEY565) {
                    t->a[i] = 0;
                    t->px[i] = t->keyrgb;
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

/* Fills a texture from ABGR8888 pixels and works out its kind. */
static void load_abgr(SDL_Texture *t, const Uint32 *src, int src_pitch_px)
{
    int x, y, keyed = 1, opaque = 1, have_key = 0;
    Uint16 krgb = 0;
    for (y = 0; y < t->h; y++) {
        const Uint32 *s = src + y * src_pitch_px;
        for (x = 0; x < t->w; x++) {
            unsigned a = s[x] >> 24;
            if (a == 255) continue;
            opaque = 0;
            if (a != 0) {
                keyed = 0;
            } else {
                Uint16 c = abgr_to565(s[x]);
                if (!have_key) {
                    have_key = 1;
                    krgb = c;
                } else if (c != krgb) {
                    keyed = 0;
                }
            }
        }
    }
    if (opaque) {
        t->kind = K_OPAQUE;
        for (y = 0; y < t->h; y++) {
            const Uint32 *s = src + y * src_pitch_px;
            Uint16 *d = t->px + y * t->w;
            for (x = 0; x < t->w; x++) d[x] = abgr_to565(s[x]);
        }
    } else if (keyed) {
        t->kind = K_KEYED;
        t->keyrgb = krgb;
        for (y = 0; y < t->h; y++) {
            const Uint32 *s = src + y * src_pitch_px;
            Uint16 *d = t->px + y * t->w;
            for (x = 0; x < t->w; x++) d[x] = (s[x] >> 24) ? not_key(abgr_to565(s[x])) : KEY565;
        }
    } else {
        Uint8 *a;
        t->kind = K_ALPHA;
        if (t->a == NULL) t->a = (Uint8 *)malloc((size_t)t->w * t->h);
        a = t->a;
        t->a_valid = a != NULL;
        for (y = 0; y < t->h; y++) {
            const Uint32 *s = src + y * src_pitch_px;
            Uint16 *d = t->px + y * t->w;
            for (x = 0; x < t->w; x++) {
                d[x] = abgr_to565(s[x]);
                if (a != NULL) a[y * t->w + x] = (Uint8)(s[x] >> 24);
            }
        }
    }
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
    SDL_Texture *t;
    (void)renderer;
    if (format != SDL_PIXELFORMAT_ABGR8888) {
        SDL_SetError("SDL_CreateTexture(): only ABGR8888");
        return NULL;
    }
    t = texture_new(w, h, access);
    /* SDL: a new texture is transparent black */
    if (t != NULL && alpha_plane(t) != NULL) memset(t->a, 0, (size_t)w * h);
    return t;
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
    SDL_Texture *t;
    Uint32 *tmp;
    int x, y;
    (void)renderer;
    if (surface == NULL) {
        SDL_SetError("no surface");
        return NULL;
    }
    t = texture_new(surface->w, surface->h, SDL_TEXTUREACCESS_STATIC);
    if (t == NULL) return NULL;
    tmp = (Uint32 *)malloc((size_t)surface->w * surface->h * 4);
    if (tmp == NULL) {
        SDL_DestroyTexture(t);
        SDL_SetError("out of memory");
        return NULL;
    }

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
            Uint32 *d = tmp + y * surface->w;
            for (x = 0; x < surface->w; x++) d[x] = lut[s[x]];
        }
    } else {
        int has_alpha = surface->format->Amask != 0;
        for (y = 0; y < surface->h; y++) {
            const Uint32 *s = (const Uint32 *)((const Uint8 *)surface->pixels + y * surface->pitch);
            Uint32 *d = tmp + y * surface->w;
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
    load_abgr(t, tmp, surface->w);
    free(tmp);
    t->alphamod = surface->alphamod;
    t->blend = surface->has_colorkey ? SDL_BLENDMODE_BLEND : surface->blendmode;
    return t;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture == NULL) return;
    if (the_renderer != NULL) {
        if (the_renderer->alias == texture) {
            memcpy(the_renderer->back->px, texture->px, (size_t)texture->w * texture->h * 2);
            the_renderer->alias = NULL;
        }
        if (the_renderer->target == texture) the_renderer->target = NULL;
    }
    free(texture->px);
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

static void back_materialize(SDL_Renderer *r);

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    SDL_Rect r;
    int x, y, opaque = 1;
    if (texture == NULL) return SDL_SetError("no texture");
    if (the_renderer != NULL && the_renderer->alias == texture) back_materialize(the_renderer);
    if (rect == NULL) {
        r.x = r.y = 0;
        r.w = texture->w;
        r.h = texture->h;
    } else {
        r = *rect;
    }
    if (r.x == 0 && r.y == 0 && r.w == texture->w && r.h == texture->h && pitch % 4 == 0) {
        load_abgr(texture, (const Uint32 *)pixels, pitch / 4);
        return 0;
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
    for (y = 0; y < r.h; y++) {
        const Uint32 *s = (const Uint32 *)((const Uint8 *)pixels + y * pitch);
        Uint16 *d = texture->px + (r.y + y) * texture->w + r.x;
        for (x = 0; x < r.w; x++) d[x] = abgr_to565(s[x]);
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
    info->name = "warp-565";
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
        SDL_Texture *nb = texture_new(w, h, SDL_TEXTUREACCESS_TARGET);
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

static inline void fill16(Uint16 *d, Uint16 c, int n);

static void back_materialize(SDL_Renderer *r)
{
    if (r->alias != NULL) {
        memcpy(r->back->px, r->alias->px, (size_t)r->back->w * r->back->h * 2);
        r->alias = NULL;
    } else if (r->clear_pending) {
        int y;
        for (y = 0; y < r->back->h; y++) fill16(r->back->px + y * r->back->w, r->clear_color, r->back->w);
    }
    r->clear_pending = 0;
}

/* The texture drawn into; back is made real first. A texture that is the
   source of back's alias and is drawn into makes back real too. */
static SDL_Texture *cur_target(SDL_Renderer *r)
{
    if (r->target == NULL) {
        back_materialize(r);
        return r->back;
    }
    if (r->alias == r->target) back_materialize(r);
    return r->target;
}

static void mark(SDL_Renderer *renderer, const SDL_Rect *r)
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

static inline void fill16(Uint16 *d, Uint16 c, int n)
{
    if (n > 0 && ((size_t)d & 2)) {
        *d++ = c;
        n--;
    }
    {
        Uint32 cc = ((Uint32)c << 16) | c;
        Uint32 *l = (Uint32 *)d;
        int m = n >> 1;
        while (m >= 4) {
            l[0] = cc; l[1] = cc; l[2] = cc; l[3] = cc;
            l += 4;
            m -= 4;
        }
        while (m-- > 0) *l++ = cc;
        if (n & 1) *(Uint16 *)l = c;
    }
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    SDL_Texture *t;
    Uint16 c = pack565(renderer->r, renderer->g, renderer->b);
    SDL_Rect all;
    int y;
    if (renderer->target == NULL) {
        /* the window: only noted, the game copies its screen texture over
           it right after */
        renderer->alias = NULL;
        renderer->clear_pending = 1;
        renderer->clear_color = c;
        all.x = all.y = 0;
        all.w = renderer->back->w;
        all.h = renderer->back->h;
        mark(renderer, &all);
        return 0;
    }
    t = cur_target(renderer);
    for (y = 0; y < t->h; y++) fill16(t->px + y * t->w, c, t->w);
    if (renderer->a == 255) {
        if (t->kind != K_ALPHA || t->a == NULL) t->kind = K_OPAQUE;
        else t->a_valid = 0, t->kind = K_OPAQUE;
    } else {
        Uint8 *a;
        t->kind = K_OPAQUE;
        t->a_valid = 0;
        a = alpha_plane(t);
        if (a != NULL) memset(a, renderer->a, (size_t)t->w * t->h);
    }
    all.x = all.y = 0;
    all.w = t->w;
    all.h = t->h;
    mark(renderer, &all);
    return 0;
}

/* General per-pixel blend in 8-bit channels (SDL's rules). */
static inline Uint16 blend565(Uint16 s, Uint16 d, unsigned a, SDL_BlendMode mode)
{
    unsigned ia, r, g, b;
    switch (mode) {
        case SDL_BLENDMODE_BLEND:
            if (a == 255) return s;
            ia = 255 - a;
            r = (r8(s) * a + r8(d) * ia + 128) >> 8;
            g = (g8(s) * a + g8(d) * ia + 128) >> 8;
            b = (b8(s) * a + b8(d) * ia + 128) >> 8;
            return pack565(r, g, b);
        case SDL_BLENDMODE_ADD:
            r = r8(d) + ((r8(s) * a) >> 8);
            g = g8(d) + ((g8(s) * a) >> 8);
            b = b8(d) + ((b8(s) * a) >> 8);
            return pack565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
        case SDL_BLENDMODE_MOD:
            return pack565(r8(s) * r8(d) / 255, g8(s) * g8(d) / 255, b8(s) * b8(d) / 255);
        default:
            return s;
    }
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
    static Uint16 *tmp;
    static size_t tmp_n;
    const Uint16 *spx;

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

    /* the whole of a texture onto the whole window, plain copy: back just
       becomes an alias of it (the game's screen texture, every frame) */
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
    spx = texture->px;

    if (texture == t) {
        /* onto itself (shimmer effects): from a copy */
        size_t n = (size_t)texture->w * texture->h;
        if (tmp_n < n) {
            free(tmp);
            tmp = (Uint16 *)malloc(n * 2);
            tmp_n = tmp != NULL ? n : 0;
            if (tmp == NULL) return SDL_SetError("out of memory");
        }
        memcpy(tmp, texture->px, n * 2);
        spx = tmp;
    }

    /* keyed into a keyed target (the shroud cache): the pixels as they are */
    if (t->kind == K_KEYED && kind == K_KEYED && mode == SDL_BLENDMODE_NONE && texture != t &&
        xstep == 0x10000u && ystep == 0x10000u && amod == 255) {
        for (y = 0; y < cd.h; y++) {
            memcpy(t->px + (cd.y + y) * t->w + cd.x,
                   spx + (sr.y + (sy >> 16) + y) * texture->w + sr.x + (sx0 >> 16), cd.w * 2);
        }
        return 0;
    }

    /* the target's alpha matters only if it has a plane already, or when a
       NONE copy brings transparent pixels into it */
    if (mode == SDL_BLENDMODE_NONE && kind != K_OPAQUE && !(t == renderer->back)) alpha_plane(t);
    da = (t->kind == K_ALPHA && t->a != NULL && t->a_valid) ? t->a : NULL;
    if (mode == SDL_BLENDMODE_NONE && kind != K_OPAQUE && da == NULL && t != renderer->back) da = alpha_plane(t);

    /* fast paths: 1:1, no alpha modulation */
    plain = xstep == 0x10000u && ystep == 0x10000u && amod == 255 && da == NULL;
    if (plain && (kind == K_OPAQUE && (mode == SDL_BLENDMODE_NONE || mode == SDL_BLENDMODE_BLEND))) {
        for (y = 0; y < cd.h; y++) {
            memcpy(t->px + (cd.y + y) * t->w + cd.x,
                   spx + (sr.y + (sy >> 16) + y) * texture->w + sr.x + (sx0 >> 16), cd.w * 2);
        }
        mark(renderer, &cd);
        return 0;
    }
    if (plain && kind == K_KEYED && mode == SDL_BLENDMODE_BLEND) {
        for (y = 0; y < cd.h; y++) {
            const Uint16 *s = spx + (sr.y + (sy >> 16) + y) * texture->w + sr.x + (sx0 >> 16);
            Uint16 *d = t->px + (cd.y + y) * t->w + cd.x;
            for (x = 0; x < cd.w; x++) {
                Uint16 p = s[x];
                if (p != KEY565) d[x] = p;
            }
        }
        mark(renderer, &cd);
        return 0;
    }
    if (plain && kind == K_KEYED && mode == SDL_BLENDMODE_NONE) {
        /* the back buffer has no alpha: the transparent pixels show their RGB */
        Uint16 kr = texture->keyrgb;
        for (y = 0; y < cd.h; y++) {
            const Uint16 *s = spx + (sr.y + (sy >> 16) + y) * texture->w + sr.x + (sx0 >> 16);
            Uint16 *d = t->px + (cd.y + y) * t->w + cd.x;
            for (x = 0; x < cd.w; x++) {
                Uint16 p = s[x];
                d[x] = p == KEY565 ? kr : p;
            }
        }
        mark(renderer, &cd);
        return 0;
    }

    /* general path */
    {
        const Uint8 *sa = (kind == K_ALPHA) ? texture->a : NULL;
        Uint16 kr = texture->keyrgb;
        for (y = 0; y < cd.h; y++, sy += ystep) {
            int srow = (sr.y + (sy >> 16)) * texture->w + sr.x;
            Uint16 *d = t->px + (cd.y + y) * t->w + cd.x;
            Uint8 *dal = da != NULL ? da + (cd.y + y) * t->w + cd.x : NULL;
            Uint32 sx = sx0;
            for (x = 0; x < cd.w; x++, sx += xstep) {
                int si = srow + (sx >> 16);
                Uint16 s = spx[si];
                unsigned a;
                if (kind == K_KEYED) {
                    if (s == KEY565) {
                        a = 0;
                        s = kr;
                    } else {
                        a = 255;
                    }
                } else if (sa != NULL) {
                    a = sa[si];
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
                        d[x] = blend565(s, d[x], a, mode);
                        if (dal != NULL) dal[x] = (Uint8)(a + ((dal[x] * (255 - a) * 257 + 32768) >> 16));
                        break;
                    default:
                        d[x] = blend565(s, d[x], a, mode);
                        break;
                }
            }
        }
    }
    mark(renderer, &cd);
    return 0;
}

static void fill(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    SDL_Texture *t = cur_target(renderer);
    SDL_Rect clip, r;
    int x, y;
    Uint16 c = pack565(renderer->r, renderer->g, renderer->b);
    unsigned a = renderer->a;
    SDL_Rect moved = *rect;
    moved.x += renderer->ox;
    moved.y += renderer->oy;
    clip.x = clip.y = 0;
    clip.w = t->w;
    clip.h = t->h;
    if (renderer->has_clip && !SDL_IntersectRect(&clip, &renderer->uclip, &clip)) return;
    if (!SDL_IntersectRect(&moved, &clip, &r)) return;
    if (t->kind == K_KEYED && renderer->draw_blend == SDL_BLENDMODE_NONE && a == 0) {
        for (y = 0; y < r.h; y++) fill16(t->px + (r.y + y) * t->w + r.x, KEY565, r.w);
        return;
    }
    for (y = 0; y < r.h; y++) {
        Uint16 *d = t->px + (r.y + y) * t->w + r.x;
        if (renderer->draw_blend == SDL_BLENDMODE_NONE || a == 255) {
            fill16(d, c, r.w);
        } else {
            for (x = 0; x < r.w; x++) d[x] = blend565(c, d[x], a, renderer->draw_blend);
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
    for (y = 0; y < r.h; y++) {
        Uint32 *d = (Uint32 *)((Uint8 *)pixels + y * pitch);
        const Uint16 *s = t->px + (r.y + y) * t->w + r.x;
        for (x = 0; x < r.w; x++) {
            unsigned a = 255;
            Uint16 p = s[x];
            if (t->kind == K_KEYED && p == KEY565) {
                a = 0;
                p = t->keyrgb;
            } else if (t->kind == K_ALPHA && t->a != NULL) {
                a = t->a[(r.y + y) * t->w + r.x + x];
            }
            d[x] = r8(p) | (g8(p) << 8) | (b8(p) << 16) | ((Uint32)a << 24);
        }
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    SDL_Rect d;
    const Uint16 *px;
    /* The game presents once with its screen texture as the target and once
       more after copying it to the window: only the second shows. */
    if (renderer->target != NULL) return;
    if (renderer->clear_pending) back_materialize(renderer);
    px = renderer->alias != NULL ? renderer->alias->px : renderer->back->px;
    msdl_fps_tick();
    if (msdl_fps_on) {
        msdl_fps_draw16((Uint16 *)px, renderer->back->w * 2, renderer->back->w, renderer->back->h);
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
    msdl_video_present16(px, renderer->back->w * 2, renderer->back->w, renderer->back->h, &d);
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
   so that copying it with BLEND takes the fast path. */
int SDL_MakeTextureKeyedX(SDL_Texture *t)
{
    size_t i, n = (size_t)t->w * t->h;
    int have = 0;
    Uint16 kr = 0;
    if (t->kind != K_ALPHA || t->a == NULL || !t->a_valid) return t->kind == K_KEYED || t->kind == K_OPAQUE;
    for (i = 0; i < n; i++) {
        Uint8 a = t->a[i];
        if (a != 0 && a != 255) return 0;
    }
    for (i = 0; i < n; i++) {
        if (t->a[i] == 0) {
            if (!have) { have = 1; kr = t->px[i]; }
            t->px[i] = KEY565;
        } else {
            t->px[i] = not_key(t->px[i]);
        }
    }
    t->kind = K_KEYED;
    t->keyrgb = kr;
    t->a_valid = 0;
    return 1;
}

int SDL_SetTextureKeyLikeX(SDL_Texture *t, SDL_Texture *src)
{
    size_t i, n = (size_t)t->w * t->h;
    if (src == NULL || src->kind != K_KEYED) return 0;
    t->kind = K_KEYED;
    t->keyrgb = src->keyrgb;
    t->a_valid = 0;
    for (i = 0; i < n; i++) t->px[i] = KEY565;
    return 1;
}
