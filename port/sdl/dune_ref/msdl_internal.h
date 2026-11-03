/* Internal interfaces between the mini-SDL2 parts. */
#ifndef MSDL_INTERNAL_H
#define MSDL_INTERNAL_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sdl_main / Workbench */
int msdl_wb_start(void);

/* sdl_video_amiga.c */
void msdl_video_quit(void);
void msdl_video_hide_for_requester(void);
/* The finished frame (ABGR8888, w x h, pitch bytes) to the display.
   dirty: the rectangle that may have changed since the last present
   (NULL = all). */
void msdl_video_present(const Uint32 *pixels, int pitch, int w, int h, const SDL_Rect *dirty);
/* The same for an R5G6B5 frame (sdl_render.c). */
void msdl_video_present16(const Uint16 *pixels, int pitch, int w, int h, const SDL_Rect *dirty);
/* An 8-bit frame (sdl_render8.c) to the CLUT screen. */
void msdl_video_present8(const Uint8 *pixels, int pitch, int w, int h, const SDL_Rect *dirty);
/* 8 for an 8-bit CLUT screen, 16 otherwise. */
int msdl_window_depth(SDL_Window *window);
/* The screen palette (MSDL_SetScreenPalette), 256 entries. */
const SDL_Color *msdl_screen_palette(void);
int msdl_screen_palette_set(void);
void msdl_video_load_palette(const SDL_Color *colors);
/* VRAM bitmaps (8-bit screen only): NULL when not on the card. */
void *msdl_vram_alloc(int w, int h);
void msdl_vram_free(void *bm);
unsigned char *msdl_vram_lock(void *bm, int *bpr);
void msdl_vram_unlock(void);
void msdl_vram_blit(void *src, int sx, int sy, void *dst, int dx, int dy, int w, int h);
void msdl_vram_fill(void *bm, int x, int y, int w, int h, int color);
void msdl_vram_show(void *bm, int w, int h);
/* msdl_overlay.c: the FPS counter */
extern int msdl_fps_on;
void msdl_fps_tick(void);
void msdl_fps_draw8(Uint8 *px, int pitch, int w, int h, Uint8 fg, Uint8 bg);
void msdl_fps_draw16(Uint16 *px, int pitch, int w, int h);
void msdl_sleep_ms(Uint32 ms);

/* sdl_events_amiga.c: reads the window's IDCMP port. */
void msdl_events_attach(void *window);   /* struct Window * */
void msdl_events_quit(void);
/* Mouse position of the window in logical coordinates. */
void msdl_events_set_scale(int sx_num, int sx_den, int sy_num, int sy_den, int ox, int oy);

/* sdl_surface.c */
int msdl_bpp_of_format(Uint32 format);

#ifdef __cplusplus
}
#endif

#endif
