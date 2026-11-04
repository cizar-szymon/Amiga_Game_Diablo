/* Workbench start of the Settlers port (amiga_wb.c). */
#ifndef AMIGA_WB_H
#define AMIGA_WB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Started from the Workbench (icon)? */
int amiga_wb_start(void);
/* The icon's Tool Types (without "(...)" hints); returns their number. */
int amiga_tooltypes(char ***list);
/* A message for the user: a requester with a Workbench start, else a
   line on stdout. */
void amiga_message(const char *text);
/* Tool Types (Workbench) or the same words in the Shell's arguments made
   into the game's options (amiga_wb.c); SHOWFPS and NOSOUND are kept here. */
void amiga_args(int *argc, char ***argv);
extern int amiga_opt_showfps;   /* -1 as diablo.ini, 0 off, 1 on */
extern int amiga_opt_nosound;

#ifdef __cplusplus
}
#endif

#endif /* AMIGA_WB_H */
