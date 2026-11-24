/* A bug report to the game server (amiga_report.c; F10 in diablo.cpp). */
#ifndef AMIGA_REPORT_H
#define AMIGA_REPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#define REPORT_MAX_FILES 6

/* A process sends the fields and up to REPORT_MAX_FILES files (missing ones
 * are left out). Report_Send: 0 if one is still being sent. Report_Result: 0
 * while it goes on; 1 when done, *ok 1 and text the number of the report, or
 * *ok 0 and text why not. Report_Quit at the end of the program (atexit). */
int Report_Send(const char *host, int port, const char *version, const char *kind, const char *email,
                const char *text, const char *machine, const char *const *files, int nfiles);
int Report_Result(char *text, int size, int *ok);
void Report_Quit(void);
/* 1: a TCP/IP stack is running (its bsdsocket.library in memory) - a report
 * can go; else F10 only saves the files. */
int Report_Network(void);

#ifdef __cplusplus
}
#endif

#endif
