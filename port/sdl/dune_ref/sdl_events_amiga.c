/* Mini-SDL2 events for AmigaOS: the window's IDCMP messages become SDL
   events (mouse, keys, text, wheel, close); Ctrl-C quits. Also SDL_Delay. */
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
#include <SDL.h>
#include "msdl_internal.h"

extern void msdl_prof_on(void);
extern void msdl_prof_off(void);

struct Library *KeymapBase;

static struct Window *win;
static int scale_num = 1, scale_den = 1, off_x, off_y;

#define QSIZE 128
static SDL_Event queue[QSIZE];
static int q_head, q_tail;

static Uint8 keystate[SDL_NUM_SCANCODES];
static Uint16 modstate;
static int mouse_x, mouse_y;
static Uint32 mouse_buttons;

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

void msdl_events_set_scale(int sx_num, int sx_den, int sy_num, int sy_den, int ox, int oy)
{
    (void)sy_num;
    (void)sy_den;
    scale_num = sx_num;
    scale_den = sx_den;
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

/* Amiga raw key -> SDL keycode and scancode for the keys that are not plain
   characters. */
static SDL_Keycode special_key(UWORD code, SDL_Scancode *sc)
{
    *sc = SDL_SCANCODE_UNKNOWN;
    switch (code) {
        case 0x45: *sc = SDL_SCANCODE_ESCAPE; return SDLK_ESCAPE;
        case 0x44: case 0x43: *sc = SDL_SCANCODE_RETURN; return SDLK_RETURN;
        case 0x41: *sc = SDL_SCANCODE_BACKSPACE; return SDLK_BACKSPACE;
        case 0x42: *sc = SDL_SCANCODE_TAB; return SDLK_TAB;
        case 0x40: *sc = SDL_SCANCODE_SPACE; return SDLK_SPACE;
        case 0x46: *sc = (SDL_Scancode)76; return SDLK_DELETE;
        case 0x4C: *sc = SDL_SCANCODE_UP; return SDLK_UP;
        case 0x4D: *sc = SDL_SCANCODE_DOWN; return SDLK_DOWN;
        case 0x4E: *sc = SDL_SCANCODE_RIGHT; return SDLK_RIGHT;
        case 0x4F: *sc = SDL_SCANCODE_LEFT; return SDLK_LEFT;
        case 0x4A: *sc = (SDL_Scancode)86; return SDLK_KP_MINUS;
        case 0x5E: *sc = (SDL_Scancode)87; return SDLK_KP_PLUS;
        case 0x5F: *sc = (SDL_Scancode)68; return SDLK_F11;   /* Help */
        default: break;
    }
    if (code >= 0x50 && code <= 0x59) {
        *sc = (SDL_Scancode)(SDL_SCANCODE_F1 + (code - 0x50));
        return SDLK_F1 + (code - 0x50);
    }
    return SDLK_UNKNOWN;
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

/* ---- test script (DUNE_SCRIPT=file): synthetic input for automated runs ----
   One event per line, the first number is the delay in ms after the
   previous line:  "500 click 320 240", "200 rclick 10 10", "100 move 5 5",
   "300 key return" (return esc space tab up down left right f1..f12 or one
   character), "1000 mark text" (a log line, for timing), "0 quit". */
#define MAX_SCRIPT 512
typedef struct { Uint32 delay; char op[8]; int a, b; char text[48]; } ScriptLine;
static ScriptLine *script;
static int script_n, script_i, script_loaded, script_release;
static Uint32 script_due, script_release_at;

static int script_mode;
static Uint32 script_poll;

/* Appends the lines of a file to the script (from where it stands now). */
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
        if (line[0] == '#' || sscanf(line, "%lu %7s", &d, l->op) != 2) continue;
        l->delay = (Uint32)d;
        if (strcmp(l->op, "key") == 0 || strcmp(l->op, "mark") == 0) {
            char *t = strstr(line, l->op) + strlen(l->op);
            while (*t == ' ') t++;
            strncpy(l->text, t, sizeof(l->text) - 1);
            l->text[strcspn(l->text, "\r\n")] = 0;
        } else {
            /* libnix's sscanf() does not skip with %*: parse by hand */
            char *t = strstr(line, l->op) + strlen(l->op);
            l->a = (int)strtol(t, &t, 10);
            l->b = (int)strtol(t, &t, 10);
        }
        script_n++;
    }
    fclose(f);
    SDL_Log("script: %d events from %s", script_n - first, path);
    if (first == script_i && script_n > first) script_due = SDL_GetTicks() + script[first].delay;
}

static void script_load(void)
{
    char path[256];
    script_loaded = 1;
    if (GetVar((CONST_STRPTR)"DUNE_SCRIPT", (STRPTR)path, sizeof(path), 0) < 0) return;
    script_mode = 1;
    script_read(path);
}

/* Test mode: RAM:dune-cmd, written through AmiAgent, is run and deleted. */
static void script_poll_file(void)
{
    BPTR l;
    Uint32 now = SDL_GetTicks();
    if ((Sint32)(now - script_poll) < 500) return;
    script_poll = now;
    l = Lock((CONST_STRPTR)"RAM:dune-cmd", ACCESS_READ);
    if (l == 0) return;
    UnLock(l);
    script_read("RAM:dune-cmd");
    DeleteFile((CONST_STRPTR)"RAM:dune-cmd");
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
    *sc = (t[0] >= 'a' && t[0] <= 'z') ? (SDL_Scancode)(SDL_SCANCODE_A + (t[0] - 'a')) : SDL_SCANCODE_UNKNOWN;
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
            e.type = SDL_KEYUP;
            e.key.keysym.sym = (SDL_Keycode)(-script_release - 1);
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
                SDL_Log("script: %s %d %d", l->op, l->a, l->b);
                return;
            }
        } else if (strcmp(l->op, "key") == 0) {
            SDL_Scancode sc;
            SDL_Keycode k = script_key(l->text, &sc);
            e.type = SDL_KEYDOWN;
            e.key.state = SDL_PRESSED;
            e.key.keysym.sym = k;
            e.key.keysym.scancode = sc;
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
            SDL_Log("script: key %s", l->text);
            return;
        } else if (strcmp(l->op, "mark") == 0) {
            SDL_Log("mark: %s", l->text);
        } else if (strcmp(l->op, "profon") == 0) {
            SDL_Log("script: profiler on");
            msdl_prof_on();
        } else if (strcmp(l->op, "profoff") == 0) {
            msdl_prof_off();
            SDL_Log("script: profiler off");
        } else if (strcmp(l->op, "quit") == 0) {
            e.type = SDL_QUIT;
            push(&e);
        }
    }
}

void SDL_PumpEvents(void)
{
    struct IntuiMessage *m;
    SDL_Event e;

    msdl_vram_unlock();     /* Intuition and other tasks may draw on the card */

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
                if (raw >= 0x60 && raw <= 0x67) break;  /* qualifier keys alone */
                if (raw == 0x5F) {      /* Help: the FPS counter on/off */
                    if (down) msdl_fps_on = !msdl_fps_on;
                    break;
                }
                sym = special_key(raw, &sc);
                if (sym == SDLK_UNKNOWN) {
                    int c = map_raw(raw, 0, iaddr);   /* the key without qualifiers */
                    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                    if (c == 0) break;
                    sym = c;
                    if (c >= 'a' && c <= 'z') sc = (SDL_Scancode)(SDL_SCANCODE_A + (c - 'a'));
                    else if (c >= '1' && c <= '9') sc = (SDL_Scancode)(30 + (c - '1'));
                    else if (c == '0') sc = (SDL_Scancode)39;
                }
                if (sc != SDL_SCANCODE_UNKNOWN) keystate[sc] = down ? 1 : 0;
                e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
                e.key.repeat = (qual & IEQUALIFIER_REPEAT) ? 1 : 0;
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

void SDL_FlushEvents(Uint32 minType, Uint32 maxType)
{
    SDL_Event keep[QSIZE];
    int n = 0;
    SDL_PumpEvents();
    while (q_head != q_tail) {
        SDL_Event *e = &queue[q_head];
        if (e->type < minType || e->type > maxType) keep[n++] = *e;
        q_head = (q_head + 1) % QSIZE;
    }
    q_head = q_tail = 0;
    while (n-- > 0) push(&keep[n]);
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

/* ---- SDL_Delay ---- */
static struct MsgPort *delay_port;
static struct timerequest *delay_req;
static int delay_open;

void msdl_sleep_ms(Uint32 ms)
{
    if (ms == 0) return;
    msdl_vram_unlock();     /* no bitmap stays locked while waiting */
    MSDL_Probe(4, 1);
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
    MSDL_Probe(4, 0);
}
