/* Native SDL2 subset: events. The window's IDCMP messages become SDL events
   (mouse, keys, text, wheel, focus, close); Ctrl-C quits. SDL_Delay's
   timer. Test input: ENV:DIABLO_SCRIPT=file or, live, RAM:diablo-cmd
   (checked every 0.5 s, deleted after reading); one event per line, the
   first number the delay in ms after the previous line:
     "500 click 320 240", "200 rclick 10 10", "100 move 5 5",
     "300 key return" (return esc space tab backspace up down left right
     f1..f10 or one character), "1000 mark text" (a log line), "0 profon" / "0 profoff"
     (profiler -> RAM:diablo-prof.txt), "0 stats" (frame statistics),
     "0 press x y" / "0 release" (left button held: walk), "0 quit". */
#include <exec/types.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msdl_internal.h"

struct Library *KeymapBase;

static struct Window *win;
static int scale_num = 1, scale_den = 1, off_x, off_y;

#define QSIZE 256
static SDL_Event queue[QSIZE];
static int q_head, q_tail;

static Uint8 keystate[SDL_NUM_SCANCODES];
static Uint16 modstate;
static int mouse_x, mouse_y;
static Uint32 mouse_buttons;
static Uint32 next_user_event = SDL_USEREVENT;

void msdl_events_attach(void *window)
{
    win = (struct Window *)window;
    if (KeymapBase == NULL) KeymapBase = OpenLibrary((CONST_STRPTR)"keymap.library", 37);
}

void msdl_events_quit(void)
{
    win = NULL;
    if (KeymapBase != NULL) {
        CloseLibrary(KeymapBase);
        KeymapBase = NULL;
    }
}

void msdl_events_set_scale(int num, int den, int ox, int oy)
{
    scale_num = num;
    scale_den = den;
    off_x = ox;
    off_y = oy;
}

static void push(const SDL_Event *e)
{
    int next = (q_tail + 1) % QSIZE;
    if (next == q_head) return;  /* full: drop */
    queue[q_tail] = *e;
    q_tail = next;
}

int SDL_PushEvent(SDL_Event *event)
{
    push(event);
    return 1;
}

Uint32 SDL_RegisterEvents(int numevents)
{
    Uint32 first = next_user_event;
    if (numevents <= 0 || next_user_event + numevents > SDL_LASTEVENT) return (Uint32)-1;
    next_user_event += numevents;
    return first;
}

void msdl_events_push_window(Uint8 event)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_WINDOWEVENT;
    e.window.event = event;
    e.window.windowID = 1;
    push(&e);
}

/* Amiga raw key -> SDL keycode and scancode for the keys that are not plain
   characters. */
static SDL_Keycode special_key(UWORD code, SDL_Scancode *sc)
{
    *sc = SDL_SCANCODE_UNKNOWN;
    switch (code) {
        case 0x45: *sc = SDL_SCANCODE_ESCAPE; return SDLK_ESCAPE;
        case 0x44: *sc = SDL_SCANCODE_RETURN; return SDLK_RETURN;
        case 0x43: *sc = SDL_SCANCODE_KP_ENTER; return SDLK_KP_ENTER;
        case 0x41: *sc = SDL_SCANCODE_BACKSPACE; return SDLK_BACKSPACE;
        case 0x42: *sc = SDL_SCANCODE_TAB; return SDLK_TAB;
        case 0x40: *sc = SDL_SCANCODE_SPACE; return SDLK_SPACE;
        case 0x46: *sc = SDL_SCANCODE_DELETE; return SDLK_DELETE;
        case 0x4C: *sc = SDL_SCANCODE_UP; return SDLK_UP;
        case 0x4D: *sc = SDL_SCANCODE_DOWN; return SDLK_DOWN;
        case 0x4E: *sc = SDL_SCANCODE_RIGHT; return SDLK_RIGHT;
        case 0x4F: *sc = SDL_SCANCODE_LEFT; return SDLK_LEFT;
        case 0x4A: *sc = SDL_SCANCODE_KP_MINUS; return SDLK_KP_MINUS;
        case 0x5E: *sc = SDL_SCANCODE_KP_PLUS; return SDLK_KP_PLUS;
        case 0x5F: *sc = SDL_SCANCODE_F11; return SDLK_F11;   /* Help */
        case 0x60: *sc = SDL_SCANCODE_LSHIFT; return SDLK_LSHIFT;
        case 0x61: *sc = SDL_SCANCODE_RSHIFT; return SDLK_RSHIFT;
        case 0x62: *sc = SDL_SCANCODE_CAPSLOCK; return SDLK_CAPSLOCK;
        case 0x63: *sc = SDL_SCANCODE_LCTRL; return SDLK_LCTRL;
        case 0x64: *sc = SDL_SCANCODE_LALT; return SDLK_LALT;
        case 0x65: *sc = SDL_SCANCODE_RALT; return SDLK_RALT;
        case 0x66: *sc = SDL_SCANCODE_LGUI; return SDLK_LGUI;
        case 0x67: *sc = SDL_SCANCODE_RGUI; return SDLK_RGUI;
        default: break;
    }
    if (code >= 0x50 && code <= 0x59) {
        *sc = (SDL_Scancode)(SDL_SCANCODE_F1 + (code - 0x50));
        return SDLK_F1 + (code - 0x50);
    }
    return SDLK_UNKNOWN;
}

static SDL_Scancode char_scancode(int c)
{
    if (c >= 'a' && c <= 'z') return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'a'));
    if (c >= '1' && c <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'));
    switch (c) {
        case '0': return SDL_SCANCODE_0;
        case '-': return SDL_SCANCODE_MINUS;
        case '=': return SDL_SCANCODE_EQUALS;
        case '[': return SDL_SCANCODE_LEFTBRACKET;
        case ']': return SDL_SCANCODE_RIGHTBRACKET;
        case '\\': return SDL_SCANCODE_BACKSLASH;
        case ';': return SDL_SCANCODE_SEMICOLON;
        case '\'': return SDL_SCANCODE_APOSTROPHE;
        case '`': return SDL_SCANCODE_GRAVE;
        case ',': return SDL_SCANCODE_COMMA;
        case '.': return SDL_SCANCODE_PERIOD;
        case '/': return SDL_SCANCODE_SLASH;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
    static const UWORD raws[] = { 0x45, 0x44, 0x43, 0x41, 0x42, 0x40, 0x46, 0x4C, 0x4D, 0x4E, 0x4F, 0x4A, 0x5E, 0x5F,
                                  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
                                  0x56, 0x57, 0x58, 0x59 };
    unsigned i;
    for (i = 0; i < sizeof(raws) / sizeof(raws[0]); i++) {
        SDL_Scancode sc;
        if (special_key(raws[i], &sc) == key) return sc;
    }
    if (key >= 0 && key < 128) return char_scancode((int)key);
    return SDL_SCANCODE_UNKNOWN;
}

static int map_raw(UWORD code, UWORD qual, APTR iaddr)
{
    struct InputEvent ie;
    char buf[4];
    if (KeymapBase == NULL) return 0;
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code & ~IECODE_UP_PREFIX;
    ie.ie_Qualifier = qual;
    ie.ie_EventAddress = iaddr != NULL ? *(APTR *)iaddr : NULL;
    if (MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL) == 1) return (unsigned char)buf[0];
    return 0;
}

static Uint16 sdl_mods(UWORD qual)
{
    Uint16 m = 0;
    if (qual & IEQUALIFIER_LSHIFT) m |= KMOD_LSHIFT;
    if (qual & IEQUALIFIER_RSHIFT) m |= KMOD_RSHIFT;
    if (qual & IEQUALIFIER_CONTROL) m |= KMOD_LCTRL;
    if (qual & IEQUALIFIER_LALT) m |= KMOD_LALT;
    if (qual & IEQUALIFIER_RALT) m |= KMOD_RALT;
    if (qual & IEQUALIFIER_LCOMMAND) m |= KMOD_LGUI;
    if (qual & IEQUALIFIER_RCOMMAND) m |= KMOD_RGUI;
    if (qual & IEQUALIFIER_CAPSLOCK) m |= KMOD_CAPS;
    return m;
}

static void to_logical(int wx, int wy, int *x, int *y)
{
    *x = (wx - off_x) * scale_num / scale_den;
    *y = (wy - off_y) * scale_num / scale_den;
}

/* ---- test script ---- */
#define MAX_SCRIPT 512
typedef struct { Uint32 delay; char op[8]; int a, b; char text[48]; } ScriptLine;
static ScriptLine *script;
static int script_n, script_i, script_loaded, script_release, script_mode;
static Uint32 script_due, script_release_at, script_poll;

static void script_read(const char *path)
{
    FILE *f;
    char line[128];
    int first = script_n;
    f = fopen(path, "r");
    if (f == NULL) return;
    if (script == NULL) script = (ScriptLine *)calloc(MAX_SCRIPT, sizeof(ScriptLine));
    if (script_i >= script_n) {   /* everything done: start over */
        script_i = script_n = first = 0;
    }
    while (script != NULL && script_n < MAX_SCRIPT && fgets(line, sizeof(line), f) != NULL) {
        ScriptLine *l = &script[script_n];
        unsigned long d;
        char *t;
        if (line[0] == '#' || sscanf(line, "%lu %7s", &d, l->op) != 2) continue;
        l->delay = (Uint32)d;
        t = strstr(line, l->op) + strlen(l->op);
        if (strcmp(l->op, "key") == 0 || strcmp(l->op, "mark") == 0) {
            while (*t == ' ') t++;
            strncpy(l->text, t, sizeof(l->text) - 1);
            l->text[strcspn(l->text, "\r\n")] = 0;
        } else {
            l->a = (int)strtol(t, &t, 10);
            l->b = (int)strtol(t, &t, 10);
        }
        script_n++;
    }
    fclose(f);
    msdl_logf(SDL_LOG_PRIORITY_INFO, "script: %d events from %s", script_n - first, path);
    if (first == script_i && script_n > first) script_due = SDL_GetTicks() + script[first].delay;
}

static void script_load(void)
{
    char path[256];
    script_loaded = 1;
    script_mode = 1;    /* RAM:diablo-cmd is always polled */
    if (msdl_env("DIABLO_SCRIPT", path, sizeof(path)) < 0) return;
    script_read(path);
}

static void script_poll_file(void)
{
    BPTR l;
    Uint32 now = SDL_GetTicks();
    if ((Sint32)(now - script_poll) < 500) return;
    script_poll = now;
    l = Lock((CONST_STRPTR)"RAM:diablo-cmd", ACCESS_READ);
    if (l == 0) return;
    UnLock(l);
    script_read("RAM:diablo-cmd");
    DeleteFile((CONST_STRPTR)"RAM:diablo-cmd");
}

static SDL_Keycode script_key(const char *t, SDL_Scancode *sc)
{
    static const struct { const char *n; UWORD raw; } names[] = {
        {"return", 0x44}, {"esc", 0x45}, {"space", 0x40}, {"tab", 0x42}, {"backspace", 0x41},
        {"up", 0x4C}, {"down", 0x4D}, {"right", 0x4E}, {"left", 0x4F}, {"f1", 0x50}, {"f2", 0x51},
        {"f3", 0x52}, {"f4", 0x53}, {"f5", 0x54}, {"f6", 0x55}, {"f7", 0x56}, {"f8", 0x57},
        {"f9", 0x58}, {"f10", 0x59}
    };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(t, names[i].n) == 0) return special_key(names[i].raw, sc);
    }
    *sc = char_scancode((unsigned char)t[0]);
    return (SDL_Keycode)(unsigned char)t[0];
}

static void script_step(void)
{
    SDL_Event e;
    if (!script_loaded) script_load();
    if (!script_mode) return;
    script_poll_file();
    if (script_release && (Sint32)(SDL_GetTicks() - script_release_at) < 0) return;
    if (script_release) {     /* button up / key up 100 ms after the press */
        memset(&e, 0, sizeof(e));
        if (script_release < 0) {
            SDL_Scancode sc;
            e.type = SDL_KEYUP;
            e.key.keysym.sym = (SDL_Keycode)(-script_release - 1);
            sc = SDL_GetScancodeFromKey(e.key.keysym.sym);
            e.key.keysym.scancode = sc;
            if (sc != SDL_SCANCODE_UNKNOWN) keystate[sc] = 0;
        } else {
            e.type = SDL_MOUSEBUTTONUP;
            e.button.button = (Uint8)script_release;
            e.button.x = mouse_x;
            e.button.y = mouse_y;
            mouse_buttons &= ~SDL_BUTTON(script_release);
        }
        push(&e);
        script_release = 0;
        return;
    }
    while (script_i < script_n && (Sint32)(SDL_GetTicks() - script_due) >= 0) {
        ScriptLine *l = &script[script_i++];
        if (script_i < script_n) script_due += script[script_i].delay;
        memset(&e, 0, sizeof(e));
        if (strcmp(l->op, "move") == 0 || strcmp(l->op, "click") == 0 || strcmp(l->op, "rclick") == 0) {
            e.type = SDL_MOUSEMOTION;
            e.motion.x = l->a;
            e.motion.y = l->b;
            e.motion.xrel = l->a - mouse_x;
            e.motion.yrel = l->b - mouse_y;
            mouse_x = l->a;
            mouse_y = l->b;
            push(&e);
            if (l->op[0] != 'm') {
                Uint8 b = l->op[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                memset(&e, 0, sizeof(e));
                e.type = SDL_MOUSEBUTTONDOWN;
                e.button.button = b;
                e.button.state = SDL_PRESSED;
                e.button.clicks = 1;
                e.button.x = l->a;
                e.button.y = l->b;
                mouse_buttons |= SDL_BUTTON(b);
                push(&e);
                script_release = b;
                script_release_at = SDL_GetTicks() + 100;
                msdl_logf(SDL_LOG_PRIORITY_INFO, "script: %s %d %d", l->op, l->a, l->b);
                return;
            }
        } else if (strcmp(l->op, "key") == 0) {
            SDL_Scancode sc;
            SDL_Keycode k = script_key(l->text, &sc);
            e.type = SDL_KEYDOWN;
            e.key.state = SDL_PRESSED;
            e.key.keysym.sym = k;
            e.key.keysym.scancode = sc;
            if (sc != SDL_SCANCODE_UNKNOWN) keystate[sc] = 1;
            push(&e);
            if (k >= 32 && k < 127) {
                SDL_Event t;
                memset(&t, 0, sizeof(t));
                t.type = SDL_TEXTINPUT;
                t.text.text[0] = (char)k;
                push(&t);
            }
            script_release = -(int)k - 1;
            script_release_at = SDL_GetTicks() + 100;
            msdl_logf(SDL_LOG_PRIORITY_INFO, "script: key %s", l->text);
            return;
        } else if (strcmp(l->op, "mark") == 0) {
            msdl_logf(SDL_LOG_PRIORITY_INFO, "mark: %s", l->text);
        } else if (strcmp(l->op, "press") == 0) {
            /* left button down at a b and kept down (walk towards it) */
            e.type = SDL_MOUSEMOTION;
            e.motion.x = l->a;
            e.motion.y = l->b;
            e.motion.xrel = l->a - mouse_x;
            e.motion.yrel = l->b - mouse_y;
            mouse_x = l->a;
            mouse_y = l->b;
            push(&e);
            memset(&e, 0, sizeof(e));
            e.type = SDL_MOUSEBUTTONDOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.state = SDL_PRESSED;
            e.button.clicks = 1;
            e.button.x = l->a;
            e.button.y = l->b;
            mouse_buttons |= SDL_BUTTON(SDL_BUTTON_LEFT);
            push(&e);
            msdl_logf(SDL_LOG_PRIORITY_INFO, "script: press %d %d", l->a, l->b);
        } else if (strcmp(l->op, "release") == 0) {
            e.type = SDL_MOUSEBUTTONUP;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = mouse_x;
            e.button.y = mouse_y;
            mouse_buttons &= ~SDL_BUTTON(SDL_BUTTON_LEFT);
            push(&e);
        } else if (strcmp(l->op, "crash") == 0) {
            /* test of the crash report (msdl_core.c): a call to address 0 */
            void (*volatile nowhere)(void) = 0;
            nowhere();
        } else if (strcmp(l->op, "profon") == 0) {
            msdl_prof_on();
        } else if (strcmp(l->op, "profoff") == 0) {
            msdl_prof_off();
        } else if (strcmp(l->op, "stats") == 0) {
            msdl_stats_toggle();
        } else if (strcmp(l->op, "quit") == 0) {
            e.type = SDL_QUIT;
            push(&e);
        }
    }
}

extern void (*amiga_snd_pump_fn)(void);

void SDL_PumpEvents(void)
{
    struct IntuiMessage *m;
    SDL_Event e;
    /* the music stream is topped up from the game's event loop (amiga_sound.c) */
    if (amiga_snd_pump_fn != NULL) amiga_snd_pump_fn();

    msdl_prof_check_env();
    script_step();

    if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
        SetSignal(0, SIGBREAKF_CTRL_C);
        memset(&e, 0, sizeof(e));
        e.type = SDL_QUIT;
        push(&e);
    }
    if (win == NULL) return;

    while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        UWORD qual = m->Qualifier;
        APTR iaddr = m->IAddress;
        int x, y;
        to_logical(m->MouseX, m->MouseY, &x, &y);
        ReplyMsg((struct Message *)m);

        modstate = sdl_mods(qual);
        memset(&e, 0, sizeof(e));
        switch (cls) {
            case IDCMP_MOUSEMOVE: {
                /* several moves in a row: keep only the last */
                int prev = (q_tail + QSIZE - 1) % QSIZE;
                e.type = SDL_MOUSEMOTION;
                e.motion.x = x;
                e.motion.y = y;
                e.motion.xrel = x - mouse_x;
                e.motion.yrel = y - mouse_y;
                e.motion.state = mouse_buttons;
                mouse_x = x;
                mouse_y = y;
                if (q_head != q_tail && queue[prev].type == SDL_MOUSEMOTION) {
                    e.motion.xrel += queue[prev].motion.xrel;
                    e.motion.yrel += queue[prev].motion.yrel;
                    queue[prev] = e;
                } else {
                    push(&e);
                }
                break;
            }
            case IDCMP_MOUSEBUTTONS: {
                Uint8 b;
                int down;
                switch (code) {
                    case SELECTDOWN: b = SDL_BUTTON_LEFT; down = 1; break;
                    case SELECTUP: b = SDL_BUTTON_LEFT; down = 0; break;
                    case MIDDLEDOWN: b = SDL_BUTTON_MIDDLE; down = 1; break;
                    case MIDDLEUP: b = SDL_BUTTON_MIDDLE; down = 0; break;
                    case MENUDOWN: b = SDL_BUTTON_RIGHT; down = 1; break;
                    case MENUUP: b = SDL_BUTTON_RIGHT; down = 0; break;
                    default: continue;
                }
                if (down) mouse_buttons |= SDL_BUTTON(b);
                else mouse_buttons &= ~SDL_BUTTON(b);
                mouse_x = x;
                mouse_y = y;
                e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                e.button.button = b;
                e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
                e.button.clicks = 1;
                e.button.x = x;
                e.button.y = y;
                push(&e);
                break;
            }
            case IDCMP_RAWKEY: {
                UWORD raw = code & ~IECODE_UP_PREFIX;
                int down = (code & IECODE_UP_PREFIX) == 0;
                SDL_Scancode sc;
                SDL_Keycode sym;
                if (raw == 0x7A || raw == 0x7B) {   /* NewMouse wheel */
                    if (down) {
                        e.type = SDL_MOUSEWHEEL;
                        e.wheel.y = raw == 0x7A ? 1 : -1;
                        push(&e);
                    }
                    break;
                }
                sym = special_key(raw, &sc);
                if (sym == SDLK_UNKNOWN) {
                    int c = map_raw(raw, 0, iaddr);   /* the key without qualifiers */
                    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                    if (c == 0) break;
                    sym = c;
                    sc = char_scancode(c);
                }
                if (sc != SDL_SCANCODE_UNKNOWN) keystate[sc] = down ? 1 : 0;
                e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
                e.key.repeat = (qual & IEQUALIFIER_REPEAT) ? 1 : 0;
                e.key.windowID = 1;
                e.key.keysym.scancode = sc;
                e.key.keysym.sym = sym;
                e.key.keysym.mod = modstate;
                push(&e);
                if (down && !(qual & (IEQUALIFIER_CONTROL | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
                    int c = map_raw(raw, qual, iaddr);
                    if (c >= 32 && c != 127) {
                        SDL_Event t;
                        memset(&t, 0, sizeof(t));
                        t.type = SDL_TEXTINPUT;
                        t.text.windowID = 1;
                        if (c < 128) {
                            t.text.text[0] = (char)c;
                        } else {   /* Latin-1 -> UTF-8 */
                            t.text.text[0] = (char)(0xC0 | (c >> 6));
                            t.text.text[1] = (char)(0x80 | (c & 0x3F));
                        }
                        push(&t);
                    }
                }
                break;
            }
            case IDCMP_ACTIVEWINDOW:
                msdl_events_push_window(SDL_WINDOWEVENT_FOCUS_GAINED);
                break;
            case IDCMP_INACTIVEWINDOW:
                msdl_events_push_window(SDL_WINDOWEVENT_FOCUS_LOST);
                break;
            case IDCMP_CLOSEWINDOW:
                e.type = SDL_QUIT;
                push(&e);
                break;
            default:
                break;
        }
    }
}

int SDL_PollEvent(SDL_Event *event)
{
    if (q_head == q_tail) SDL_PumpEvents();
    if (q_head == q_tail) return 0;
    if (event != NULL) {
        *event = queue[q_head];
        q_head = (q_head + 1) % QSIZE;
    }
    return 1;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys != NULL) *numkeys = SDL_NUM_SCANCODES;
    return keystate;
}

SDL_Keymod SDL_GetModState(void) { return (SDL_Keymod)modstate; }

Uint32 SDL_GetMouseState(int *x, int *y)
{
    if (x != NULL) *x = mouse_x;
    if (y != NULL) *y = mouse_y;
    return mouse_buttons;
}

void SDL_StartTextInput(void) {}
void SDL_StopTextInput(void) {}
void SDL_SetTextInputRect(const SDL_Rect *rect) { (void)rect; }

/* ---- SDL_Delay ---- */
static struct MsgPort *delay_port;
static struct timerequest *delay_req;
static int delay_open;

void msdl_sleep_ms(Uint32 ms)
{
    if (ms == 0) return;
    if (!delay_open) {
        delay_port = CreateMsgPort();
        if (delay_port != NULL) delay_req = (struct timerequest *)CreateIORequest(delay_port, sizeof(struct timerequest));
        if (delay_req == NULL || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)delay_req, 0) != 0) {
            Delay((ms + 19) / 20);
            return;
        }
        delay_open = 1;
    }
    delay_req->tr_node.io_Command = TR_ADDREQUEST;
    delay_req->tr_time.tv_secs = ms / 1000;
    delay_req->tr_time.tv_micro = (ms % 1000) * 1000;
    DoIO((struct IORequest *)delay_req);
}
