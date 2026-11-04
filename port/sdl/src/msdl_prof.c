/* Sampling profiler (ENV:DIABLO_PROFILE set, or "profon"/"profoff" in the
   test input): a vertical-blank interrupt server looks at the exception
   frame the CPU pushed when the interrupt came in. When it interrupted the
   game task in user mode, its PC (as an offset into the code hunk) is
   counted. At exit / profoff the offsets go to RAM:diablo-prof.txt;
   tools/prof.sh maps them to functions with nm of the unstripped binary.
   From Dune Legacy's port (Dune/port/sdl/src/msdl_prof.c).

   Format 0 frame of a level 3 autovector interrupt on the 68020-68060:
   SR (word), PC (long), format/vector word 0x006C. */
#include <exec/types.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>

#include "msdl_internal.h"

extern struct ExecBase *SysBase;
extern char _stext[];            /* start of the code hunk (__stext) */

#define MAX_SAMPLES 200000
#define CODE_MAX    0x01000000UL

static ULONG *samples;
static volatile ULONG nsamples, nother, nmiss, nnoframe;
#define MAX_MISSED 50000
static ULONG *missed;
static struct Task *game_task;
static struct Interrupt prof_int;
static int prof_on, prof_checked;

void msdl_prof_sample(UWORD *sp);
void msdl_prof_server(void);

__asm__(
    "    .globl _msdl_prof_server\n"
    "_msdl_prof_server:\n"
    "    move.l  sp,d0\n"
    "    movem.l d2-d7/a2-a6,-(sp)\n"
    "    move.l  d0,-(sp)\n"
    "    jsr     _msdl_prof_sample\n"
    "    addq.l  #4,sp\n"
    "    movem.l (sp)+,d2-d7/a2-a6\n"
    "    moveq   #0,d0\n"
    "    rts\n");

void msdl_prof_sample(UWORD *sp)
{
    int i;
    if (SysBase->ThisTask != game_task) {
        nother++;
        return;
    }
    for (i = 0; i < 160; i++) {
        if (sp[i + 3] == 0x006C && (sp[i] & 0x2000) == 0) {
            ULONG pc = ((ULONG)sp[i + 1] << 16) | sp[i + 2];
            ULONG off = pc - (ULONG)_stext;
            if (off < CODE_MAX && nsamples < MAX_SAMPLES) samples[nsamples++] = off;
            else {
                if (nmiss < MAX_MISSED && missed != NULL) missed[nmiss] = pc;
                nmiss++;
            }
            return;
        }
    }
    /* no user-mode frame: the task was in supervisor mode (an OS call) */
    nnoframe++;
}

static void prof_stop(void)
{
    FILE *f;
    ULONG i;
    if (!prof_on) return;
    RemIntServer(INTB_VERTB, &prof_int);
    prof_on = 0;
    msdl_logf(SDL_LOG_PRIORITY_INFO, "profiler: %lu samples, %lu other tasks, %lu missed", (unsigned long)nsamples,
              (unsigned long)nother, (unsigned long)nmiss);
    f = fopen("RAM:diablo-prof.txt", "w");
    if (f != NULL) {
        fprintf(f, "# samples %lu, other tasks %lu, missed %lu\n", (unsigned long)nsamples, (unsigned long)nother,
                (unsigned long)nmiss);
        for (i = 0; i < nsamples; i++) fprintf(f, "%lx\n", (unsigned long)samples[i]);
        fprintf(f, "# outside the game code (absolute PCs), %lu without a user-mode frame\n", (unsigned long)nnoframe);
        for (i = 0; i < nmiss && i < MAX_MISSED && missed != NULL; i++) fprintf(f, "m %lx\n", (unsigned long)missed[i]);
        fclose(f);
    }
}

static void prof_begin(void)
{
    static int registered;
    if (samples == NULL) samples = (ULONG *)malloc(MAX_SAMPLES * sizeof(ULONG));
    if (missed == NULL) missed = (ULONG *)malloc(MAX_MISSED * sizeof(ULONG));
    if (samples == NULL) return;
    nsamples = nother = nmiss = nnoframe = 0;
    game_task = FindTask(NULL);
    prof_int.is_Node.ln_Type = NT_INTERRUPT;
    prof_int.is_Node.ln_Pri = 127;
    prof_int.is_Node.ln_Name = (char *)"DevilutionX profiler";
    prof_int.is_Data = NULL;
    prof_int.is_Code = (void (*)(void))msdl_prof_server;
    AddIntServer(INTB_VERTB, &prof_int);
    prof_on = 1;
    msdl_logf(SDL_LOG_PRIORITY_INFO, "profiler: on");
    if (!registered) {
        registered = 1;
        atexit(prof_stop);
    }
}

void msdl_prof_on(void) { if (!prof_on) prof_begin(); }
void msdl_prof_off(void) { prof_stop(); }

/* Called once from the first event pump. */
void msdl_prof_check_env(void)
{
    char buf[16];
    if (prof_checked) return;
    prof_checked = 1;
    if (msdl_env("DIABLO_PROFILE", buf, sizeof(buf)) >= 0) prof_begin();
}
