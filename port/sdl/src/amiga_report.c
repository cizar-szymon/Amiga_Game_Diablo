/* A bug report to the game server (Warp port of DevilutionX; copied from
 * OpenDUNE port/src/os/report_amiga.c, after
 * the Settlers port's amiga_report.c). Used by F10 (diablo.cpp AmigaBugReport).
 *
 * The e-mail address and words of the player, the computer and the files of
 * the report (F10: bugNNN.*; a LAN game out of step: the states of the
 * players), sent by a process of its own ("DevilutionX report"). The game never
 * waits: it starts the sending and reads the result when it is there.
 *
 * On the wire (the game server, Settlers/server; the frames of net_amiga.c):
 * RELAY "REPORT 1 <version> <kind>", "FIELD <key> <value>" (email, text,
 * machine), for each file "FILE <name> <size>" and its bytes in RELAY_DATA
 * frames (6), "DONE"; the server answers "REPORTED <number>" or "ERR <why>".
 *
 * Its own bsdsocket.library base (a library base belongs to the task that
 * opened it).
 */

#define SocketBase ReportSocketBase

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/filio.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
#define NET_PROTOCOL 1
#include "amiga_report.h"

extern unsigned long amiga_now_ms(void);
#define AmigaTimer_GetMs() ((ULONG)amiga_now_ms())

struct Library *ReportSocketBase = NULL;

enum { F_RELAY = 5, F_RELAY_DATA = 6 };
/* each step (connecting, a piece sent, the answer) within this */
#define STEP_MS 20000

static volatile int s_state = 0;	/* 0 idle, 1 sending, 2 done */
static int s_ok;
static char s_result[64];
static struct Task *volatile s_task = NULL;
static struct Task *s_parent = NULL;
static BYTE s_signal = -1;
static volatile int s_quit = 0;

/* what to send (copied when it starts) */
static char s_host[64], s_version[24], s_kind[12], s_email[96], s_text[1100], s_machine[128];
static int s_port, s_files;
static char s_file[REPORT_MAX_FILES][108];

static long s_fd = -1;

static void Report_WaitFd(int write, ULONG end)
{
	fd_set set;
	struct timeval tv;
	ULONG sigs = SIGBREAKF_CTRL_C;
	LONG left = (LONG)(end - AmigaTimer_GetMs());
	if (left <= 0) return;
	if (left > 200) left = 200;
	FD_ZERO(&set);
	FD_SET(s_fd, &set);
	tv.tv_secs = 0;
	tv.tv_micro = (ULONG)left * 1000;
	WaitSelect(s_fd + 1, write ? NULL : &set, write ? &set : NULL, NULL, &tv, &sigs);
	if (sigs & SIGBREAKF_CTRL_C) s_quit = 1;
}

/* all of buf out; 0 on a timeout or an error */
static int Report_SendAll(const UBYTE *buf, int len)
{
	ULONG end = AmigaTimer_GetMs() + STEP_MS;
	int sent = 0;
	while (sent < len) {
		long n = send(s_fd, (UBYTE *)buf + sent, len - sent, 0);
		if (n > 0) {
			sent += (int)n;
			end = AmigaTimer_GetMs() + STEP_MS;
			continue;
		}
		if (n < 0 && Errno() != EWOULDBLOCK) return 0;
		if (s_quit || (LONG)(end - AmigaTimer_GetMs()) <= 0) return 0;
		Report_WaitFd(1, end);
	}
	return 1;
}

static int Report_Frame(int type, const void *data, int len)
{
	UBYTE b[2 + 250];
	if (len > 250) len = 250;
	b[0] = (UBYTE)(1 + len);
	b[1] = (UBYTE)type;
	memcpy(b + 2, data, len);
	return Report_SendAll(b, 2 + len);
}

static int Report_Line(const char *line)
{
	return Report_Frame(F_RELAY, line, (int)strlen(line));
}

/* "FIELD key value" in pieces of up to 200 letters (the server joins them) */
static int Report_Field(const char *key, const char *value)
{
	char line[230];
	int len = (int)strlen(value), pos = 0;
	do {
		int n = len - pos > 200 ? 200 : len - pos;
		snprintf(line, sizeof(line), "FIELD %s %.*s", key, n, value + pos);
		if (!Report_Line(line)) return 0;
		pos += n;
	} while (pos < len);
	return 1;
}

static const char *Report_BaseName(const char *path)
{
	const char *b = path, *p;
	for (p = path; *p; p++) {
		if (*p == ':' || *p == '/') b = p + 1;
	}
	return b;
}

/* The file in RELAY_DATA frames of 240 bytes, 32 frames to a send: one send
 * a frame made the sending wait for each acknowledgement (Settlers 0.131). */
#define FILE_FRAMES 32
static UBYTE s_fileOut[FILE_FRAMES * 242];

static int Report_File(const char *path)
{
	BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
	LONG size, n;
	char line[160];
	UBYTE buf[240];
	int out = 0;
	if (fh == 0) return 1;	/* not there: nothing to send */
	Seek(fh, 0, OFFSET_END);
	size = Seek(fh, 0, OFFSET_BEGINNING);
	if (size < 0) size = 0;
	snprintf(line, sizeof(line), "FILE %s %ld", Report_BaseName(path), (long)size);
	if (!Report_Line(line)) { Close(fh); return 0; }
	while (size > 0 && (n = Read(fh, buf, sizeof(buf))) > 0) {
		if (n > size) n = size;
		s_fileOut[out] = (UBYTE)(1 + n);
		s_fileOut[out + 1] = F_RELAY_DATA;
		memcpy(s_fileOut + out + 2, buf, n);
		out += 2 + (int)n;
		size -= n;
		if (out > (int)sizeof(s_fileOut) - 242) {
			if (!Report_SendAll(s_fileOut, out)) { Close(fh); return 0; }
			out = 0;
		}
	}
	if (out > 0 && !Report_SendAll(s_fileOut, out)) { Close(fh); return 0; }
	Close(fh);
	/* a file shorter than it said: the server waits for the rest - as zeros */
	memset(buf, 0, sizeof(buf));
	while (size > 0) {
		n = size > 240 ? 240 : size;
		if (!Report_Frame(F_RELAY_DATA, buf, (int)n)) return 0;
		size -= n;
	}
	return 1;
}

static void Report_SetResult(int ok, const char *text)
{
	s_ok = ok;
	strncpy(s_result, text, sizeof(s_result) - 1);
	s_result[sizeof(s_result) - 1] = 0;
}

static void Report_Do(void)
{
	struct sockaddr_in to;
	struct hostent *he;
	long one = 1, r, err = 0, sndbuf = 32768;
	socklen_t elen = sizeof(err);
	ULONG end;
	char line[160];
	UBYTE answer[256];
	int len = 0, i;

	Report_SetResult(0, "The game server did not answer.");
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(s_port);
	to.sin_addr.s_addr = inet_addr((STRPTR)s_host);
	if (to.sin_addr.s_addr == INADDR_NONE) {
		he = gethostbyname((STRPTR)s_host);
		if (he == NULL || he->h_addr_list[0] == NULL) {
			Report_SetResult(0, "Game server not found.");
			return;
		}
		memcpy(&to.sin_addr, he->h_addr_list[0], sizeof(to.sin_addr));
	}
	s_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s_fd < 0) return;
	IoctlSocket(s_fd, FIONBIO, (char *)&one);
	setsockopt(s_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(s_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
	end = AmigaTimer_GetMs() + STEP_MS;
	r = connect(s_fd, (struct sockaddr *)&to, sizeof(to));
	if (r < 0 && Errno() != EINPROGRESS) goto done;
	if (r < 0) {
		for (;;) {
			fd_set set;
			struct timeval tv;
			if (s_quit || (LONG)(end - AmigaTimer_GetMs()) <= 0) goto done;
			Report_WaitFd(1, end);
			FD_ZERO(&set);
			FD_SET(s_fd, &set);
			tv.tv_secs = 0;
			tv.tv_micro = 0;
			if (WaitSelect(s_fd + 1, NULL, &set, NULL, &tv, NULL) > 0) break;
		}
		getsockopt(s_fd, SOL_SOCKET, SO_ERROR, &err, &elen);
		if (err != 0) goto done;
	}
	Report_SetResult(0, "Sending stopped.");
	snprintf(line, sizeof(line), "REPORT %d %s %s", NET_PROTOCOL, s_version, s_kind);
	if (!Report_Line(line)) goto done;
	if (!Report_Field("email", s_email) || !Report_Field("text", s_text) || !Report_Field("machine", s_machine)) goto done;
	for (i = 0; i < s_files; i++) {
		if (!Report_File(s_file[i])) goto done;
	}
	if (!Report_Line("DONE")) goto done;
	/* the answer: one RELAY frame */
	end = AmigaTimer_GetMs() + STEP_MS;
	while (len < 2 || len < 1 + answer[0]) {
		long n = recv(s_fd, answer + len, sizeof(answer) - 1 - len, 0);
		if (n > 0) { len += (int)n; continue; }
		if (n == 0 || Errno() != EWOULDBLOCK) break;
		if (s_quit || (LONG)(end - AmigaTimer_GetMs()) <= 0) break;
		Report_WaitFd(0, end);
	}
	if (len >= 2 && len >= 1 + answer[0] && answer[1] == F_RELAY) {
		int n = answer[0] - 1;
		answer[2 + n] = 0;
		if (strncmp((char *)answer + 2, "REPORTED ", 9) == 0) {
			Report_SetResult(1, (char *)answer + 11);	/* the number */
		} else if (strncmp((char *)answer + 2, "ERR ", 4) == 0) {
			Report_SetResult(0, (char *)answer + 6);
		}
	}
done:
	CloseSocket(s_fd);
	s_fd = -1;
}

static void Report_Main(void)
{
	ReportSocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
	if (ReportSocketBase != NULL) {
		Report_Do();
		CloseLibrary(ReportSocketBase);
		ReportSocketBase = NULL;
	} else {
		Report_SetResult(0, "No TCP/IP stack (bsdsocket.library).");
	}
	Forbid();
	s_state = 2;
	s_task = NULL;
	Signal(s_parent, 1UL << s_signal);
}

static void Report_Copy(char *dst, int size, const char *src)
{
	strncpy(dst, src != NULL ? src : "", size - 1);
	dst[size - 1] = 0;
}

int Report_Send(const char *host, int port, const char *version, const char *kind, const char *email,
                const char *text, const char *machine, const char *const *files, int nfiles)
{
	struct Process *p;
	int i;
	if (s_state == 1) return 0;
	Report_Copy(s_host, sizeof(s_host), host);
	Report_Copy(s_version, sizeof(s_version), version);
	Report_Copy(s_kind, sizeof(s_kind), kind);
	Report_Copy(s_email, sizeof(s_email), email);
	Report_Copy(s_text, sizeof(s_text), text);
	Report_Copy(s_machine, sizeof(s_machine), machine);
	s_port = port > 0 ? port : 6810;
	s_files = nfiles > REPORT_MAX_FILES ? REPORT_MAX_FILES : nfiles;
	for (i = 0; i < s_files; i++) Report_Copy(s_file[i], sizeof(s_file[i]), files[i]);
	AmigaTimer_GetMs();	/* timer.device open before the process uses it */
	if (s_signal < 0) {
		s_parent = FindTask(NULL);
		s_signal = AllocSignal(-1);
		if (s_signal < 0) return 0;
		atexit(Report_Quit);	/* the process ends before the program does */
	}
	SetSignal(0, 1UL << s_signal);
	s_quit = 0;
	s_state = 1;
	Forbid();
	/* priority 1, over the game (it mostly waits for the network): at 0 a
	 * busy game left it so little time that 786 KB took 4 minutes (Settlers) */
	p = CreateNewProcTags(NP_Entry, (ULONG)Report_Main,
	                      NP_Name, (ULONG)"DevilutionX report",
	                      NP_Priority, 1,
	                      NP_StackSize, 16384,
	                      TAG_DONE);
	if (p != NULL) s_task = (struct Task *)p;
	Permit();
	if (p == NULL) {
		s_state = 0;
		return 0;
	}
	return 1;
}

int Report_Result(char *text, int size, int *ok)
{
	if (s_state != 2) return 0;
	strncpy(text, s_result, size - 1);
	text[size - 1] = 0;
	*ok = s_ok;
	s_state = 0;
	return 1;
}

int Report_Network(void)
{
	/* the TCP/IP stack running: its bsdsocket.library in memory. Not opened
	 * here - a library base belongs to the task that opened it */
	struct Node *lib;
	Forbid();
	lib = FindName(&SysBase->LibList, (CONST_STRPTR)"bsdsocket.library");
	Permit();
	return lib != NULL;
}

void Report_Quit(void)
{
	if (s_signal < 0) return;
	if (s_task != NULL) {
		s_quit = 1;
		Signal((struct Task *)s_task, SIGBREAKF_CTRL_C);
	}
	while (s_task != NULL) Wait(1UL << s_signal);
	FreeSignal(s_signal);
	s_signal = -1;
}
