/* Native SDL2 subset: errors, log, hints, init, version, paths, clipboard,
   mutexes, threads (not supported), time, memory, message box. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include "msdl_internal.h"
#include "amiga_wb.h"

/* libnix: this stack is set up before main(), also when started from the
   icon (which would give only the icon's stack). __stack alone does not do
   it: the code that swaps the stack (swapstack.o, __stkinit) is linked only
   when something refers to it (PORTING_PLAN, OpenDUNE 29.10.2026). */
unsigned long __stack = 1000000;
extern void __stkinit(void);
void (*const amiga_stackSwap)(void) = __stkinit;

static char error_buf[256];

/* libnix's I/O calls __chkabort(), which on Ctrl-C calls exit() from
   wherever the program is (also halfway through loading). The game gets
   Ctrl-C as SDL_QUIT from SDL_PumpEvents instead. */
void __chkabort(void) {}

int msdl_env(const char *name, char *buf, int size)
{
    return (int)GetVar((CONST_STRPTR)name, (STRPTR)buf, size, 0);
}

/* Test mode ENV:DIABLO_GOD: the player takes no damage (player.cpp). */
int msdl_test_god(void)
{
    static int god = -1;
    if (god < 0) {
        char buf[8];
        god = msdl_env("DIABLO_GOD", buf, sizeof(buf)) >= 0;
        if (god) msdl_logf(SDL_LOG_PRIORITY_INFO, "test: DIABLO_GOD - the player takes no damage");
    }
    return god;
}

/* ---- errors ---- */
const char *SDL_GetError(void) { return error_buf; }

int SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_buf, sizeof(error_buf), fmt, ap);
    va_end(ap);
    return -1;
}

void SDL_ClearError(void) { error_buf[0] = 0; }

/* ---- log ---- */
static SDL_LogOutputFunction log_fn;
static void *log_user;
static SDL_LogPriority log_priority = SDL_LOG_PRIORITY_INFO;

void SDL_LogSetOutputFunction(SDL_LogOutputFunction callback, void *userdata)
{
    log_fn = callback;
    log_user = userdata;
}

void SDL_LogSetAllPriority(SDL_LogPriority priority) { log_priority = priority; }
SDL_LogPriority SDL_LogGetPriority(int category) { (void)category; return log_priority; }

static void log_v(int category, SDL_LogPriority priority, const char *fmt, va_list ap)
{
    char buf[1024];
    if (priority < log_priority) return;
    vsnprintf(buf, sizeof(buf), fmt, ap);
    if (log_fn != NULL) {
        log_fn(log_user, category, priority, buf);
    } else if (!amiga_wb_start()) {
        printf("%s\n", buf);
    }
}

void msdl_logf(int priority, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(SDL_LOG_CATEGORY_APPLICATION, (SDL_LogPriority)priority, fmt, ap);
    va_end(ap);
}

#define LOG_FN(name, prio)                                                         \
    void name(int category, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)        \
    {                                                                              \
        va_list ap;                                                                \
        va_start(ap, fmt);                                                         \
        log_v(category, prio, fmt, ap);                                            \
        va_end(ap);                                                                \
    }
LOG_FN(SDL_LogVerbose, SDL_LOG_PRIORITY_VERBOSE)
LOG_FN(SDL_LogDebug, SDL_LOG_PRIORITY_DEBUG)
LOG_FN(SDL_LogInfo, SDL_LOG_PRIORITY_INFO)
LOG_FN(SDL_LogWarn, SDL_LOG_PRIORITY_WARN)
LOG_FN(SDL_LogError, SDL_LOG_PRIORITY_ERROR)
LOG_FN(SDL_LogCritical, SDL_LOG_PRIORITY_CRITICAL)

void SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

void SDL_LogMessage(int category, SDL_LogPriority priority, SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(category, priority, fmt, ap);
    va_end(ap);
}

/* ---- hints: accepted and ignored ---- */
SDL_bool SDL_SetHint(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return SDL_TRUE;
}

/* ---- init ---- */
static Uint32 init_flags;

int SDL_Init(Uint32 flags)
{
    init_flags |= flags;
    return 0;
}

int SDL_InitSubSystem(Uint32 flags) { return SDL_Init(flags); }
void SDL_QuitSubSystem(Uint32 flags) { init_flags &= ~flags; }
Uint32 SDL_WasInit(Uint32 flags) { return flags == 0 ? init_flags : (init_flags & flags); }

void SDL_Quit(void)
{
    msdl_video_quit();
    msdl_events_quit();
    amiga_time_close();
    init_flags = 0;
}

void SDL_GetVersion(SDL_version *ver) { SDL_VERSION(ver); }

void SDL_DisableScreenSaver(void) {}

/* ---- paths ---- */
static char *dir_with_slash(BPTR lock, const char *fallback)
{
    char *p = (char *)malloc(256);
    size_t n;
    if (p == NULL) return NULL;
    if (lock == 0 || !NameFromLock(lock, (STRPTR)p, 250)) strcpy(p, fallback);
    n = strlen(p);
    if (n > 0 && p[n - 1] != ':' && p[n - 1] != '/') strcat(p, "/");
    return p;
}

char *SDL_GetBasePath(void) { return dir_with_slash(GetProgramDir(), "PROGDIR:"); }

/* Saves and the ini next to the program unless --save-dir/--config-dir. */
char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void)org;
    (void)app;
    return SDL_GetBasePath();
}

/* ---- clipboard: none ---- */
static char empty_text[1];
char *SDL_GetClipboardText(void)
{
    char *p = (char *)malloc(1);
    if (p != NULL) p[0] = 0;
    return p != NULL ? p : empty_text;
}
SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }
int SDL_SetClipboardText(const char *text) { (void)text; return 0; }

/* ---- mutexes: Exec semaphores (recursive, like SDL's) ---- */
struct SDL_mutex {
    struct SignalSemaphore sem;
};

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *m = (SDL_mutex *)AllocVec(sizeof(SDL_mutex), MEMF_PUBLIC | MEMF_CLEAR);
    if (m == NULL) {
        SDL_SetError("out of memory");
        return NULL;
    }
    InitSemaphore(&m->sem);
    return m;
}

int SDL_LockMutex(SDL_mutex *m)
{
    if (m == NULL) return 0;
    ObtainSemaphore(&m->sem);
    return 0;
}

int SDL_UnlockMutex(SDL_mutex *m)
{
    if (m == NULL) return 0;
    ReleaseSemaphore(&m->sem);
    return 0;
}

void SDL_DestroyMutex(SDL_mutex *m)
{
    if (m != NULL) FreeVec(m);
}

/* ---- threads: not used by the Amiga build ---- */
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
    (void)fn;
    (void)name;
    (void)data;
    SDL_SetError("threads not supported");
    return NULL;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
    (void)thread;
    if (status != NULL) *status = 0;
}

SDL_threadID SDL_GetThreadID(SDL_Thread *thread)
{
    (void)thread;
    return (SDL_threadID)FindTask(NULL);
}

/* ---- time ---- */
Uint32 SDL_GetTicks(void) { return (Uint32)amiga_now_ms(); }
Uint64 SDL_GetTicks64(void) { return (Uint64)amiga_now_ms(); }
void SDL_Delay(Uint32 ms) { msdl_sleep_ms(ms); }

/* ---- memory ---- */
void *SDL_malloc(size_t size) { return malloc(size); }
void *SDL_realloc(void *mem, size_t size) { return realloc(mem, size); }
void SDL_free(void *mem) { free(mem); }
void *SDL_memcpy(SDL_OUT_BYTECAP(len) void *dst, SDL_IN_BYTECAP(len) const void *src, size_t len)
{
    return memcpy(dst, src, len);
}
void *SDL_memset(SDL_OUT_BYTECAP(len) void *dst, int c, size_t len) { return memset(dst, c, len); }

/* ---- message box ---- */
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window)
{
    struct EasyStruct es;
    (void)flags;
    (void)window;
    msdl_logf(SDL_LOG_PRIORITY_ERROR, "%s: %s", title != NULL ? title : "", message != NULL ? message : "");
    msdl_video_hide_for_requester();
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)(title != NULL ? title : "DevilutionX");
    es.es_TextFormat = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequest(NULL, &es, NULL, (ULONG)(message != NULL ? message : ""));
    return 0;
}
