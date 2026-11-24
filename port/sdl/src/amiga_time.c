/* Millisecond clock from timer.device's E-clock, kept in a C file of its own:
   the AmigaOS headers clash with Freeserf's class names (Message). */
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/expansion.h>
#include <devices/timer.h>
#include <libraries/configvars.h>

#include "amiga_time.h"

struct Device *TimerBase;
static struct timerequest tr;
static ULONG per_ms;
extern unsigned long msdl_counter[10];  /* [8] ms clock reads, [9] E-clock reads (stats) */

unsigned long amiga_now_ms(void)
{
    struct EClockVal ev;
    ULONG hi, lo, r, q;
    int b;

    msdl_counter[8]++;
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

/* The Warp's E-clock counter in its FPGA (firmware 3075 and later, all Warp
   cards): the same 709 379 Hz count as timer.device, low 32 bits, ~90 ns a
   read instead of 0.5-13 us for ReadEClock(). Only after the SysCfg id says
   the register is there (older FPGAs do not decode the address). */
static volatile ULONG *fpga_eclk;
static int fpga_checked;

static void check_fpga_eclock(void)
{
    struct Library *ExpansionBase;
    struct ConfigDev *cd;
    fpga_checked = 1;
    ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 37);
    if (ExpansionBase == NULL) return;
    cd = FindConfigDev(NULL, 0x1400, 101);
    if (cd != NULL && cd->cd_BoardAddr != NULL) {
        volatile ULONG *syscfg = (volatile ULONG *)((UBYTE *)cd->cd_BoardAddr + 0x5000);
        if (syscfg[0] >= 0x0123456AUL) fpga_eclk = syscfg + 0x24 / 4;
    }
    CloseLibrary(ExpansionBase);
}

/* For the log: 1 = Warp FPGA counter, 0 = timer.device. */
int amiga_eclock_fpga(void)
{
    if (!fpga_checked) check_fpga_eclock();
    return fpga_eclk != NULL;
}

/* Low 32 bits of the E-clock, for timing short pieces of code (measurements
   only); the difference of two readings is right as long as they are less
   than about 100 minutes apart. */
unsigned long amiga_eclock(void)
{
    struct EClockVal ev;
    if (!fpga_checked) check_fpga_eclock();
    if (TimerBase == NULL) amiga_now_ms();
    msdl_counter[9]++;
    if (fpga_eclk != NULL) return *fpga_eclk;
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
