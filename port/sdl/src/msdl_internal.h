/* Native SDL2 subset for DevilutionX on AmigaOS 3 + Picasso96 (Warp RTG).
   Built against the real SDL2 headers (sdl2inc/); only the functions the
   game uses are implemented. Internal interfaces between the parts. */
#ifndef MSDL_INTERNAL_H
#define MSDL_INTERNAL_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The opaque SDL_BlitMap: here only the surface's colour key. */
struct SDL_BlitMap {
    int has_colorkey;
    Uint32 colorkey;
};

/* msdl_core.c */
int msdl_env(const char *name, char *buf, int size);   /* ENV: variable, length or -1 */
void msdl_logf(int priority, const char *fmt, ...);

/* msdl_video.c */
void msdl_video_quit(void);
void msdl_video_hide_for_requester(void);
void msdl_vram_unlock(void);

/* msdl_events.c */
void msdl_events_attach(void *window);   /* struct Window *, NULL to detach */
void msdl_events_quit(void);
void msdl_events_set_scale(int num, int den, int ox, int oy);
void msdl_events_push_window(Uint8 event);
void msdl_sleep_ms(Uint32 ms);

/* msdl_prof.c */
void msdl_prof_on(void);
void msdl_prof_off(void);
void msdl_prof_check_env(void);

/* msdl_video.c: frame statistics to the log every 5 s (ENV:DIABLO_STATS or
   "stats" in the test input) */
void msdl_stats_toggle(void);

/* msdl_cells.c: changed-cell measurement (ENV:DIABLO_CELLS) */
void msdl_cells_frame(const Uint8 *pixels, int pitch, int w, int h);

/* msdl_view.c: changes of the view since the last present (cell renderer) */
void msdl_view_scene(int dx, int dy, int vw, int vh, const int *rects, int n, int full);
void msdl_view_overlay(int x, int y, int w, int h);
void msdl_view_overlay_all(void);
int msdl_view_take_damage(int *out4, int max);
void msdl_view_present(const Uint8 *pix, int pitch, int w, int h);
int msdl_view_upload(void (*shift)(int dx, int dy, int vw, int vh), void (*send)(int x, int y, int w, int h));
int msdl_view_verify_on(void);

/* msdl_surface.c */
SDL_PixelFormat *msdl_alloc_format(Uint32 format);
void msdl_free_format(SDL_PixelFormat *f);

/* amiga_time.c */
unsigned long amiga_now_ms(void);
void amiga_time_close(void);
unsigned long amiga_eclock(void);
unsigned long amiga_eclock_per_ms(void);

#ifdef __cplusplus
}
#endif

#endif
