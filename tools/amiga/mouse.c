/* Mouse for programs that cannot be scripted (the old Aminet DevilutionX):
   events through input.device, so they reach the active window like a
   real mouse.
     mouse move X Y        pointer to X, Y of the front screen
     mouse down / up       left button
     mouse click X Y       move, down, 80 ms, up
     mouse hold X Y MS     move, down, wait MS, up (walking: button held) */
#include <exec/memory.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <stdlib.h>
#include <string.h>

extern struct IntuitionBase *IntuitionBase;

static struct MsgPort *port;
static struct IOStdReq *io;

static void send(struct InputEvent *ev)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Data = ev;
    io->io_Length = sizeof(*ev);
    DoIO((struct IORequest *)io);
}

static void move(int x, int y)
{
    struct InputEvent ev;
    struct IEPointerPixel pp;
    memset(&ev, 0, sizeof(ev));
    pp.iepp_Screen = IntuitionBase->FirstScreen;
    pp.iepp_Position.X = (WORD)x;
    pp.iepp_Position.Y = (WORD)y;
    ev.ie_Class = IECLASS_NEWPOINTERPOS;
    ev.ie_SubClass = IESUBCLASS_PIXEL;
    ev.ie_Code = IECODE_NOBUTTON;
    ev.ie_EventAddress = &pp;
    send(&ev);
}

static void button(int down)
{
    struct InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.ie_Class = IECLASS_RAWMOUSE;
    ev.ie_Code = down ? IECODE_LBUTTON : (IECODE_LBUTTON | IECODE_UP_PREFIX);
    ev.ie_Qualifier = down ? IEQUALIFIER_LEFTBUTTON : 0;
    send(&ev);
}

int main(int argc, char **argv)
{
    int rc = 0;
    if (argc < 2) return 5;
    port = CreateMsgPort();
    io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(*io)) : NULL;
    if (io == NULL || OpenDevice((CONST_STRPTR) "input.device", 0, (struct IORequest *)io, 0) != 0) return 20;
    if (!strcmp(argv[1], "move") && argc >= 4) {
        move(atoi(argv[2]), atoi(argv[3]));
    } else if (!strcmp(argv[1], "down")) {
        button(1);
    } else if (!strcmp(argv[1], "up")) {
        button(0);
    } else if (!strcmp(argv[1], "click") && argc >= 4) {
        move(atoi(argv[2]), atoi(argv[3]));
        Delay(2);
        button(1);
        Delay(4);
        button(0);
    } else if (!strcmp(argv[1], "hold") && argc >= 5) {
        move(atoi(argv[2]), atoi(argv[3]));
        Delay(2);
        button(1);
        Delay(atoi(argv[4]) / 20);
        button(0);
    } else {
        rc = 5;
    }
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return rc;
}
