/* Millisecond clock from timer.device's E-clock, kept in a C file of its own:
   the AmigaOS headers clash with Freeserf's class names (Message). */
#include <proto/exec.h>
#include <proto/timer.h>
#include <devices/timer.h>

#include "amiga_time.h"

struct Device *TimerBase;
static struct timerequest tr;
static ULONG per_ms;

unsigned long amiga_now_ms(void)
{
    struct EClockVal ev;
    ULONG hi, lo, r, q;
    int b;

    if (TimerBase == NULL) {
        if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                       (struct IORequest *)&tr, 0) != 0)
            return 0;
        TimerBase = tr.tr_node.io_Device;
    }
    per_ms = ReadEClock(&ev) / 1000;
    /* low 32 bits of (hi:lo) / per_ms, by long division without 64-bit ops */
    hi = ev.ev_hi;
    lo = ev.ev_lo;
    r = hi % per_ms;
    q = 0;
    for (b = 31; b >= 0; b--) {
        r = (r << 1) | ((lo >> b) & 1);
        q <<= 1;
        if (r >= per_ms) {
            r -= per_ms;
            q |= 1;
        }
    }
    return q;
}

void amiga_time_close(void)
{
    if (TimerBase != NULL) {
        CloseDevice((struct IORequest *)&tr);
        TimerBase = NULL;
    }
}

/* Low 32 bits of the E-clock, for timing short pieces of code; the
   difference of two readings is right as long as they are less than
   about 100 minutes apart. */
unsigned long amiga_eclock(void)
{
    struct EClockVal ev;
    if (TimerBase == NULL) amiga_now_ms();
    if (TimerBase == NULL) return 0;
    ReadEClock(&ev);
    return ev.ev_lo;
}

/* E-clock ticks per millisecond. */
unsigned long amiga_eclock_per_ms(void)
{
    if (TimerBase == NULL) amiga_now_ms();
    return per_ms != 0 ? per_ms : 1;
}
