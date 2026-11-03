/* Mini-SDL2 video for AmigaOS 3 + Picasso96 (Warp RTG).

   The renderer's back buffer (ABGR8888, logical size) is converted to
   R5G6B5 into a shadow frame in fast RAM. Only the bands of rows that
   differ from the last frame sent are sent (Warp RTG notes: write VRAM
   only, never read it, only what changed):
     - window on the Workbench: p96WritePixelArray() from the shadow frame
       (P96 clips against other windows and converts to a 32-bit
       Workbench when needed);
     - own screen (SDL_WINDOW_FULLSCREEN): a 16-bit R5G6B5 screen, the bands
       written straight into the locked bitmap with long moves.
   A window larger than the logical size gets every pixel doubled. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
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
#include <SDL.h>
#include "msdl_internal.h"

struct Library *P96Base;
extern struct ExecBase *SysBase;

struct SDL_Window {
    int w, h;               /* inner size (physical) */
    Uint32 flags;
    struct Screen *screen;  /* own screen, or NULL on the Workbench */
    struct Window *win;
    struct Screen *wb;      /* locked public Workbench screen */
    int depth;              /* 8: CLUT screen (full screen), 16: R5G6B5 */
    char title[128];
};

static SDL_Window *the_window;
static UWORD *blank_pointer;
static int cursor_shown = 1;

/* shadow frame (R5G6B5, the window's size) and its row hash for the bands */
void msdl_vram_unlock(void);
static UWORD *shadow;
static int shadow_w, shadow_h;
static UBYTE *shadow8;          /* the 8-bit screen's copy */
static int shadow8_w, shadow8_h;
#define BAND 8

/* VERIFY-style statistics, see msdl_video_stats() */
static unsigned long st_frames, st_bands, st_bytes;
static int last_scale = 1;


int open_p96(void)
{
    if (P96Base == NULL) P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    return P96Base != NULL;
}

/* ---- display modes: 16-bit R5G6B5 modes of the board ---- */
#define MAX_MODES 64
static SDL_DisplayMode modes[MAX_MODES];
static ULONG mode_ids[MAX_MODES];
static int num_modes = -1;

void scan_modes(void)
{
    struct List *ml;
    struct P96Mode *m;
    num_modes = 0;
    if (!open_p96()) return;
    ml = p96AllocModeListTags(P96MA_FormatsAllowed, RGBFF_R5G6B5, TAG_DONE);
    if (ml == NULL) return;
    for (m = (struct P96Mode *)ml->lh_Head; m->Node.ln_Succ != NULL && num_modes < MAX_MODES;
         m = (struct P96Mode *)m->Node.ln_Succ) {
        int i, dup = 0;
        for (i = 0; i < num_modes; i++) {
            if (modes[i].w == m->Width && modes[i].h == m->Height) dup = 1;
        }
        if (dup) continue;
        modes[num_modes].format = SDL_PIXELFORMAT_RGB565;
        modes[num_modes].w = m->Width;
        modes[num_modes].h = m->Height;
        modes[num_modes].refresh_rate = 60;
        modes[num_modes].driverdata = NULL;
        mode_ids[num_modes] = m->DisplayID;
        num_modes++;
    }
    p96FreeModeList(ml);
    /* largest first, as SDL sorts them */
    {
        int i, j;
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
}

int SDL_GetNumDisplayModes(int displayIndex)
{
    (void)displayIndex;
    if (num_modes < 0) scan_modes();
    return num_modes;
}

int SDL_GetDisplayMode(int displayIndex, int modeIndex, SDL_DisplayMode *mode)
{
    if (SDL_GetNumDisplayModes(displayIndex) <= modeIndex || modeIndex < 0) return SDL_SetError("no such mode");
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
    wb = LockPubScreen(NULL);
    if (wb != NULL) {
        mode->w = wb->Width;
        mode->h = wb->Height;
        UnlockPubScreen(NULL, wb);
        return 0;
    }
    mode->w = 640;
    mode->h = 480;
    return 0;
}

/* The game asks for the mode closest to the configured size; in a window
   any size up to the Workbench's works, so it gets exactly what it asked. */
SDL_DisplayMode *SDL_GetClosestDisplayMode(int displayIndex, const SDL_DisplayMode *mode, SDL_DisplayMode *closest)
{
    int i, best = -1;
    (void)displayIndex;
    if (SDL_GetNumDisplayModes(0) <= 0) {
        *closest = *mode;
        return closest;
    }
    for (i = 0; i < num_modes; i++) {
        if (modes[i].w >= mode->w && modes[i].h >= mode->h &&
            (best < 0 || (long)modes[i].w * modes[i].h < (long)modes[best].w * modes[best].h)) {
            best = i;
        }
    }
    if (best < 0) {
        *closest = *mode;
        return closest;
    }
    *closest = modes[best];
    if (closest->w != mode->w || closest->h != mode->h) {
        /* windowed play does not need a mode: keep the asked size */
        closest->w = mode->w;
        closest->h = mode->h;
    }
    return closest;
}

ULONG find_mode_id(int w, int h)
{
    int i;
    ULONG best = INVALID_ID;
    long best_area = 0x7FFFFFFFL;
    if (SDL_GetNumDisplayModes(0) <= 0) return INVALID_ID;
    for (i = 0; i < num_modes; i++) {
        long area = (long)modes[i].w * modes[i].h;
        if (modes[i].w >= w && modes[i].h >= h && area < best_area) {
            best_area = area;
            best = mode_ids[i];
        }
    }
    return best;
}

/* 8-bit CLUT mode of exactly w x h, else the smallest that holds it
   (p96BestModeIDTags() answered 320x200 for 640x480 on a Warp, Settlers). */
static ULONG find_clut_mode(int w, int h)
{
    struct List *ml;
    struct P96Mode *m;
    ULONG best = INVALID_ID;
    ULONG best_area = 0xFFFFFFFFUL;
    if (!open_p96()) return INVALID_ID;
    ml = p96AllocModeListTags(P96MA_FormatsAllowed, RGBFF_CLUT, TAG_DONE);
    if (ml == NULL) return INVALID_ID;
    for (m = (struct P96Mode *)ml->lh_Head; m->Node.ln_Succ != NULL; m = (struct P96Mode *)m->Node.ln_Succ) {
        ULONG area;
        if (m->Depth != 8 || m->Width < w || m->Height < h) continue;
        area = (ULONG)m->Width * m->Height;
        if (area < best_area) {
            best_area = area;
            best = m->DisplayID;
        }
    }
    p96FreeModeList(ml);
    return best;
}

/* ---- screen palette ---- */
static SDL_Color screen_pal[256];
static int screen_pal_set;
static ULONG palette32[1 + 256 * 3 + 1];

const SDL_Color *msdl_screen_palette(void)
{
    if (!screen_pal_set) {     /* until the game gives its palette: greys */
        int i;
        for (i = 0; i < 256; i++) {
            screen_pal[i].r = screen_pal[i].g = screen_pal[i].b = (Uint8)i;
            screen_pal[i].a = 255;
        }
    }
    return screen_pal;
}

void msdl_video_load_palette(const SDL_Color *colors)
{
    int i;
    if (the_window == NULL || the_window->screen == NULL || the_window->depth != 8) return;
    msdl_vram_unlock();     /* never LoadRGB32 with a bitmap locked */
    palette32[0] = (256UL << 16) | 0;
    for (i = 0; i < 256; i++) {
        palette32[1 + i * 3] = (ULONG)colors[i].r * 0x01010101UL;
        palette32[2 + i * 3] = (ULONG)colors[i].g * 0x01010101UL;
        palette32[3 + i * 3] = (ULONG)colors[i].b * 0x01010101UL;
    }
    palette32[1 + 256 * 3] = 0;
    LoadRGB32(&the_window->screen->ViewPort, palette32);
}

void MSDL_SetScreenPalette(const SDL_Palette *p)
{
    int i;
    if (p == NULL) return;
    for (i = 0; i < 256; i++) {
        if (i < p->ncolors) screen_pal[i] = p->colors[i];
        screen_pal[i].a = 255;
    }
    screen_pal_set = 1;
    msdl_video_load_palette(screen_pal);
}

int msdl_screen_palette_set(void) { return screen_pal_set; }

int msdl_window_depth(SDL_Window *window)
{
    return window != NULL ? window->depth : 16;
}

/* ---- window ---- */
void msdl_window_size(SDL_Window *window, int *w, int *h)
{
    if (window == NULL) {
        *w = 640;
        *h = 480;
        return;
    }
    *w = window->w;
    *h = window->h;
}

void apply_pointer(void)
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

void close_display(SDL_Window *w)
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
}

#define IDCMP_ALL (IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | \
                   IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW)

int open_display(SDL_Window *w)
{
    msdl_vram_unlock();
    if (!open_p96()) return SDL_SetError("Picasso96API.library v2 not found");

    last_scale = 0;
    if (w->flags & SDL_WINDOW_FULLSCREEN) {
        ULONG id = w->depth == 8 ? find_clut_mode(w->w, w->h) : find_mode_id(w->w, w->h);
        if (id == INVALID_ID) return SDL_SetError("no %d-bit RTG screen mode for %dx%d", w->depth, w->w, w->h);
        w->screen = p96OpenScreenTags(P96SA_DisplayID, id,
                                      P96SA_Width, w->w,
                                      P96SA_Height, w->h,
                                      P96SA_Depth, w->depth,
                                      P96SA_RGBFormat, w->depth == 8 ? RGBFB_CLUT : RGBFB_R5G6B5,
                                      P96SA_Quiet, TRUE,
                                      P96SA_ShowTitle, FALSE,
                                      P96SA_Title, (ULONG)w->title,
                                      P96SA_Exclusive, TRUE,
                                      TAG_DONE);
        if (w->screen == NULL) return SDL_SetError("cannot open a %dx%d %d-bit screen", w->w, w->h, w->depth);
        if (w->depth == 8) msdl_video_load_palette(msdl_screen_palette());
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
    } else {
        w->wb = LockPubScreen(NULL);
        if (w->wb == NULL) return SDL_SetError("no Workbench screen");
        if (GetBitMapAttr(w->wb->RastPort.BitMap, BMA_DEPTH) < 15) {
            return SDL_SetError("the Workbench needs 15 bits or more");
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
                                /* smart refresh: Intuition restores what other
                                   windows covered, since only changes are sent */
                                WA_SmartRefresh, TRUE,
                                WA_IDCMP, IDCMP_ALL,
                                TAG_DONE);
        if (w->win == NULL) return SDL_SetError("cannot open a window on the Workbench");
    }
    msdl_events_attach(w->win);
    msdl_events_set_scale(1, 1, 1, 1, w->screen != NULL ? 0 : w->win->BorderLeft,
                          w->screen != NULL ? 0 : w->win->BorderTop);
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
    win->flags = flags | SDL_WINDOW_SHOWN;
    /* full screen: 256 colours in the game's palette; a window: the
       Workbench's depth, 16 bits */
    win->depth = (flags & SDL_WINDOW_FULLSCREEN) ? 8 : 16;
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
    return win;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    if (window == NULL) return;
    close_display(window);
    if (window == the_window) the_window = NULL;
    free(window);
    free(shadow);
    shadow = NULL;
    shadow_w = shadow_h = 0;
    free(shadow8);
    shadow8 = NULL;
    shadow8_w = shadow8_h = 0;
}

Uint32 SDL_GetWindowFlags(SDL_Window *window)
{
    return window != NULL ? (window->flags | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS) : 0;
}

/* Switching between window and screen: close and open again. */
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
    Uint32 old = window->flags;
    /* the textures are in the renderer's format: 8-bit full screen and the
       16-bit window cannot be swapped while the game runs (the options menu
       restarts the video instead) */
    if (((flags & SDL_WINDOW_FULLSCREEN) ? 8 : 16) != window->depth) return SDL_SetError("restart needed");
    close_display(window);
    window->flags = (window->flags & ~SDL_WINDOW_FULLSCREEN_DESKTOP) | (flags & SDL_WINDOW_FULLSCREEN);
    free(shadow);
    shadow = NULL;
    if (open_display(window) != 0) {
        close_display(window);
        window->flags = old;
        open_display(window);
        return -1;
    }
    return 0;
}

void SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
    if (window->w == w && window->h == h) return;
    close_display(window);
    window->w = w;
    window->h = h;
    free(shadow);
    shadow = NULL;
    open_display(window);
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    strncpy(window->title, title, sizeof(window->title) - 1);
    if (window->win != NULL && window->screen == NULL) {
        SetWindowTitles(window->win, (CONST_STRPTR)window->title, (CONST_STRPTR)-1);
    }
}

void SDL_MinimizeWindow(SDL_Window *window)
{
    if (window != NULL && window->screen != NULL) ScreenToBack(window->screen);
}

int SDL_ShowCursor(int toggle)
{
    if (toggle >= 0) {
        cursor_shown = toggle != 0;
        apply_pointer();
    }
    return cursor_shown;
}

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

/* ---- present ---- */
static inline UWORD to565(Uint32 p)
{
    return (UWORD)(((p & 0xF8u) << 8) | ((p >> 5) & 0x07E0u) | ((p >> 19) & 0x1Fu));
}

void send_rect(int x, int y, int w, int h)
{
    struct Window *win = the_window->win;
    st_bands++;
    st_bytes += (unsigned long)w * h * 2;
    if (the_window->screen != NULL) {
        struct BitMap *bm = the_window->screen->RastPort.BitMap;
        struct RenderInfo ri;
        LONG lock = p96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
        if (lock != 0 && ri.Memory != NULL && ri.RGBFormat == RGBFB_R5G6B5) {
            int j;
            UBYTE *dst = (UBYTE *)ri.Memory + (LONG)y * ri.BytesPerRow + x * 2;
            const UWORD *src = shadow + y * shadow_w + x;
            for (j = 0; j < h; j++) {
                ULONG *d = (ULONG *)dst;
                const ULONG *s = (const ULONG *)src;
                int n = w >> 1;
                /* x and w are even (bands are aligned to 2 pixels) */
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
    }
    {
        struct RenderInfo ri;
        ri.Memory = shadow;
        ri.BytesPerRow = shadow_w * 2;
        ri.pad = 0;
        ri.RGBFormat = RGBFB_R5G6B5;
        p96WritePixelArray(&ri, x, y, win->RPort,
                           (the_window->screen != NULL ? 0 : win->BorderLeft) + x,
                           (the_window->screen != NULL ? 0 : win->BorderTop) + y, w, h);
    }
}

/* DUNE_STATS: a log line every 5 s with frames/s, the time between two
   presents and the time of the present itself (convert + send). */
extern unsigned long amiga_eclock(void);
extern unsigned long amiga_eclock_per_ms(void);
static int stats_mode = -1;
static unsigned long stats_t0, stats_last, stats_frames, stats_present_ticks, stats_bytes0;

static unsigned long stats_prev_end, stats_max_dt;

/* MSDL_Probe: E-clock time of parts of the frame (DUNE_STATS only), shown
   with the stats line as ms per frame */
#define NPROBES 8
static const char *probe_name[NPROBES] = { "logic", "map", "panel", "radar", "wait", "pace", "p6", "p7" };
static unsigned long probe_acc[NPROBES], probe_t0[NPROBES], probe_count[NPROBES];
static unsigned long probe_frame[NPROBES];     /* this frame only */

int msdl_stats_on(void)
{
    if (stats_mode < 0) {
        char buf[8];
        stats_mode = GetVar((CONST_STRPTR)"DUNE_STATS", (STRPTR)buf, sizeof(buf), 0) >= 0;
    }
    return stats_mode;
}

void MSDL_Probe(int id, int start)
{
    if (!msdl_stats_on() || id < 0 || id >= NPROBES) return;
    if (start) {
        probe_t0[id] = amiga_eclock();
    } else {
        unsigned long d = amiga_eclock() - probe_t0[id];
        probe_acc[id] += d;
        probe_frame[id] += d;
        probe_count[id]++;
    }
}

static void stats_frame(unsigned long t_start, unsigned long t_end)
{
    unsigned long per_ms = amiga_eclock_per_ms();
    if (stats_prev_end != 0 && t_end - stats_prev_end > stats_max_dt) stats_max_dt = t_end - stats_prev_end;
    if (stats_prev_end != 0 && t_end - stats_prev_end > 150 * per_ms) {
        /* a long frame: where its time went */
        char line[256];
        int i, n = 0;
        for (i = 0; i < NPROBES; i++) {
            n += snprintf(line + n, sizeof(line) - n, " %s %lu", probe_name[i], probe_frame[i] / per_ms);
        }
        SDL_Log("stats: long frame %lu ms:%s", (t_end - stats_prev_end) / per_ms, line);
    }
    {
        int i;
        for (i = 0; i < NPROBES; i++) probe_frame[i] = 0;
    }
    stats_prev_end = t_end;
    stats_frames++;
    stats_present_ticks += t_end - t_start;
    if (stats_t0 == 0) {
        stats_t0 = t_start;
        stats_bytes0 = st_bytes;
        return;
    }
    if (t_end - stats_t0 >= 5000 * per_ms) {
        unsigned long ms = (t_end - stats_t0) / per_ms;
        SDL_Log("stats: longest frame %lu ms", stats_max_dt / per_ms);
        stats_max_dt = 0;
        {
            char line[256];
            int i, n = 0;
            for (i = 0; i < NPROBES; i++) {
                if (probe_count[i] == 0) continue;
                n += snprintf(line + n, sizeof(line) - n, " %s %lu.%lu", probe_name[i],
                              probe_acc[i] / per_ms / (stats_frames + 1),
                              (probe_acc[i] * 10 / per_ms / (stats_frames + 1)) % 10);
                probe_acc[i] = probe_count[i] = 0;
            }
            if (n > 0) SDL_Log("stats: ms per frame:%s", line);
        }
        SDL_Log("stats: %lu.%lu fps, frame %lu ms, present %lu.%lu ms, sent %lu KB/frame",
                stats_frames * 1000 / ms, (stats_frames * 10000 / ms) % 10,
                ms / stats_frames,
                stats_present_ticks / per_ms / stats_frames,
                (stats_present_ticks * 10 / per_ms / stats_frames) % 10,
                (st_bytes - stats_bytes0) / 1024 / stats_frames);
        {
            extern unsigned long r8_px_opaque, r8_px_keyed, r8_px_keynone, r8_px_general, r8_copies, r8_updates;
            unsigned long f = stats_frames;
            SDL_Log("stats: per frame %lu copies, %lu updates, Kpx opaque %lu keyed %lu keynone %lu general %lu",
                    r8_copies / f, r8_updates / f, r8_px_opaque / f / 1000, r8_px_keyed / f / 1000,
                    r8_px_keynone / f / 1000, r8_px_general / f / 1000);
            r8_px_opaque = r8_px_keyed = r8_px_keynone = r8_px_general = r8_copies = r8_updates = 0;
        }
        stats_t0 = t_end;
        stats_frames = 0;
        stats_present_ticks = 0;
        stats_bytes0 = st_bytes;
    }
}

void present_frame(const Uint32 *pixels, int pitch, int w, int h, const SDL_Rect *dirty);

void msdl_video_present(const Uint32 *pixels, int pitch, int w, int h, const SDL_Rect *dirty)
{
    unsigned long t0;
    if (stats_mode < 0) {
        char buf[8];
        stats_mode = GetVar((CONST_STRPTR)"DUNE_STATS", (STRPTR)buf, sizeof(buf), 0) >= 0;
    }
    if (!stats_mode) {
        present_frame(pixels, pitch, w, h, dirty);
        return;
    }
    t0 = amiga_eclock();
    present_frame(pixels, pitch, w, h, dirty);
    stats_frame(t0, amiga_eclock());
}

void present_frame(const Uint32 *pixels, int pitch, int w, int h, const SDL_Rect *dirty)
{
    int scale, y0, y1, x0, x1, by;
    int full = 0;
    if (the_window == NULL || the_window->win == NULL) return;
    st_frames++;

    scale = (the_window->w >= 2 * w && the_window->h >= 2 * h) ? 2 : 1;
    if (scale != last_scale) {
        struct Window *wn = the_window->win;
        last_scale = scale;
        msdl_events_set_scale(1, scale, 1, scale, the_window->screen != NULL ? 0 : wn->BorderLeft,
                              the_window->screen != NULL ? 0 : wn->BorderTop);
    }
    if (shadow == NULL || shadow_w != w * scale || shadow_h != h * scale) {
        free(shadow);
        shadow_w = w * scale;
        shadow_h = h * scale;
        shadow = (UWORD *)malloc((size_t)shadow_w * shadow_h * 2);
        if (shadow == NULL) return;
        full = 1;
    }
    if (dirty == NULL) {
        x0 = 0; y0 = 0; x1 = w; y1 = h;
    } else {
        x0 = dirty->x; y0 = dirty->y; x1 = dirty->x + dirty->w; y1 = dirty->y + dirty->h;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > w) x1 = w;
        if (y1 > h) y1 = h;
    }
    if (full) { x0 = 0; y0 = 0; x1 = w; y1 = h; }
    if (x1 <= x0 || y1 <= y0) return;

    /* bands of BAND logical rows: convert, compare with the shadow, send the
       changed column span of the band */
    for (by = y0 - (y0 % BAND); by < y1; by += BAND) {
        int ry0 = by < y0 ? y0 : by;
        int ry1 = (by + BAND) > y1 ? y1 : by + BAND;
        int cx0 = 0x7FFF, cx1 = -1;
        int y;
        for (y = ry0; y < ry1; y++) {
            const Uint32 *s = (const Uint32 *)((const Uint8 *)pixels + y * pitch);
            int x;
            if (scale == 1) {
                UWORD *d = shadow + y * shadow_w;
                for (x = x0; x < x1; x++) {
                    UWORD c = to565(s[x]);
                    if (d[x] != c || full) {
                        d[x] = c;
                        if (x < cx0) cx0 = x;
                        if (x > cx1) cx1 = x;
                    }
                }
            } else {
                UWORD *d = shadow + (2 * y) * shadow_w;
                UWORD *d2 = d + shadow_w;
                for (x = x0; x < x1; x++) {
                    UWORD c = to565(s[x]);
                    if (d[2 * x] != c || full) {
                        d[2 * x] = d[2 * x + 1] = d2[2 * x] = d2[2 * x + 1] = c;
                        if (x < cx0) cx0 = x;
                        if (x > cx1) cx1 = x;
                    }
                }
            }
        }
        if (cx1 >= cx0) {
            cx0 &= ~1;
            cx1 = (cx1 + 2) & ~1;
            if (cx1 > w) cx1 = w;
            send_rect(cx0 * scale, ry0 * scale, (cx1 - cx0) * scale, (ry1 - ry0) * scale);
        }
    }
}

/* R5G6B5 frames (sdl_render.c): no conversion; rows compared with the
   shadow a long word (two pixels) at a time from both ends, only the span
   between the first and the last difference copied and sent. */
void present_frame16(const Uint16 *pixels, int pitch, int w, int h, const SDL_Rect *dirty)
{
    int scale, y0, y1, by;
    int full = 0;
    int lw = w >> 1;            /* longs per row (w is even) */
    if (the_window == NULL || the_window->win == NULL) return;
    st_frames++;

    scale = (the_window->w >= 2 * w && the_window->h >= 2 * h) ? 2 : 1;
    if (scale != last_scale) {
        struct Window *wn = the_window->win;
        last_scale = scale;
        msdl_events_set_scale(1, scale, 1, scale, the_window->screen != NULL ? 0 : wn->BorderLeft,
                              the_window->screen != NULL ? 0 : wn->BorderTop);
    }
    if (shadow == NULL || shadow_w != w * scale || shadow_h != h * scale) {
        free(shadow);
        shadow_w = w * scale;
        shadow_h = h * scale;
        shadow = (UWORD *)malloc((size_t)shadow_w * shadow_h * 2);
        if (shadow == NULL) return;
        full = 1;
    }
    if (dirty == NULL || full) {
        y0 = 0; y1 = h;
    } else {
        y0 = dirty->y; y1 = dirty->y + dirty->h;
        if (y0 < 0) y0 = 0;
        if (y1 > h) y1 = h;
        if (dirty->w <= 0) return;
    }
    if (y1 <= y0) return;

    for (by = y0 - (y0 % BAND); by < y1; by += BAND) {
        int ry0 = by < y0 ? y0 : by;
        int ry1 = (by + BAND) > y1 ? y1 : by + BAND;
        int cx0 = 0x7FFF, cx1 = -1;     /* longs */
        int y;
        for (y = ry0; y < ry1; y++) {
            const ULONG *s = (const ULONG *)((const UBYTE *)pixels + y * pitch);
            int a, b;
            if (scale == 1) {
                ULONG *d = (ULONG *)(shadow + y * shadow_w);
                if (full) {
                    a = 0;
                    b = lw - 1;
                } else {
                    for (a = 0; a < lw && s[a] == d[a]; a++) ;
                    if (a == lw) continue;
                    for (b = lw - 1; b > a && s[b] == d[b]; b--) ;
                }
                memcpy(d + a, s + a, (b - a + 1) * 4);
            } else {
                ULONG *d = (ULONG *)(shadow + (2 * y) * shadow_w);
                ULONG *d2 = d + shadow_w / 2;
                int x;
                /* compare against the left pixels of the doubled shadow */
                if (full) {
                    a = 0;
                    b = lw - 1;
                } else {
                    for (a = 0; a < lw; a++) {
                        ULONG v = s[a];
                        if ((d[2 * a] >> 16) != (v >> 16) || (d[2 * a + 1] >> 16) != (v & 0xFFFF)) break;
                    }
                    if (a == lw) continue;
                    for (b = lw - 1; b > a; b--) {
                        ULONG v = s[b];
                        if ((d[2 * b] >> 16) != (v >> 16) || (d[2 * b + 1] >> 16) != (v & 0xFFFF)) break;
                    }
                }
                for (x = a; x <= b; x++) {
                    ULONG v = s[x];
                    ULONG p0 = (v & 0xFFFF0000u) | (v >> 16);
                    ULONG p1 = (v << 16) | (v & 0xFFFF);
                    d[2 * x] = d2[2 * x] = p0;
                    d[2 * x + 1] = d2[2 * x + 1] = p1;
                }
            }
            if (a < cx0) cx0 = a;
            if (b > cx1) cx1 = b;
        }
        if (cx1 >= cx0) {
            send_rect(cx0 * 2 * scale, ry0 * scale, (cx1 - cx0 + 1) * 2 * scale, (ry1 - ry0) * scale);
        }
    }
}

void msdl_video_present16(const Uint16 *pixels, int pitch, int w, int h, const SDL_Rect *dirty)
{
    unsigned long t0;
    if (stats_mode < 0) {
        char buf[8];
        stats_mode = GetVar((CONST_STRPTR)"DUNE_STATS", (STRPTR)buf, sizeof(buf), 0) >= 0;
    }
    if (!stats_mode) {
        present_frame16(pixels, pitch, w, h, dirty);
        return;
    }
    t0 = amiga_eclock();
    present_frame16(pixels, pitch, w, h, dirty);
    stats_frame(t0, amiga_eclock());
}

/* ---- 8-bit frames to the CLUT screen ----
   The shadow holds what the screen shows; rows are compared a long word
   (four pixels) at a time from both ends and the changed span of each band
   is written into the locked screen bitmap with long moves - VRAM is only
   written, never read (Warp RTG notes). */
void send_rect8(int x, int y, int w, int h)
{
    struct BitMap *bm;
    struct RenderInfo ri;
    LONG lock;
    st_bands++;
    st_bytes += (unsigned long)w * h;
    if (the_window->screen == NULL) return;
    bm = the_window->screen->RastPort.BitMap;
    lock = p96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
    if (lock != 0 && ri.Memory != NULL && ri.RGBFormat == RGBFB_CLUT) {
        int j;
        UBYTE *dst = (UBYTE *)ri.Memory + (LONG)y * ri.BytesPerRow + x;
        const UBYTE *src = shadow8 + y * shadow8_w + x;
        for (j = 0; j < h; j++) {
            ULONG *d = (ULONG *)dst;
            const ULONG *sl = (const ULONG *)src;
            int n = w >> 2;          /* x and w are multiples of 4 */
            while (n >= 4) {
                d[0] = sl[0]; d[1] = sl[1]; d[2] = sl[2]; d[3] = sl[3];
                d += 4; sl += 4; n -= 4;
            }
            while (n-- > 0) *d++ = *sl++;
            dst += ri.BytesPerRow;
            src += shadow8_w;
        }
        p96UnlockBitMap(bm, lock);
        return;
    }
    if (lock != 0) p96UnlockBitMap(bm, lock);
    WriteChunkyPixels(the_window->win->RPort, x, y, x + w - 1, y + h - 1, shadow8 + y * shadow8_w + x, shadow8_w);
}

/* PRESENT=FULL test (DUNE_PRESENT_FULL): the changed rows written to VRAM
   without comparing them with the shadow first. */
static int present_full = -1;

static void present_rows8(const Uint8 *pixels, int pitch, int w, int y0, int y1)
{
    struct BitMap *bm = the_window->screen->RastPort.BitMap;
    struct RenderInfo ri;
    LONG lock = p96LockBitMap(bm, (UBYTE *)&ri, sizeof(ri));
    st_bands++;
    st_bytes += (unsigned long)w * (y1 - y0);
    if (lock != 0 && ri.Memory != NULL && ri.RGBFormat == RGBFB_CLUT) {
        int y;
        for (y = y0; y < y1; y++) {
            ULONG *d = (ULONG *)((UBYTE *)ri.Memory + (LONG)y * ri.BytesPerRow);
            const ULONG *sl = (const ULONG *)(pixels + y * pitch);
            int n = w >> 2;
            while (n >= 4) {
                d[0] = sl[0]; d[1] = sl[1]; d[2] = sl[2]; d[3] = sl[3];
                d += 4; sl += 4; n -= 4;
            }
            while (n-- > 0) *d++ = *sl++;
        }
    }
    if (lock != 0) p96UnlockBitMap(bm, lock);
}

void present_frame8(const Uint8 *pixels, int pitch, int w, int h, const SDL_Rect *dirty)
{
    int y0, y1, by, full = 0;
    int lw = w >> 2;
    if (the_window == NULL || the_window->win == NULL) return;
    st_frames++;
    if (present_full < 0) {
        char buf[8];
        present_full = GetVar((CONST_STRPTR)"DUNE_PRESENT_FULL", (STRPTR)buf, sizeof(buf), 0) >= 0;
    }
    if (present_full && the_window->screen != NULL) {
        if (dirty != NULL && (dirty->w <= 0 || dirty->h <= 0)) return;
        y0 = dirty != NULL ? dirty->y : 0;
        y1 = dirty != NULL ? dirty->y + dirty->h : h;
        if (y0 < 0) y0 = 0;
        if (y1 > h) y1 = h;
        present_rows8(pixels, pitch, w, y0, y1);
        return;
    }
    if (shadow8 == NULL || shadow8_w != w || shadow8_h != h) {
        free(shadow8);
        shadow8_w = w;
        shadow8_h = h;
        shadow8 = (UBYTE *)malloc((size_t)w * h);
        if (shadow8 == NULL) return;
        full = 1;
    }
    if (dirty == NULL || full) {
        y0 = 0; y1 = h;
    } else {
        y0 = dirty->y; y1 = dirty->y + dirty->h;
        if (y0 < 0) y0 = 0;
        if (y1 > h) y1 = h;
        if (dirty->w <= 0) return;
    }
    if (y1 <= y0) return;
    for (by = y0 - (y0 % BAND); by < y1; by += BAND) {
        int ry0 = by < y0 ? y0 : by;
        int ry1 = (by + BAND) > y1 ? y1 : by + BAND;
        int cx0 = 0x7FFF, cx1 = -1, y;
        for (y = ry0; y < ry1; y++) {
            const ULONG *sl = (const ULONG *)(pixels + y * pitch);
            ULONG *d = (ULONG *)(shadow8 + y * shadow8_w);
            int a, b;
            if (full) {
                a = 0;
                b = lw - 1;
            } else {
                for (a = 0; a < lw && sl[a] == d[a]; a++) ;
                if (a == lw) continue;
                for (b = lw - 1; b > a && sl[b] == d[b]; b--) ;
            }
            memcpy(d + a, sl + a, (b - a + 1) * 4);
            if (a < cx0) cx0 = a;
            if (b > cx1) cx1 = b;
        }
        if (cx1 >= cx0) send_rect8(cx0 * 4, ry0, (cx1 - cx0 + 1) * 4, ry1 - ry0);
    }
}

void msdl_video_present8(const Uint8 *pixels, int pitch, int w, int h, const SDL_Rect *dirty)
{
    unsigned long t0;
    if (stats_mode < 0) {
        char buf[8];
        stats_mode = GetVar((CONST_STRPTR)"DUNE_STATS", (STRPTR)buf, sizeof(buf), 0) >= 0;
    }
    if (!stats_mode) {
        present_frame8(pixels, pitch, w, h, dirty);
        return;
    }
    t0 = amiga_eclock();
    present_frame8(pixels, pitch, w, h, dirty);
    stats_frame(t0, amiga_eclock());
}

void msdl_video_stats(unsigned long *frames, unsigned long *bands, unsigned long *bytes)
{
    *frames = st_frames;
    *bands = st_bands;
    *bytes = st_bytes;
}

/* ---- VRAM bitmaps for the 8-bit renderer (sdl_render8.c) ----
   Bitmaps on the card, friends of the screen's bitmap. The CPU only writes
   them while locked (one at a time); copies between them and fills are
   the card's blitter (Warp RTG notes: 514 MB/s, write-only CPU access). */
static struct BitMap *locked_bm;
static LONG locked_lock;

static int vram_on_board(struct BitMap *bm)
{
    return bm != NULL && p96GetBitMapAttr(bm, P96BMA_ISONBOARD) != 0;
}

void *msdl_vram_alloc(int w, int h)
{
    struct BitMap *bm;
    if (the_window == NULL || the_window->screen == NULL || the_window->depth != 8) return NULL;
    msdl_vram_unlock();     /* never AllocBitMap with a bitmap locked */
    bm = AllocBitMap(w, h, 8, BMF_MINPLANES | BMF_DISPLAYABLE, the_window->screen->RastPort.BitMap);
    if (!vram_on_board(bm)) {
        if (bm != NULL) FreeBitMap(bm);
        return NULL;
    }
    return bm;
}

void msdl_vram_unlock(void)
{
    if (locked_bm != NULL) {
        p96UnlockBitMap(locked_bm, locked_lock);
        locked_bm = NULL;
    }
}

void msdl_vram_free(void *bm)
{
    if (bm == NULL) return;
    if (locked_bm == (struct BitMap *)bm) msdl_vram_unlock();
    WaitBlit();
    FreeBitMap((struct BitMap *)bm);
}

/* The bitmap's memory for the CPU (stays locked until another bitmap is
   locked or a blit/fill/show); NULL when it cannot be locked. */
unsigned char *msdl_vram_lock(void *bm, int *bpr)
{
    static struct RenderInfo ri;
    if (locked_bm != (struct BitMap *)bm) {
        msdl_vram_unlock();
        WaitBlit();
        locked_lock = p96LockBitMap((struct BitMap *)bm, (UBYTE *)&ri, sizeof(ri));
        if (locked_lock == 0) return NULL;
        locked_bm = (struct BitMap *)bm;
    }
    *bpr = ri.BytesPerRow;
    return (unsigned char *)ri.Memory;
}

void msdl_vram_blit(void *src, int sx, int sy, void *dst, int dx, int dy, int w, int h)
{
    msdl_vram_unlock();
    BltBitMap((struct BitMap *)src, sx, sy, (struct BitMap *)dst, dx, dy, w, h, 0xc0, 0xff, NULL);
    st_bands++;
}

void msdl_vram_fill(void *bm, int x, int y, int w, int h, int color)
{
    struct RastPort rp;
    msdl_vram_unlock();
    InitRastPort(&rp);
    rp.BitMap = (struct BitMap *)bm;
    SetAPen(&rp, color);
    RectFill(&rp, x, y, x + w - 1, y + h - 1);
}

/* The finished frame from a VRAM bitmap onto the screen: one blit. */
void msdl_vram_show(void *bm, int w, int h)
{
    unsigned long t0 = 0;
    if (the_window == NULL || the_window->win == NULL) return;
    if (stats_mode < 0) {
        char buf[8];
        stats_mode = GetVar((CONST_STRPTR)"DUNE_STATS", (STRPTR)buf, sizeof(buf), 0) >= 0;
    }
    if (stats_mode) t0 = amiga_eclock();
    msdl_vram_unlock();
    BltBitMapRastPort((struct BitMap *)bm, 0, 0, the_window->win->RPort, 0, 0, w, h, 0xc0);
    st_frames++;
    st_bands++;
    st_bytes += (unsigned long)w * h;
    if (stats_mode) stats_frame(t0, amiga_eclock());
}
