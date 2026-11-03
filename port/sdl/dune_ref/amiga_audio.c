/* Paula output for the Settlers port: a process of its own fills a ring of
   8-bit buffers in chip RAM for one left and one right channel of
   audio.device, from a mix function that the game provides (music and
   sound effects, 16-bit stereo). The game and the mixer share their state
   under amiga_audio_lock(). Pattern of the ring and the channel
   allocation: Vastar's main_amiga.c. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <devices/audio.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <graphics/gfxbase.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <clib/alib_protos.h>

#include <string.h>

#include "amiga_audio.h"

#define BUFS 4

static AmigaAudioFill fill_fn;
static void *fill_user;
static int frames;              /* samples per buffer and channel */
static int period;
static int rate;
static short *mix;              /* frames * 2, fast RAM */
static BYTE *chip[BUFS][2];     /* left, right */

static struct SignalSemaphore lock;
static struct Process *proc;
static struct Task *parent;
static BYTE parent_sig = -1;
static volatile int proc_state; /* 0 starting, 1 running, -1 failed, 2 done */

void amiga_audio_lock(void) { ObtainSemaphore(&lock); }
void amiga_audio_unlock(void) { ReleaseSemaphore(&lock); }
int amiga_audio_rate(void) { return rate; }

/* Diagnostics for STATS: times every queued buffer had played out before
   it was refilled (a gap in the sound), buffers rendered, E-clock ticks
   spent rendering, and the period and PAL flag the rate came from. */
static volatile unsigned long underruns, rendered, render_ticks;
static int pal_flag;
extern unsigned long amiga_eclock(void);

void amiga_audio_stats(unsigned long *under, unsigned long *buffers,
                       unsigned long *ticks, int *per, int *pal)
{
  *under = underruns;
  *buffers = rendered;
  *ticks = render_ticks;
  *per = period;
  *pal = pal_flag;
}

/* One buffer of both channels from the mix function. */
static void render(int b)
{
  int i;
  BYTE *l = chip[b][0];
  BYTE *r = chip[b][1];
  unsigned long t0 = amiga_eclock();
  memset(mix, 0, frames * 2 * sizeof(short));
  ObtainSemaphore(&lock);
  fill_fn(mix, frames, fill_user);
  ReleaseSemaphore(&lock);
  for (i = 0; i < frames; i++) {
    int vl = mix[2 * i] >> 8;
    int vr = mix[2 * i + 1] >> 8;
    l[i] = (BYTE)(vl < -128 ? -128 : vl > 127 ? 127 : vl);
    r[i] = (BYTE)(vr < -128 ? -128 : vr > 127 ? 127 : vr);
  }
  render_ticks += amiga_eclock() - t0;
  rendered++;
}

static void audio_process(void)
{
  static UBYTE channels[] = { 3, 5, 10, 12 };
  struct MsgPort *port;
  struct IOAudio *alloc;
  struct IOAudio *io[BUFS][2];
  int busy[BUFS];
  int next = 0;
  ULONG units;
  int b, c, ok = 0;

  memset(io, 0, sizeof(io));
  port = CreateMsgPort();
  alloc = port ? (struct IOAudio *)CreateIORequest(port, sizeof(*alloc))
               : NULL;
  if (alloc != NULL) {
    alloc->ioa_Request.io_Message.mn_Node.ln_Pri = 1;
    alloc->ioa_Request.io_Flags = ADIOF_NOWAIT;
    alloc->ioa_Data = channels;
    alloc->ioa_Length = sizeof(channels);
    if (OpenDevice((CONST_STRPTR)AUDIONAME, 0,
                   (struct IORequest *)alloc, 0) == 0) {
      ok = 1;
    } else {
      DeleteIORequest((struct IORequest *)alloc);
      alloc = NULL;
    }
  }
  if (ok) {
    units = (ULONG)alloc->ioa_Request.io_Unit;
    for (b = 0; b < BUFS && ok; b++) {
      for (c = 0; c < 2; c++) {
        io[b][c] = (struct IOAudio *)AllocMem(sizeof(struct IOAudio),
                                              MEMF_PUBLIC | MEMF_CLEAR);
        if (io[b][c] == NULL) {
          ok = 0;
          break;
        }
        *io[b][c] = *alloc;
        /* left: channels 0 and 3, right: 1 and 2 */
        io[b][c]->ioa_Request.io_Unit =
          (struct Unit *)(units & (c ? 0x06 : 0x09));
        io[b][c]->ioa_Data = (UBYTE *)chip[b][c];
        io[b][c]->ioa_Period = (UWORD)period;
        io[b][c]->ioa_Volume = 64;
        io[b][c]->ioa_Cycles = 1;
      }
    }
  }

  proc_state = ok ? 1 : -1;
  Signal(parent, 1UL << parent_sig);

  if (ok) {
    for (b = 0; b < BUFS; b++) {
      render(b);
      for (c = 0; c < 2; c++) {
        io[b][c]->ioa_Request.io_Command = CMD_WRITE;
        io[b][c]->ioa_Request.io_Flags = ADIOF_PERVOL;
        io[b][c]->ioa_Length = frames;
        BeginIO((struct IORequest *)io[b][c]);
      }
      busy[b] = 2;
    }
    for (;;) {
      ULONG got = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);
      struct Message *m;
      if (got & SIGBREAKF_CTRL_C) break;
      while ((m = GetMsg(port)) != NULL) {
        for (b = 0; b < BUFS; b++) {
          for (c = 0; c < 2; c++) {
            if (m == &io[b][c]->ioa_Request.io_Message) busy[b]--;
          }
        }
      }
      {
        int idle = 1;
        for (b = 0; b < BUFS; b++) {
          if (busy[b] != 0) idle = 0;
        }
        if (idle) underruns++;
      }
      /* Refill the buffers whose two halves both finished, in the order
         of the ring, so that they are queued again in the same order. */
      while (busy[next] == 0) {
        b = next;
        render(b);
        for (c = 0; c < 2; c++) {
          io[b][c]->ioa_Request.io_Command = CMD_WRITE;
          io[b][c]->ioa_Request.io_Flags = ADIOF_PERVOL;
          io[b][c]->ioa_Length = frames;
          BeginIO((struct IORequest *)io[b][c]);
        }
        busy[b] = 2;
        next = (next + 1) % BUFS;
      }
    }
    for (b = 0; b < BUFS; b++) {
      for (c = 0; c < 2; c++) {
        if (busy[b] > 0) {
          AbortIO((struct IORequest *)io[b][c]);
          WaitIO((struct IORequest *)io[b][c]);
        }
      }
    }
  }

  for (b = 0; b < BUFS; b++) {
    for (c = 0; c < 2; c++) {
      if (io[b][c] != NULL) FreeMem(io[b][c], sizeof(struct IOAudio));
    }
  }
  if (alloc != NULL) {
    CloseDevice((struct IORequest *)alloc);
    DeleteIORequest((struct IORequest *)alloc);
  }
  if (port != NULL) DeleteMsgPort(port);

  Forbid();          /* the parent may free our code once signalled */
  proc_state = 2;
  Signal(parent, 1UL << parent_sig);
}

int amiga_audio_open(int samples_per_buffer, AmigaAudioFill fn, void *user)
{
  ULONG clock;
  int b, c;

  fill_fn = fn;
  fill_user = user;
  frames = samples_per_buffer;
  pal_flag = (GfxBase->DisplayFlags & PAL) != 0;
  clock = pal_flag ? 3546895UL : 3579545UL;
  /* 22 kHz; the period rounded up, so the rate stays at most 22050
     (PAL 161 -> 22030 Hz). */
  period = (int)((clock + 22049UL) / 22050UL);
  rate = (int)(clock / period);

  InitSemaphore(&lock);
  mix = (short *)AllocMem(frames * 2 * sizeof(short), MEMF_ANY);
  if (mix == NULL) return -1;
  for (b = 0; b < BUFS; b++) {
    for (c = 0; c < 2; c++) {
      chip[b][c] = (BYTE *)AllocMem(frames, MEMF_CHIP | MEMF_CLEAR);
      if (chip[b][c] == NULL) {
        amiga_audio_close();
        return -1;
      }
    }
  }

  parent = FindTask(NULL);
  parent_sig = AllocSignal(-1);
  if (parent_sig < 0) {
    amiga_audio_close();
    return -1;
  }
  proc_state = 0;
  proc = CreateNewProcTags(NP_Entry, (ULONG)audio_process,
                           NP_Name, (ULONG)"Settlers audio",
                           NP_Priority, 10,
                           NP_StackSize, 16384,
                           TAG_DONE);
  if (proc == NULL) {
    amiga_audio_close();
    return -1;
  }
  while (proc_state == 0) Wait(1UL << parent_sig);
  if (proc_state < 0) {
    while (proc_state != 2) Wait(1UL << parent_sig);
    proc = NULL;
    amiga_audio_close();
    return -1;
  }
  return 0;
}

void amiga_audio_close(void)
{
  int b, c;
  if (proc != NULL) {
    Signal(&proc->pr_Task, SIGBREAKF_CTRL_C);
    while (proc_state != 2) Wait(1UL << parent_sig);
    proc = NULL;
  }
  if (parent_sig >= 0) {
    FreeSignal(parent_sig);
    parent_sig = -1;
  }
  for (b = 0; b < BUFS; b++) {
    for (c = 0; c < 2; c++) {
      if (chip[b][c] != NULL) {
        FreeMem(chip[b][c], frames);
        chip[b][c] = NULL;
      }
    }
  }
  if (mix != NULL) {
    FreeMem(mix, frames * 2 * sizeof(short));
    mix = NULL;
  }
}
