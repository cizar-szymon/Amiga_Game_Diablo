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

#ifdef __cplusplus
}
#endif

#endif /* AMIGA_WB_H */
