/* Native SDL2 subset: the window and its 8-bit surface on AmigaOS 3 +
   Picasso96 (Warp RTG).

   The game renders straight into the window surface (8-bit, Fast RAM,
   dx.cpp CanRenderDirectlyToOutputSurface) and reports the changed
   rectangles with SDL_UpdateWindowSurfaceRects. A shadow copy of what the
   display shows is compared a long word at a time in bands of 8 rows; only
   the changed span of each band is sent (Warp RTG notes: write VRAM only,
   never read it, only what changed):
     - full screen: an own 8-bit CLUT screen, the game's palette loaded into
       the hardware (no conversion), spans written into the locked bitmap
       with long moves;
     - window on the Workbench (15 bits or more): spans converted through a
       palette table to R5G6B5 and written with p96WritePixelArray().
   No renderer: SDL_CreateRenderer() fails, the game must not ask for one
   (upscale off). */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <libraries/Picasso96.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msdl_internal.h"

struct Library *P96Base;

struct SDL_Window {
    int w, h;
    Uint32 flags;
    struct Screen *screen;  /* own screen, or NULL on the Workbench */
    struct Window *win;
    struct Screen *wb;      /* locked public Workbench screen */
    SDL_Surface *surface;   /* the 8-bit window surface */
    void *pixels_raw;       /* its pixels before aligning to 16 bytes (MOVE16) */
    char title[128];
};

static SDL_Window *the_window;
static UWORD *blank_pointer;
static int cursor_shown = 1;

static Uint8 *shadow8;      /* what the display shows */
static int shadow_w, shadow_h;
static Uint32 pal_version;  /* palette version on the display */
static UWORD lut565[256];
static UWORD *row16;        /* one converted span (window mode) */
#define BAND 8

/* statistics (ENV:DIABLO_STATS or "stats"): every 5 s a log line with the
   frames per second, the time between two presents (the game's frame) and
   the time of the present itself (compare + send to the card) */
static unsigned long st_frames, st_bands, st_bytes;
static unsigned long st_panel_full;   /* presents without rectangles (whole frame) */
static int stats_on = -1;
static unsigned long stats_t0, stats_prev, stats_frames, stats_present, stats_bytes0, stats_max;

void msdl_stats_toggle(void)
{
    stats_on = stats_on > 0 ? 0 : 1;
    stats_t0 = 0;
    msdl_logf(SDL_LOG_PRIORITY_INFO, "stats: %s", stats_on ? "on" : "off");
}

static void stats_frame(unsigned long t_start, unsigned long t_end)
{
    unsigned long per_ms = amiga_eclock_per_ms();
    if (stats_prev != 0 && t_end - stats_prev > stats_max) stats_max = t_end - stats_prev;
    stats_prev = t_end;
    if (stats_t0 == 0) {
        stats_t0 = t_end;
        stats_frames = 0;
        stats_present = 0;
        stats_max = 0;
        stats_bytes0 = st_bytes;
        return;
    }
    stats_frames++;
    stats_present += t_end - t_start;
    if (t_end - stats_t0 >= 5000 * per_ms) {
        unsigned long ms = (t_end - stats_t0) / per_ms;
        unsigned long f = stats_frames;
        msdl_logf(SDL_LOG_PRIORITY_INFO,
                  "stats: %lu.%lu fps, frame %lu ms (longest %lu), present %lu.%lu ms, sent %lu KB/frame, %lu full-frame reports",
                  f * 1000 / ms, (f * 10000 / ms) % 10, ms / f, stats_max / per_ms,
                  stats_present / per_ms / f, (stats_present * 10 / per_ms / f) % 10,
                  (st_bytes - stats_bytes0) / 1024 / f, st_panel_full);
        st_panel_full = 0;
        stats_t0 = t_end;
        stats_frames = 0;
        stats_present = 0;
        stats_max = 0;
        stats_bytes0 = st_bytes;
    }
}

static int open_p96(void)
{
    if (P96Base == NULL) P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    return P96Base != NULL;
}

/* ---- display modes: 8-bit CLUT modes of the board ---- */
#define MAX_MODES 32
static SDL_DisplayMode modes[MAX_MODES];
static ULONG mode_ids[MAX_MODES];
static int num_modes = -1;

static void scan_modes(void)
{
    struct List *ml;
    struct P96Mode *m;
    int i, j;
    num_modes = 0;
    if (!open_p96()) return;
    ml = p96AllocModeListTags(P96MA_FormatsAllowed, RGBFF_CLUT, TAG_DONE);
    if (ml == NULL) return;
    for (m = (struct P96Mode *)ml->lh_Head; m->Node.ln_Succ != NULL && num_modes < MAX_MODES;
         m = (struct P96Mode *)m->Node.ln_Succ) {
        int dup = 0;
        if (m->Depth != 8) continue;
        for (i = 0; i < num_modes; i++) {
            if (modes[i].w == m->Width && modes[i].h == m->Height) dup = 1;
        }
        if (dup) continue;
        memset(&modes[num_modes], 0, sizeof(modes[0]));
        modes[num_modes].format = SDL_PIXELFORMAT_INDEX8;
        modes[num_modes].w = m->Width;
        modes[num_modes].h = m->Height;
        modes[num_modes].refresh_rate = 60;
        mode_ids[num_modes] = m->DisplayID;
        num_modes++;
    }
    p96FreeModeList(ml);
    /* largest first, as SDL sorts them */
    for (i = 0; i < num_modes; i++) {
        for (j = i + 1; j < num_modes; j++) {
            if ((long)modes[j].w * modes[j].h > (long)modes[i].w * modes[i].h) {
                SDL_DisplayMode t = modes[i];
                ULONG id = mode_ids[i];
                modes[i] = modes[j];
                modes[j] = t;
                mode_ids[i] = mode_ids[j];
                mode_ids[j] = id;
            }
        }
    }
}

int SDL_GetNumDisplayModes(int displayIndex)
{
    (void)displayIndex;
    if (num_modes < 0) scan_modes();
    return num_modes;
}

int SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode)
{
    if (modeIndex < 0 || modeIndex >= SDL_GetNumDisplayModes(displayIndex)) {
        /* no CLUT modes: one 640x480 so the game has something */
        if (modeIndex == 0) {
            memset(mode, 0, sizeof(*mode));
            mode->format = SDL_PIXELFORMAT_INDEX8;
            mode->w = 640;
            mode->h = 480;
            mode->refresh_rate = 60;
            return 0;
        }
        return SDL_SetError("no such mode");
    }
    *mode = modes[modeIndex];
    return 0;
}

int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode)
{
    struct Screen *wb;
    (void)displayIndex;
    memset(mode, 0, sizeof(*mode));
    mode->format = SDL_PIXELFORMAT_RGB565;
    mode->refresh_rate = 60;
    mode->w = 640;
    mode->h = 480;
    wb = LockPubScreen(NULL);
    if (wb != NULL) {
        mode->w = wb->Width;
        mode->h = wb->Height;
        UnlockPubScreen(NULL, wb);
    }
    return 0;
}

int SDL_GetDisplayDPI(int displayIndex, float *ddpi, float *hdpi, float *vdpi)
{
    (void)displayIndex;
    if (ddpi != NULL) *ddpi = 96.0f;
    if (hdpi != NULL) *hdpi = 96.0f;
    if (vdpi != NULL) *vdpi = 96.0f;
    return 0;
}

static ULONG find_clut_mode(int w, int h)
{
    int i;
    ULONG best = INVALID_ID;
    long best_area = 0x7FFFFFFFL;
    SDL_GetNumDisplayModes(0);
    for (i = 0; i < num_modes; i++) {
        long area = (long)modes[i].w * modes[i].h;
        if (modes[i].w >= w && modes[i].h >= h && area < best_area) {
            best_area = area;
            best = mode_ids[i];
        }
    }
    return best;
}

int SDL_GetWindowDisplayIndex(SDL_Window *window) { (void)window; return 0; }

int SDL_GetWindowDisplayMode(SDL_Window *window, SDL_DisplayMode *mode)
{
    memset(mode, 0, sizeof(*mode));
    mode->format = SDL_PIXELFORMAT_INDEX8;
    mode->w = window != NULL ? window->w : 640;
    mode->h = window != NULL ? window->h : 480;
    mode->refresh_rate = 60;
    return 0;
}

/* The game sets the mode closest to its size; the screen is opened in
   exactly the window's size anyway. */
int SDL_SetWindowDisplayMode(SDL_Window *window, const SDL_DisplayMode *mode)
{
    (void)window;
    (void)mode;
    return 0;
}

Uint32 SDL_GetWindowPixelFormat(SDL_Window *window) { (void)window; return SDL_PIXELFORMAT_INDEX8; }

/* ---- palette ---- */
static ULONG palette32[1 + 256 * 3 + 1];

static void load_palette(const SDL_Palette *p)
{
    int i;
    if (the_window == NULL || p == NULL) return;
    if (the_window->screen != NULL) {
        msdl_vram_unlock();
        palette32[0] = (256UL << 16) | 0;
        for (i = 0; i < 256; i++) {
            const SDL_Color *c = i < p->ncolors ? &p->colors[i] : &p->colors[0];
            palette32[1 + i * 3] = (ULONG)c->r * 0x01010101UL;
            palette32[2 + i * 3] = (ULONG)c->g * 0x01010101UL;
            palette32[3 + i * 3] = (ULONG)c->b * 0x01010101UL;
        }
        palette32[1 + 256 * 3] = 0;
        LoadRGB32(&the_window->screen->ViewPort, palette32);
    } else {
        for (i = 0; i < 256; i++) {
            const SDL_Color *c = i < p->ncolors ? &p->colors[i] : &p->colors[0];
            lut565[i] = (UWORD)(((c->r & 0xF8) << 8) | ((c->g & 0xFC) << 3) | (c->b >> 3));
        }
    }
}

/* ---- window ---- */
static void apply_pointer(void)
{
    if (the_window == NULL || the_window->win == NULL) return;
    if (cursor_shown) {
        ClearPointer(the_window->win);
        return;
    }
    if (blank_pointer == NULL) {
        blank_pointer = (UWORD *)AllocVec(16, MEMF_CHIP | MEMF_CLEAR);
        if (blank_pointer == NULL) return;
    }
    SetPointer(the_window->win, blank_pointer, 1, 16, 0, 0);
}

static void close_display(SDL_Window *w)
{
    msdl_vram_unlock();
    if (w->win != NULL) {
        msdl_events_attach(NULL);
        ClearPointer(w->win);
        CloseWindow(w->win);
        w->win = NULL;
    }
    if (w->screen != NULL) {
        CloseScreen(w->screen);
        w->screen = NULL;
    }
    if (w->wb != NULL) {
        UnlockPubScreen(NULL, w->wb);
        w->wb = NULL;
    }
    free(shadow8);
    shadow8 = NULL;
    pal_version = 0;
}

#define IDCMP_ALL (IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | \
                   IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW)

static int open_display(SDL_Window *w)
{
    msdl_vram_unlock();
    if (!open_p96()) return SDL_SetError("Picasso96API.library v2 not found");
    if (w->flags & SDL_WINDOW_FULLSCREEN) {
        ULONG id = find_clut_mode(w->w, w->h);
        if (id == INVALID_ID) return SDL_SetError("no 8-bit RTG screen mode for %dx%d", w->w, w->h);
        w->screen = p96OpenScreenTags(P96SA_DisplayID, id,
                                      P96SA_Width, w->w,
                                      P96SA_Height, w->h,
                                      P96SA_Depth, 8,
                                      P96SA_RGBFormat, RGBFB_CLUT,
                                      P96SA_Quiet, TRUE,
                                      P96SA_ShowTitle, FALSE,
                                      P96SA_Title, (ULONG)w->title,
                                      P96SA_Exclusive, TRUE,
                                      TAG_DONE);
        if (w->screen == NULL) return SDL_SetError("cannot open a %dx%d 8-bit screen", w->w, w->h);
        w->win = OpenWindowTags(NULL,
                                WA_CustomScreen, (ULONG)w->screen,
                                WA_Left, 0, WA_Top, 0,
                                WA_Width, w->w, WA_Height, w->h,
                                WA_Backdrop, TRUE,
                                WA_Borderless, TRUE,
                                WA_Activate, TRUE,
                                WA_RMBTrap, TRUE,
                                WA_ReportMouse, TRUE,
                                WA_SimpleRefresh, TRUE,
                                WA_NoCareRefresh, TRUE,
                                WA_IDCMP, IDCMP_ALL,
                                TAG_DONE);
        if (w->win == NULL) return SDL_SetError("cannot open the window");
        SetRast(w->win->RPort, 0);
        msdl_events_attach(w->win);
        msdl_events_set_scale(1, 1, 0, 0);
    } else {
        w->wb = LockPubScreen(NULL);
        if (w->wb == NULL) return SDL_SetError("no Workbench screen");
        if (GetBitMapAttr(w->wb->RastPort.BitMap, BMA_DEPTH) < 15) {
            return SDL_SetError("the Workbench needs 15 bits or more for a window; use full screen");
        }
        w->win = OpenWindowTags(NULL,
                                WA_PubScreen, (ULONG)w->wb,
                                WA_InnerWidth, w->w, WA_InnerHeight, w->h,
                                WA_Left, (w->wb->Width > w->w + 8) ? (w->wb->Width - w->w) / 2 : 0,
                                WA_Top, (w->wb->Height > w->h + 24) ? (w->wb->Height - w->h) / 2 : 0,
                                WA_Title, (ULONG)w->title,
                                WA_DragBar, TRUE,
                                WA_DepthGadget, TRUE,
                                WA_CloseGadget, TRUE,
                                WA_Activate, TRUE,
                                WA_RMBTrap, TRUE,
                                WA_ReportMouse, TRUE,
                                WA_SmartRefresh, TRUE,
                                WA_IDCMP, IDCMP_ALL,
                                TAG_DONE);
        if (w->win == NULL) return SDL_SetError("cannot open a window on the Workbench");
        msdl_events_attach(w->win);
        msdl_events_set_scale(1, 1, w->win->BorderLeft, w->win->BorderTop);
    }
    apply_pointer();
    return 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    SDL_Window *win;
    (void)x;
    (void)y;
    if (the_window != NULL) {
        SDL_SetError("only one window");
        return NULL;
    }
    win = (SDL_Window *)calloc(1, sizeof(SDL_Window));
    if (win == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    strncpy(win->title, title != NULL ? title : "", sizeof(win->title) - 1);
    win->w = w;
    win->h = h;
    win->flags = (flags & ~SDL_WINDOW_RESIZABLE) | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS;
    the_window = win;
    if (open_display(win) != 0) {
        char err[256];
        strncpy(err, SDL_GetError(), sizeof(err) - 1);
        err[sizeof(err) - 1] = 0;
        close_display(win);
        free(win);
        the_window = NULL;
        SDL_SetError("%s", err);
        return NULL;
    }
    msdl_logf(SDL_LOG_PRIORITY_INFO, "msdl: %s %dx%d", (win->flags & SDL_WINDOW_FULLSCREEN) ? "8-bit screen" : "window",
              w, h);
    return win;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    if (window == NULL) return;
    close_display(window);
    if (window->surface != NULL) {
        window->surface->flags &= ~SDL_DONTFREE;
        SDL_FreeSurface(window->surface);
    }
    if (window == the_window) the_window = NULL;
    free(window->pixels_raw);
    free(window);
    free(row16);
    row16 = NULL;
}

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    if (window == NULL) {
        SDL_SetError("no window");
        return NULL;
    }
    if (window->surface == NULL || window->surface->w != window->w || window->surface->h != window->h) {
        if (window->surface != NULL) {
            window->surface->flags &= ~SDL_DONTFREE;
            SDL_FreeSurface(window->surface);
        }
        free(window->pixels_raw);
        /* 64 spare rows above and below: the game's clipped renderers may
           write a little outside the picture, it must not hit the heap */
        window->pixels_raw = calloc(1, (size_t)window->w * (window->h + 128) + 16);
        if (window->pixels_raw == NULL) {
            SDL_SetError("out of memory for the window surface");
            return NULL;
        }
        /* 16-byte aligned rows (w is a multiple of 16) for MOVE16 */
        window->surface = SDL_CreateRGBSurfaceWithFormatFrom(
            (void *)((((unsigned long)window->pixels_raw + 15) & ~15UL) + (unsigned long)window->w * 64), window->w,
            window->h, 8, window->w, SDL_PIXELFORMAT_INDEX8);
        if (window->surface == NULL) return NULL;
        window->surface->flags |= SDL_DONTFREE;
        free(shadow8);
        shadow8 = NULL;
    }
    return window->surface;
}

Uint32 SDL_GetWindowFlags(SDL_Window *window) { return window != NULL ? window->flags : 0; }

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    if (w != NULL) *w = window != NULL ? window->w : 640;
    if (h != NULL) *h = window != NULL ? window->h : 480;
}

void SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
    if (window == NULL || (window->w == w && window->h == h)) return;
    close_display(window);
    window->w = w;
    window->h = h;
    open_display(window);
}

/* Window <-> screen: close and open again. */
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
    Uint32 old;
    if (window == NULL) return SDL_SetError("no window");
    old = window->flags;
    if (((old & SDL_WINDOW_FULLSCREEN) != 0) == ((flags & SDL_WINDOW_FULLSCREEN) != 0)) return 0;
    close_display(window);
    window->flags = (window->flags & ~SDL_WINDOW_FULLSCREEN_DESKTOP) | (flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (open_display(window) != 0) {
        close_display(window);
        window->flags = old;
        open_display(window);
        return -1;
    }
    return 0;
}

void SDL_SetWindowResizable(SDL_Window *window, SDL_bool resizable) { (void)window; (void)resizable; }
void SDL_SetWindowGrab(SDL_Window *window, SDL_bool grabbed) { (void)window; (void)grabbed; }

void SDL_RaiseWindow(SDL_Window *window)
{
    if (window == NULL) return;
    if (window->screen != NULL) ScreenToFront(window->screen);
    else if (window->win != NULL) WindowToFront(window->win);
}

void SDL_ShowWindow(SDL_Window *window) { (void)window; }
void SDL_HideWindow(SDL_Window *window) { (void)window; }
void SDL_RestoreWindow(SDL_Window *window) { (void)window; }

SDL_Window *SDL_GetKeyboardFocus(void) { return the_window; }

void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y) { (void)window; (void)x; (void)y; }

/* ---- cursor: the game draws its own when there is no hardware cursor ---- */
int SDL_ShowCursor(int toggle)
{
    if (toggle >= 0) {
        cursor_shown = toggle != 0;
        apply_pointer();
    }
    return cursor_shown;
}

SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *surface, int hot_x, int hot_y)
{
    (void)surface;
    (void)hot_x;
    (void)hot_y;
    SDL_SetError("no colour cursors");
    return NULL;
}

void SDL_SetCursor(SDL_Cursor *cursor) { (void)cursor; }
void SDL_FreeCursor(SDL_Cursor *cursor) { (void)cursor; }

void msdl_video_hide_for_requester(void)
{
    msdl_vram_unlock();
    if (the_window != NULL && the_window->screen != NULL) WBenchToFront();
}

void msdl_video_quit(void)
{
    if (the_window != NULL) SDL_DestroyWindow(the_window);
    if (blank_pointer != NULL) {
        FreeVec(blank_pointer);
        blank_pointer = NULL;
    }
    if (P96Base != NULL) {
        CloseLibrary(P96Base);
        P96Base = NULL;
    }
}

void msdl_vram_unlock(void) {}

/* ---- present ---- */
static void send_span(int x, int y, int w, int h)
{
    struct Window *win = the_window->win;
    st_bands++;
    st_bytes += (unsigned long)w * h;
    if (the_window->screen != NULL) {
        struct BitMap *bm = the_window->screen->RastPort.BitMap;
        struct RenderInfo ri;
        LONG lock = p96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
        if (lock != 0 && ri.Memory != NULL && ri.RGBFormat == RGBFB_CLUT) {
            int j;
            UBYTE *dst = (UBYTE *)ri.Memory + (LONG)y * ri.BytesPerRow + x;
            const UBYTE *src = shadow8 + y * shadow_w + x;
            for (j = 0; j < h; j++) {
                ULONG *d = (ULONG *)dst;
                const ULONG *s = (const ULONG *)src;
                int n = w >> 2;          /* x and w are multiples of 4 */
                while (n >= 4) {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                    d += 4; s += 4; n -= 4;
                }
                while (n-- > 0) *d++ = *s++;
                dst += ri.BytesPerRow;
                src += shadow_w;
            }
            p96UnlockBitMap(bm, lock);
            return;
        }
        if (lock != 0) p96UnlockBitMap(bm, lock);
        WriteChunkyPixels(win->RPort, x, y, x + w - 1, y + h - 1, shadow8 + y * shadow_w + x, shadow_w);
        return;
    }
    /* window: convert the span through the palette table */
    {
        struct RenderInfo ri;
        int j, i;
        UWORD *d;
        if (row16 == NULL) {
            row16 = (UWORD *)malloc((size_t)shadow_w * BAND * 2);
            if (row16 == NULL) return;
        }
        d = row16;
        for (j = 0; j < h; j++) {
            const UBYTE *s = shadow8 + (y + j) * shadow_w + x;
            for (i = 0; i < w; i++) d[i] = lut565[s[i]];
            d += w;
        }
        ri.Memory = row16;
        ri.BytesPerRow = (UWORD)(w * 2);
        ri.pad = 0;
        ri.RGBFormat = RGBFB_R5G6B5;
        p96WritePixelArray(&ri, 0, 0, win->RPort, win->BorderLeft + x, win->BorderTop + y, w, h);
    }
}

/* ---- full screen: the reported rectangles straight to VRAM ----
   Diablo redraws the whole view every frame, so comparing with a shadow
   copy costs more than it saves: the bounding box of the rectangles goes
   to the screen bitmap row by row with MOVE16 (WarpBench: 640x400 in
   4.7 ms). MOVE16 reads memory, not the data cache: the copyback cache is
   pushed first. Needs 16-byte aligned rows on both sides, else long moves. */
static int present_mode = -1;  /* 0 direct (default), 1 compare with the shadow */

static void copy_rows_move16(UBYTE *d, LONG dbpr, const UBYTE *s, LONG sbpr, int w16, int h)
{
    int y;
    for (y = 0; y < h; y++) {
        register const UBYTE *a0 __asm("a0") = s;
        register UBYTE *a1 __asm("a1") = d;
        register LONG d0 __asm("d0") = w16;
        __asm__ volatile(
            "1:\n\t"
            ".word 0xf620,0x9000\n\t"     /* move16 (a0)+,(a1)+ */
            "subq.l #1,%2\n\t"
            "bne.s 1b"
            : "+a"(a0), "+a"(a1), "+d"(d0)
            :
            : "cc", "memory");
        s += sbpr;
        d += dbpr;
    }
}

static void copy_rows_long(UBYTE *d, LONG dbpr, const UBYTE *s, LONG sbpr, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) {
        ULONG *dl = (ULONG *)d;
        const ULONG *sl = (const ULONG *)s;
        int n = w >> 2;
        while (n >= 4) {
            dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3];
            dl += 4; sl += 4; n -= 4;
        }
        while (n-- > 0) *dl++ = *sl++;
        s += sbpr;
        d += dbpr;
    }
}

static int present_direct8(SDL_Surface *s, const SDL_Rect *rects, int numrects)
{
    struct BitMap *bm = the_window->screen->RastPort.BitMap;
    struct RenderInfo ri;
    LONG lock;
    int x0 = 0, y0 = 0, x1 = s->w, y1 = s->h, i;
    SDL_Palette *pal = s->format->palette;
    if (pal != NULL && pal->version != pal_version) {
        load_palette(pal);
        pal_version = pal->version;
    }
    if (rects != NULL) {
        x0 = s->w; y0 = s->h; x1 = 0; y1 = 0;
        for (i = 0; i < numrects; i++) {
            const SDL_Rect *r = &rects[i];
            if (r->w <= 0 || r->h <= 0) continue;
            if (r->x < x0) x0 = r->x;
            if (r->y < y0) y0 = r->y;
            if (r->x + r->w > x1) x1 = r->x + r->w;
            if (r->y + r->h > y1) y1 = r->y + r->h;
        }
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > s->w) x1 = s->w;
        if (y1 > s->h) y1 = s->h;
        if (x1 <= x0 || y1 <= y0) return 1;
    }
    x0 &= ~15;
    x1 = (x1 + 15) & ~15;
    if (x1 > s->w) x1 = s->w & ~3;
    lock = p96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
    if (lock == 0 || ri.Memory == NULL || ri.RGBFormat != RGBFB_CLUT) {
        if (lock != 0) p96UnlockBitMap(bm, lock);
        return 0;
    }
    {
        const UBYTE *src = (const UBYTE *)s->pixels + y0 * s->pitch + x0;
        UBYTE *dst = (UBYTE *)ri.Memory + (LONG)y0 * ri.BytesPerRow + x0;
        int w = x1 - x0, h = y1 - y0;
        if ((((unsigned long)src | (unsigned long)dst | (unsigned long)s->pitch | (unsigned long)ri.BytesPerRow) & 15) == 0 &&
            (w & 15) == 0) {
            CacheClearE((APTR)src, (ULONG)s->pitch * h, CACRF_ClearD);
            copy_rows_move16(dst, ri.BytesPerRow, src, s->pitch, w >> 4, h);
        } else {
            copy_rows_long(dst, ri.BytesPerRow, src, s->pitch, w, h);
        }
        st_bands++;
        st_bytes += (unsigned long)w * h;
    }
    p96UnlockBitMap(bm, lock);
    return 1;
}

/* ---- incremental present (cell renderer, stage 2) ----
   The view is moved on the card by the blitter and only the changed
   rectangles are written; the panel below the view goes through the shadow
   comparison (it rarely changes). ENV:DIABLO_PRESENT=FULL: everything as before. */
static SDL_Surface *inc_s;
static struct BitMap *inc_bm;

static void inc_shift(int dx, int dy, int vw, int vh)
{
    int w = vw - (dx < 0 ? -dx : dx), h = vh - (dy < 0 ? -dy : dy);
    if (w <= 0 || h <= 0) return;
    /* new(x, y) = old(x + dx, y + dy); BltBitMap handles the overlap */
    BltBitMap(inc_bm, dx > 0 ? dx : 0, dy > 0 ? dy : 0, inc_bm, dx < 0 ? -dx : 0, dy < 0 ? -dy : 0, w, h, 0xC0, 0xFF, NULL);
}

static UBYTE *inc_mem;
static LONG inc_bpr, inc_lock;
static int shadow_stale = 1;   /* the panel shadow does not match the card */

static void inc_lock_now(void)
{
    struct RenderInfo ri;
    if (inc_mem != NULL) return;
    WaitBlit();
    inc_lock = p96LockBitMap(inc_bm, (UBYTE *)&ri, sizeof(ri));
    if (inc_lock == 0) return;
    if (ri.Memory == NULL || ri.RGBFormat != RGBFB_CLUT) {
        p96UnlockBitMap(inc_bm, inc_lock);
        return;
    }
    inc_mem = (UBYTE *)ri.Memory;
    inc_bpr = ri.BytesPerRow;
}

static void inc_unlock(void)
{
    if (inc_mem != NULL) {
        p96UnlockBitMap(inc_bm, inc_lock);
        inc_mem = NULL;
    }
}

static void inc_send(int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h, j;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > inc_s->w) x1 = inc_s->w;
    if (y1 > inc_s->h) y1 = inc_s->h;
    x &= ~3;
    x1 = (x1 + 3) & ~3;
    if (x1 > inc_s->w) x1 = inc_s->w & ~3;
    if (x1 <= x || y1 <= y) return;
    inc_lock_now();
    if (inc_mem == NULL) return;
    for (j = y; j < y1; j++) {
        ULONG *d = (ULONG *)(inc_mem + (LONG)j * inc_bpr + x);
        const ULONG *sl = (const ULONG *)((const UBYTE *)inc_s->pixels + j * inc_s->pitch + x);
        int n = (x1 - x) >> 2;
        while (n >= 4) {
            d[0] = sl[0]; d[1] = sl[1]; d[2] = sl[2]; d[3] = sl[3];
            d += 4; sl += 4; n -= 4;
        }
        while (n-- > 0) *d++ = *sl++;
    }
    st_bands++;
    st_bytes += (unsigned long)(x1 - x) * (y1 - y);
}

/* Rows y0..y1, columns x0..x1 of the frame compared with the shadow,
   changed spans sent. */
static void present_area_diff(SDL_Surface *s, int x0, int y0, int x1, int y1);
static void present_rows_diff(SDL_Surface *s, int y0, int y1) { present_area_diff(s, 0, y0, s->w, y1); }

static void present_area_diff(SDL_Surface *s, int x0, int y0, int x1, int y1)
{
    const Uint8 *pixels = (const Uint8 *)s->pixels;
    int lw, la, by, full = shadow_stale;
    x0 &= ~3;
    x1 = (x1 + 3) & ~3;
    if (x0 < 0) x0 = 0;
    if (x1 > s->w) x1 = s->w & ~3;
    if (x1 <= x0) return;
    la = x0 >> 2;           /* first long of the area */
    lw = x1 >> 2;           /* end long */
    if (shadow8 == NULL || shadow_w != s->w || shadow_h != s->h) {
        free(shadow8);
        shadow_w = s->w;
        shadow_h = s->h;
        shadow8 = (Uint8 *)malloc((size_t)shadow_w * shadow_h);
        if (shadow8 == NULL) return;
        full = 1;
    }
    shadow_stale = 0;
    for (by = y0; by < y1; by += BAND) {
        int ry1 = (by + BAND) > y1 ? y1 : by + BAND;
        int cx0 = 0x7FFF, cx1 = -1, y;
        for (y = by; y < ry1; y++) {
            const ULONG *sl = (const ULONG *)(pixels + y * s->pitch);
            ULONG *d = (ULONG *)(shadow8 + y * shadow_w);
            int a, b;
            if (full) {
                a = la;
                b = lw - 1;
            } else {
                for (a = la; a < lw && sl[a] == d[a]; a++) ;
                if (a == lw) continue;
                for (b = lw - 1; b > a && sl[b] == d[b]; b--) ;
            }
            memcpy(d + a, sl + a, (size_t)(b - a + 1) * 4);
            if (a < cx0) cx0 = a;
            if (b > cx1) cx1 = b;
        }
        if (cx1 >= cx0) send_span(cx0 * 4, by, (cx1 - cx0 + 1) * 4, ry1 - by);
    }
}

/* VERIFY: read the view back from the card (test only: slow) and compare. */
static void verify_vram(SDL_Surface *s, int vh)
{
    static unsigned long frames, bad_frames;
    int y, bad = 0, fx = -1, fy = -1;
    inc_lock_now();
    if (inc_mem == NULL) return;
    for (y = 0; y < vh; y++) {
        const UBYTE *a = inc_mem + (LONG)y * inc_bpr;
        const UBYTE *b = (const UBYTE *)s->pixels + y * s->pitch;
        int x;
        for (x = 0; x < s->w; x++) {
            if (a[x] != b[x]) {
                if (fx < 0) { fx = x; fy = y; }
                bad++;
            }
        }
    }
    frames++;
    if (bad != 0) {
        bad_frames++;
        if (bad_frames <= 30)
            msdl_logf(SDL_LOG_PRIORITY_INFO, "present: VRAM VERIFY frame %lu: %d pixels differ, first %d,%d", frames, bad, fx, fy);
        inc_send(0, 0, s->w, vh);   /* show the truth again */
    }
    inc_unlock();
    if ((frames % 500) == 0) msdl_logf(SDL_LOG_PRIORITY_INFO, "present: VRAM verify %lu frames, %lu bad", frames, bad_frames);
}

static int present_mode_inc = -1;

/* 1 when done incrementally, 0: the caller sends the frame as before. */
static int present_incremental(SDL_Surface *s, const SDL_Rect *rects, int numrects)
{
    int vh;
    SDL_Palette *pal = s->format->palette;
    if (present_mode_inc < 0) {
        char buf[16];
        present_mode_inc = !(msdl_env("DIABLO_PRESENT", buf, sizeof(buf)) >= 0 && (buf[0] == 'F' || buf[0] == 'f'));
    }
    if (!present_mode_inc || the_window->screen == NULL) return 0;
    if (pal != NULL && pal->version != pal_version) return 0;  /* the full path loads it */
    inc_s = s;
    inc_bm = the_window->screen->RastPort.BitMap;
    vh = msdl_view_upload(inc_shift, inc_send);
    inc_unlock();
    if (vh <= 0) return 0;
    /* the panel below the view: only the rows the game reports as redrawn
       (all of it when it reports the whole frame) */
    if (rects == NULL) st_panel_full++;
    if (rects == NULL || shadow_stale || shadow8 == NULL) {
        present_rows_diff(s, vh, s->h);
    } else {
        int i;
        for (i = 0; i < numrects; i++) {
            int y0 = rects[i].y, y1 = rects[i].y + rects[i].h;
            if (y1 <= vh || rects[i].w <= 0) continue;
            if (y0 < vh) y0 = vh;
            if (y1 > s->h) y1 = s->h;
            if (y1 > y0) present_area_diff(s, rects[i].x, y0, rects[i].x + rects[i].w, y1);
        }
    }
    if (msdl_view_verify_on()) verify_vram(s, s->h);
    return 1;
}

static void present(const SDL_Rect *rects, int numrects)
{
    SDL_Surface *s;
    const Uint8 *pixels;
    int w, h, pitch, lw, y0, y1, by, full = 0, i;
    SDL_Palette *pal;
    if (the_window == NULL || the_window->win == NULL || the_window->surface == NULL) return;
    s = the_window->surface;
    pixels = (const Uint8 *)s->pixels;
    w = s->w;
    h = s->h;
    pitch = s->pitch;
    lw = w >> 2;
    st_frames++;

    if (present_mode < 0) {
        char buf[16];
        present_mode = msdl_env("DIABLO_PRESENT", buf, sizeof(buf)) >= 0 && (buf[0] == 'D' || buf[0] == 'd');
    }
    msdl_cells_frame(pixels, pitch, w, h);
    if (present_mode == 0 && the_window->screen != NULL) {
        int done = present_incremental(s, rects, numrects);
        if (!done && present_direct8(s, rects, numrects)) {
            shadow_stale = 1;
            done = 1;
        }
        msdl_view_present(pixels, pitch, w, h);
        if (done) return;
    } else {
        msdl_view_present(pixels, pitch, w, h);
    }

    pal = s->format->palette;
    if (pal != NULL && pal->version != pal_version) {
        load_palette(pal);
        /* a window shows converted pixels: a new palette changes them all */
        if (the_window->screen == NULL) full = 1;
        pal_version = pal->version;
    }
    if (shadow8 == NULL || shadow_w != w || shadow_h != h) {
        free(shadow8);
        shadow_w = w;
        shadow_h = h;
        shadow8 = (Uint8 *)malloc((size_t)w * h);
        if (shadow8 == NULL) return;
        full = 1;
    }
    if (rects == NULL || full) {
        y0 = 0;
        y1 = h;
    } else {
        y0 = h;
        y1 = 0;
        for (i = 0; i < numrects; i++) {
            if (rects[i].w <= 0 || rects[i].h <= 0) continue;
            if (rects[i].y < y0) y0 = rects[i].y;
            if (rects[i].y + rects[i].h > y1) y1 = rects[i].y + rects[i].h;
        }
        if (y0 < 0) y0 = 0;
        if (y1 > h) y1 = h;
    }
    if (y1 <= y0) return;
    for (by = y0 - (y0 % BAND); by < y1; by += BAND) {
        int ry0 = by < y0 ? y0 : by;
        int ry1 = (by + BAND) > y1 ? y1 : by + BAND;
        int cx0 = 0x7FFF, cx1 = -1, y;
        for (y = ry0; y < ry1; y++) {
            const ULONG *sl = (const ULONG *)(pixels + y * pitch);
            ULONG *d = (ULONG *)(shadow8 + y * shadow_w);
            int a, b;
            if (full) {
                a = 0;
                b = lw - 1;
            } else {
                for (a = 0; a < lw && sl[a] == d[a]; a++) ;
                if (a == lw) continue;
                for (b = lw - 1; b > a && sl[b] == d[b]; b--) ;
            }
            memcpy(d + a, sl + a, (size_t)(b - a + 1) * 4);
            if (a < cx0) cx0 = a;
            if (b > cx1) cx1 = b;
        }
        if (cx1 >= cx0) send_span(cx0 * 4, ry0, (cx1 - cx0 + 1) * 4, ry1 - ry0);
    }
}

static void present_timed(const SDL_Rect *rects, int numrects)
{
    unsigned long t0;
    if (stats_on < 0) {
        char buf[8];
        stats_on = msdl_env("DIABLO_STATS", buf, sizeof(buf)) >= 0;
    }
    if (!stats_on) {
        present(rects, numrects);
        return;
    }
    t0 = amiga_eclock();
    present(rects, numrects);
    stats_frame(t0, amiga_eclock());
}

int SDL_UpdateWindowSurface(SDL_Window *window)
{
    (void)window;
    present_timed(NULL, 0);
    return 0;
}

int SDL_UpdateWindowSurfaceRects(SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    (void)window;
    present_timed(rects, numrects);
    return 0;
}

/* ---- renderer and textures: not available ---- */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    (void)window; (void)index; (void)flags;
    SDL_SetError("no renderer on this Amiga build (turn upscale off)");
    return NULL;
}
void SDL_DestroyRenderer(SDL_Renderer *renderer) { (void)renderer; }
int SDL_RenderClear(SDL_Renderer *renderer) { (void)renderer; return -1; }
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    (void)renderer; (void)texture; (void)srcrect; (void)dstrect;
    return -1;
}
void SDL_RenderPresent(SDL_Renderer *renderer) { (void)renderer; }
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void)renderer; (void)r; (void)g; (void)b; (void)a;
    return -1;
}
void SDL_RenderGetScale(SDL_Renderer *renderer, float *scaleX, float *scaleY)
{
    (void)renderer;
    if (scaleX != NULL) *scaleX = 1.0f;
    if (scaleY != NULL) *scaleY = 1.0f;
}
void SDL_RenderGetViewport(SDL_Renderer *renderer, SDL_Rect *rect)
{
    (void)renderer;
    if (rect != NULL) {
        rect->x = rect->y = 0;
        rect->w = the_window != NULL ? the_window->w : 640;
        rect->h = the_window != NULL ? the_window->h : 480;
    }
}
int SDL_RenderSetIntegerScale(SDL_Renderer *renderer, SDL_bool enable) { (void)renderer; (void)enable; return -1; }
int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h) { (void)renderer; (void)w; (void)h; return -1; }
int SDL_RenderSetVSync(SDL_Renderer *renderer, int vsync) { (void)renderer; (void)vsync; return -1; }
int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    (void)renderer;
    SDL_GetWindowSize(the_window, w, h);
    return 0;
}
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
    (void)renderer; (void)format; (void)access; (void)w; (void)h;
    SDL_SetError("no textures");
    return NULL;
}
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
    (void)renderer; (void)surface;
    SDL_SetError("no textures");
    return NULL;
}
void SDL_DestroyTexture(SDL_Texture *texture) { (void)texture; }
int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h)
{
    (void)texture; (void)format; (void)access; (void)w; (void)h;
    return -1;
}
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    (void)texture; (void)rect; (void)pixels; (void)pitch;
    return -1;
}

/* ---- statistics (ENV:MSDL_STATS): a log line every 5 s ---- */
void msdl_video_stats(unsigned long *frames, unsigned long *bands, unsigned long *bytes)
{
    *frames = st_frames;
    *bands = st_bands;
    *bytes = st_bytes;
}
