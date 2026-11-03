/* Mini-SDL2: errors, log, init, time, threads, atomics, message box. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <SDL.h>
#include "amiga_time.h"
#include "msdl_internal.h"

static char error_buf[256];

/* libnix's I/O calls __chkabort(), which on Ctrl-C calls exit() from
   wherever the program is (also halfway through loading). The game gets
   Ctrl-C as SDL_QUIT from SDL_PumpEvents instead. */
void __chkabort(void) {}

const char *SDL_GetError(void) { return error_buf; }

int SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_buf, sizeof(error_buf), fmt, ap);
    va_end(ap);
    return -1;
}

/* ---- log ---- */
static SDL_LogOutputFunction log_fn;
static void *log_user;

void SDL_LogSetOutputFunction(SDL_LogOutputFunction callback, void *userdata)
{
    log_fn = callback;
    log_user = userdata;
}
void SDL_LogSetAllPriority(SDL_LogPriority priority) { (void)priority; }
void SDL_LogSetPriority(int category, SDL_LogPriority priority) { (void)category; (void)priority; }

void SDL_Log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;
    /* milliseconds since the first line, for load-time profiling */
    static Uint32 t0;
    Uint32 now = SDL_GetTicks();
    if (t0 == 0) t0 = now ? now : 1;
    n = snprintf(buf, sizeof(buf), "[%6lu] ", (unsigned long)(now - t0));
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (log_fn != NULL) {
        log_fn(log_user, SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, buf);
    } else if (!msdl_wb_start()) {
        printf("%s\n", buf);
    }
}

/* ---- init ---- */
extern void msdl_prof_start(void);

int SDL_Init(Uint32 flags)
{
    (void)flags;
    msdl_prof_start();
    return 0;
}

void SDL_Quit(void)
{
    msdl_video_quit();
    msdl_events_quit();
    amiga_time_close();
}

void SDL_GetVersion(SDL_version *ver) { SDL_VERSION(ver); }
const char *SDL_GetPlatform(void) { return "AmigaOS"; }

char *SDL_GetBasePath(void)
{
    char *p = (char *)malloc(256);
    if (p == NULL) return NULL;
    if (!NameFromLock(GetProgramDir(), (STRPTR)p, 250)) strcpy(p, "PROGDIR:");
    {
        size_t n = strlen(p);
        if (n > 0 && p[n - 1] != ':' && p[n - 1] != '/') strcat(p, "/");
    }
    return p;
}

/* ---- time ---- */
Uint32 SDL_GetTicks(void) { return (Uint32)amiga_now_ms(); }

void SDL_Delay(Uint32 ms)
{
    /* dos Delay() counts 1/50 s; shorter waits through the timer. */
    msdl_sleep_ms(ms);
}

/* ---- threads: the game only starts one for the XMI (CAMD) player,
   which the Amiga build does not use. ---- */
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
    (void)fn; (void)name; (void)data;
    SDL_SetError("threads not supported");
    return NULL;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
    (void)thread;
    if (status != NULL) *status = 0;
}

int SDL_AtomicSet(SDL_atomic_t *a, int v)
{
    int old;
    Disable();
    old = a->value;
    a->value = v;
    Enable();
    return old;
}

int SDL_AtomicGet(SDL_atomic_t *a) { return a->value; }

/* ---- message box ---- */
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window)
{
    struct EasyStruct es;
    (void)flags;
    (void)window;
    SDL_Log("%s: %s", title != NULL ? title : "", message != NULL ? message : "");
    msdl_video_hide_for_requester();
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)title;
    es.es_TextFormat = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequest(NULL, &es, NULL, (ULONG)message);
    return 0;
}

int MSDL_EnvSet(const char *name)
{
    char buf[8];
    return GetVar((CONST_STRPTR)name, (STRPTR)buf, sizeof(buf), 0) >= 0;
}
