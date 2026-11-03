/* Mini-SDL2 for Dune Legacy on the Amiga (Warp RTG).
   Only the subset of the SDL 2 API the game uses, with SDL's semantics.
   Pixel formats: 8-bit indexed and 32-bit ABGR8888 (R in the low byte of
   the native Uint32, as Colors.h expects). */
#ifndef MINI_SDL_H
#define MINI_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef unsigned long Uint32;  /* long, as in NovaCoder's SDL: the game overloads int vs Sint32 */
typedef long Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_BIG_ENDIAN

#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u

#define SDL_DISABLE 0
#define SDL_ENABLE  1

/* ---- errors, log, memory ---- */
const char *SDL_GetError(void);
int SDL_SetError(const char *fmt, ...);

typedef enum {
    SDL_LOG_PRIORITY_VERBOSE = 1, SDL_LOG_PRIORITY_DEBUG, SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_CRITICAL,
    SDL_NUM_LOG_PRIORITIES
} SDL_LogPriority;
enum { SDL_LOG_CATEGORY_APPLICATION = 0 };
typedef void (*SDL_LogOutputFunction)(void *userdata, int category, SDL_LogPriority priority, const char *message);
void SDL_Log(const char *fmt, ...);
void SDL_LogSetOutputFunction(SDL_LogOutputFunction callback, void *userdata);
void SDL_LogSetAllPriority(SDL_LogPriority priority);
void SDL_LogSetPriority(int category, SDL_LogPriority priority);

#define SDL_malloc  malloc
#define SDL_calloc  calloc
#define SDL_realloc realloc
#define SDL_free    free

/* ---- init, version, platform ---- */
int SDL_Init(Uint32 flags);
void SDL_Quit(void);
typedef struct SDL_version { Uint8 major, minor, patch; } SDL_version;
#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    5
#define SDL_VERSION(x) do { (x)->major = SDL_MAJOR_VERSION; (x)->minor = SDL_MINOR_VERSION; (x)->patch = SDL_PATCHLEVEL; } while (0)
void SDL_GetVersion(SDL_version *ver);
const char *SDL_GetPlatform(void);
char *SDL_GetBasePath(void);

/* ---- time, threads, atomics ---- */
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_WaitThread(SDL_Thread *thread, int *status);
typedef struct SDL_atomic_t { volatile int value; } SDL_atomic_t;
int SDL_AtomicSet(SDL_atomic_t *a, int v);
int SDL_AtomicGet(SDL_atomic_t *a);

/* ---- byte order ---- */
static inline Uint16 SDL_Swap16(Uint16 x) { return (Uint16)((x << 8) | (x >> 8)); }
static inline Uint32 SDL_Swap32(Uint32 x) {
    return (x << 24) | ((x << 8) & 0x00FF0000u) | ((x >> 8) & 0x0000FF00u) | (x >> 24);
}
static inline Uint64 SDL_Swap64(Uint64 x) {
    return ((Uint64)SDL_Swap32((Uint32)x) << 32) | SDL_Swap32((Uint32)(x >> 32));
}
#define SDL_SwapLE16(x) SDL_Swap16(x)
#define SDL_SwapLE32(x) SDL_Swap32(x)
#define SDL_SwapLE64(x) SDL_Swap64(x)
#define SDL_SwapBE16(x) (x)
#define SDL_SwapBE32(x) (x)
#define SDL_SwapBE64(x) (x)

/* ---- RWops ---- */
#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2
typedef struct SDL_RWops {
    Sint64 (*size)(struct SDL_RWops *ctx);
    Sint64 (*seek)(struct SDL_RWops *ctx, Sint64 offset, int whence);
    size_t (*read)(struct SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum);
    size_t (*write)(struct SDL_RWops *ctx, const void *ptr, size_t size, size_t num);
    int (*close)(struct SDL_RWops *ctx);
    Uint32 type;
    union {
        struct { void *fp; } stdio;
        struct { Uint8 *base, *here, *stop; } mem;
        struct { void *data1, *data2; } unknown;
    } hidden;
} SDL_RWops;
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
SDL_RWops *SDL_AllocRW(void);
void SDL_FreeRW(SDL_RWops *area);
#define SDL_RWsize(ctx)          (ctx)->size(ctx)
#define SDL_RWseek(ctx, o, w)    (ctx)->seek(ctx, o, w)
#define SDL_RWtell(ctx)          (ctx)->seek(ctx, 0, RW_SEEK_CUR)
#define SDL_RWread(ctx, p, s, n) (ctx)->read(ctx, p, s, n)
#define SDL_RWwrite(ctx, p, s, n) (ctx)->write(ctx, p, s, n)
#define SDL_RWclose(ctx)         (ctx)->close(ctx)

/* ---- pixels ---- */
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
    Uint32 version;
    int refcount;
} SDL_Palette;

#define SDL_PIXELFORMAT_UNKNOWN  0u
#define SDL_PIXELFORMAT_INDEX8   0x13000801u
#define SDL_PIXELFORMAT_ABGR8888 0x16762004u
#define SDL_PIXELFORMAT_RGB565   0x15151002u

typedef struct SDL_PixelFormat {
    Uint32 format;
    SDL_Palette *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 padding[2];
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint8 Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    int refcount;
    struct SDL_PixelFormat *next;
} SDL_PixelFormat;

SDL_Palette *SDL_AllocPalette(int ncolors);
void SDL_FreePalette(SDL_Palette *palette);
int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
void SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormat *format, Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a);

/* ---- rects ---- */
typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
SDL_bool SDL_IntersectRect(const SDL_Rect *A, const SDL_Rect *B, SDL_Rect *result);
static inline SDL_bool SDL_PointInRect(const SDL_Point *p, const SDL_Rect *r) {
    return (p->x >= r->x && p->x < r->x + r->w && p->y >= r->y && p->y < r->y + r->h) ? SDL_TRUE : SDL_FALSE;
}

/* ---- blend modes ---- */
typedef enum {
    SDL_BLENDMODE_NONE = 0, SDL_BLENDMODE_BLEND = 1, SDL_BLENDMODE_ADD = 2, SDL_BLENDMODE_MOD = 4
} SDL_BlendMode;

/* ---- surfaces ---- */
#define SDL_SWSURFACE 0
#define SDL_PREALLOC  0x00000001u
#define SDL_RLEACCEL  0x00000002u
#define SDL_DONTFREE  0x00000004u

typedef struct SDL_BlitMap SDL_BlitMap;
typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    SDL_BlitMap *map;
    int refcount;
    /* mini-SDL state */
    int has_colorkey;
    Uint32 colorkey;
    SDL_BlendMode blendmode;
    Uint8 alphamod;
} SDL_Surface;

#define SDL_MUSTLOCK(s) 0

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int SDL_GetColorKey(SDL_Surface *surface, Uint32 *key);
SDL_bool SDL_HasColorKey(SDL_Surface *surface);
int SDL_SetSurfaceRLE(SDL_Surface *surface, int flag);
int SDL_SetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode blendMode);
int SDL_GetSurfaceBlendMode(SDL_Surface *surface, SDL_BlendMode *blendMode);
int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha);
SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
#define SDL_BlitSurface SDL_UpperBlit
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface *SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags);

/* ---- video ---- */
typedef struct SDL_DisplayMode {
    Uint32 format;
    int w, h;
    int refresh_rate;
    void *driverdata;
} SDL_DisplayMode;

typedef struct SDL_Window SDL_Window;
#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_SHOWN              0x00000004u
#define SDL_WINDOW_INPUT_FOCUS        0x00000200u
#define SDL_WINDOW_MOUSE_FOCUS        0x00000400u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_WINDOWPOS_CENTERED        0x2FFF0000u

int SDL_GetNumDisplayModes(int displayIndex);
int SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode);
int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode);
SDL_DisplayMode *SDL_GetClosestDisplayMode(int displayIndex, const SDL_DisplayMode *mode, SDL_DisplayMode *closest);
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
Uint32 SDL_GetWindowFlags(SDL_Window *window);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);
void SDL_SetWindowSize(SDL_Window *window, int w, int h);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);
void SDL_MinimizeWindow(SDL_Window *window);
int SDL_ShowCursor(int toggle);

#define SDL_MESSAGEBOX_ERROR       0x10
#define SDL_MESSAGEBOX_WARNING     0x20
#define SDL_MESSAGEBOX_INFORMATION 0x40
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window);

/* ---- renderer ---- */
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
#define SDL_RENDERER_SOFTWARE      0x00000001u
#define SDL_RENDERER_ACCELERATED   0x00000002u
#define SDL_RENDERER_PRESENTVSYNC  0x00000004u
#define SDL_RENDERER_TARGETTEXTURE 0x00000008u
typedef struct SDL_RendererInfo {
    const char *name;
    Uint32 flags;
    Uint32 num_texture_formats;
    Uint32 texture_formats[16];
    int max_texture_width;
    int max_texture_height;
} SDL_RendererInfo;
enum { SDL_TEXTUREACCESS_STATIC, SDL_TEXTUREACCESS_STREAMING, SDL_TEXTUREACCESS_TARGET };

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
int SDL_GetRendererInfo(SDL_Renderer *renderer, SDL_RendererInfo *info);
int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h);
int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
void SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h);
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);
int SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode blendMode);
int SDL_SetTextureAlphaMod(SDL_Texture *texture, Uint8 alpha);
int SDL_SetTextureColorMod(SDL_Texture *texture, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *renderer);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect);
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2);
int SDL_RenderDrawPoint(SDL_Renderer *renderer, int x, int y);
int SDL_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch);
void SDL_RenderPresent(SDL_Renderer *renderer);

/* mini-SDL extension: the palette of the 8-bit full-screen mode (the game's
   IBM.PAL); colours outside it are mapped to the nearest entry. */
void MSDL_SetScreenPalette(const SDL_Palette *palette);
/* Terrain cache (Game.cpp): everything drawn is moved by dx, dy and clipped
   to rect (NULL: no clipping); a target texture with only alpha 0/255 made
   keyed for fast BLEND copies (0 when it has other alpha values). */
void MSDL_SetRenderOffset(SDL_Renderer *renderer, int dx, int dy);
void MSDL_SetRenderClip(SDL_Renderer *renderer, const SDL_Rect *rect);
int MSDL_MakeTextureKeyed(SDL_Texture *texture);
/* A copy of the texture kept on the card (8-bit full screen): opaque copies
   from it into the frame are the card's blitter. 0: not available. */
int MSDL_MirrorTexture(SDL_Texture *texture);
/* The texture becomes keyed with the key of src, all transparent: keyed
   copies from src with NONE and transparent fills keep it keyed. */
int MSDL_SetTextureKeyLike(SDL_Texture *texture, SDL_Texture *src);
/* 1 when the environment variable is set (dos GetVar; test modes). */
int MSDL_EnvSet(const char *name);
/* DUNE_STATS: time of a part of the frame (0 logic, 1 map, 2 panel, 3 radar,
   4 wait, 5 other); start 1 / 0 = stop. */
void MSDL_Probe(int id, int start);

/* ---- keyboard ---- */
typedef enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43, SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_F1 = 58,
    SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;

typedef Sint32 SDL_Keycode;
#define SDLK_SCANCODE_MASK (1 << 30)
#define SDL_SCANCODE_TO_KEYCODE(X) ((X) | SDLK_SCANCODE_MASK)
enum {
    SDLK_UNKNOWN = 0,
    SDLK_RETURN = '\r', SDLK_ESCAPE = '\033', SDLK_BACKSPACE = '\b', SDLK_TAB = '\t',
    SDLK_SPACE = ' ', SDLK_PLUS = '+', SDLK_MINUS = '-', SDLK_EQUALS = '=',
    SDLK_0 = '0', SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
    SDLK_a = 'a', SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g, SDLK_h, SDLK_i, SDLK_j,
    SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q, SDLK_r, SDLK_s, SDLK_t,
    SDLK_u, SDLK_v, SDLK_w, SDLK_x, SDLK_y, SDLK_z,
    SDLK_DELETE = '\177',
    SDLK_F1 = SDL_SCANCODE_TO_KEYCODE(58), SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6,
    SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
    SDLK_PRINTSCREEN = SDL_SCANCODE_TO_KEYCODE(70),
    SDLK_RIGHT = SDL_SCANCODE_TO_KEYCODE(79), SDLK_LEFT = SDL_SCANCODE_TO_KEYCODE(80),
    SDLK_DOWN = SDL_SCANCODE_TO_KEYCODE(81), SDLK_UP = SDL_SCANCODE_TO_KEYCODE(82),
    SDLK_KP_MINUS = SDL_SCANCODE_TO_KEYCODE(86), SDLK_KP_PLUS = SDL_SCANCODE_TO_KEYCODE(87),
    SDLK_SYSREQ = SDL_SCANCODE_TO_KEYCODE(154)
};
#define SDLK_PRINT SDLK_PRINTSCREEN

typedef enum {
    KMOD_NONE = 0, KMOD_LSHIFT = 0x0001, KMOD_RSHIFT = 0x0002,
    KMOD_LCTRL = 0x0040, KMOD_RCTRL = 0x0080, KMOD_LALT = 0x0100, KMOD_RALT = 0x0200,
    KMOD_LGUI = 0x0400, KMOD_RGUI = 0x0800, KMOD_CAPS = 0x2000
} SDL_Keymod;
#define KMOD_CTRL  (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_ALT   (KMOD_LALT | KMOD_RALT)
#define KMOD_GUI   (KMOD_LGUI | KMOD_RGUI)

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
} SDL_Keysym;

const Uint8 *SDL_GetKeyboardState(int *numkeys);
SDL_Keymod SDL_GetModState(void);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);

/* ---- mouse ---- */
#define SDL_BUTTON(X) (1 << ((X) - 1))
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
Uint32 SDL_GetMouseState(int *x, int *y);

/* ---- events ---- */
typedef enum {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT = 0x100,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300, SDL_KEYUP, SDL_TEXTEDITING, SDL_TEXTINPUT,
    SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL,
    SDL_USEREVENT = 0x8000,
    SDL_LASTEVENT = 0xFFFF
} SDL_EventType;

#define SDL_PRESSED  1
#define SDL_RELEASED 0

typedef struct SDL_KeyboardEvent {
    Uint32 type, timestamp, windowID;
    Uint8 state, repeat, padding2, padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;
#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32
typedef struct SDL_TextInputEvent {
    Uint32 type, timestamp, windowID;
    char text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
} SDL_TextInputEvent;
typedef struct SDL_MouseMotionEvent {
    Uint32 type, timestamp, windowID, which, state;
    Sint32 x, y, xrel, yrel;
} SDL_MouseMotionEvent;
typedef struct SDL_MouseButtonEvent {
    Uint32 type, timestamp, windowID, which;
    Uint8 button, state, clicks, padding1;
    Sint32 x, y;
} SDL_MouseButtonEvent;
typedef struct SDL_MouseWheelEvent {
    Uint32 type, timestamp, windowID, which;
    Sint32 x, y;
    Uint32 direction;
} SDL_MouseWheelEvent;
typedef struct SDL_QuitEvent { Uint32 type, timestamp; } SDL_QuitEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_QuitEvent quit;
    Uint8 padding[56];
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
void SDL_PumpEvents(void);
void SDL_FlushEvents(Uint32 minType, Uint32 maxType);
int SDL_PushEvent(SDL_Event *event);

/* ---- audio (only what SDL_mixer users need) ---- */
#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_U16LSB 0x0010
#define AUDIO_S16LSB 0x8010
#define AUDIO_U16MSB 0x1010
#define AUDIO_S16MSB 0x9010
#define AUDIO_S16SYS AUDIO_S16MSB
typedef Uint16 SDL_AudioFormat;

#ifdef __cplusplus
}
#endif

#endif
