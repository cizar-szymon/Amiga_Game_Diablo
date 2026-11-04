/* Starting from the Workbench: the icon's Tool Types as options, and
   messages in a requester instead of on stdout (with a Workbench start
   libnix would open a console window for any output). */
#include <exec/types.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "amiga_wb.h"

extern struct WBStartup *_WBenchMsg;

struct Library *IconBase;

#define MAX_TOOLTYPES 32
static char tooltype_text[MAX_TOOLTYPES][128];
static char *tooltypes[MAX_TOOLTYPES];

int amiga_wb_start(void)
{
  return _WBenchMsg != NULL;
}

int amiga_tooltypes(char ***list)
{
  struct WBArg *arg;
  struct DiskObject *dobj;
  BPTR old;
  int n = 0;

  *list = tooltypes;
  if (_WBenchMsg == NULL || _WBenchMsg->sm_NumArgs < 1) return 0;
  IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36);
  if (IconBase == NULL) return 0;
  arg = _WBenchMsg->sm_ArgList;
  old = CurrentDir(arg->wa_Lock);
  dobj = GetDiskObject(arg->wa_Name);
  CurrentDir(old);
  if (dobj != NULL) {
    STRPTR *tt = (STRPTR *)dobj->do_ToolTypes;
    for (; tt != NULL && *tt != NULL && n < MAX_TOOLTYPES; tt++) {
      /* "(OPTION)" is a disabled hint, an empty line nothing. */
      if ((*tt)[0] == '(' || (*tt)[0] == 0) continue;
      strncpy(tooltype_text[n], (const char *)*tt,
              sizeof(tooltype_text[n]) - 1);
      tooltype_text[n][sizeof(tooltype_text[n]) - 1] = 0;
      tooltypes[n] = tooltype_text[n];
      n++;
    }
    FreeDiskObject(dobj);
  }
  CloseLibrary(IconBase);
  IconBase = NULL;
  return n;
}

void amiga_message(const char *text)
{
  if (_WBenchMsg != NULL) {
    struct EasyStruct es;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"DevilutionX";
    es.es_TextFormat = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"OK";
    EasyRequest(NULL, &es, NULL, (ULONG)text);
  } else {
    printf("%s\n", text);
  }
}

/* ---- Tool Types (and the same words from the Shell) as the game's options ----
   DATADIR=dir       --data-dir dir (where diabdat.mpq is)
   SAVEDIR=dir       --save-dir dir --config-dir dir (saves, diablo.ini)
   LANGUAGE=pl|en|de --lang (any language the game has)
   SKIPINTRO         -n
   SHOWFPS=YES|NO    the frame counter on or off (over diablo.ini)
   NOSOUND           no sound */
int amiga_opt_showfps = -1;
int amiga_opt_nosound = 0;

#define MAX_ARGS 64
static char *new_argv[MAX_ARGS + 1];
static char dir_text[2][160];

static int starts(const char *s, const char *key)
{
  size_t n = strlen(key);
  return strncasecmp(s, key, n) == 0;
}

static const char *as_dir(int k, const char *v)
{
  size_t n = strlen(v);
  strncpy(dir_text[k], v, sizeof(dir_text[k]) - 2);
  dir_text[k][sizeof(dir_text[k]) - 2] = 0;
  n = strlen(dir_text[k]);
  if (n > 0 && dir_text[k][n - 1] != '/' && dir_text[k][n - 1] != ':') strcat(dir_text[k], "/");
  return dir_text[k];
}

/* Returns 1 when the word was one of ours (added to new_argv as needed). */
static int option_word(const char *t, int *n)
{
  if (starts(t, "DATADIR=") && *n + 2 <= MAX_ARGS) {
    new_argv[(*n)++] = (char *)"--data-dir";
    new_argv[(*n)++] = (char *)as_dir(0, t + 8);
  } else if (starts(t, "SAVEDIR=") && *n + 4 <= MAX_ARGS) {
    const char *d = as_dir(1, t + 8);
    new_argv[(*n)++] = (char *)"--save-dir";
    new_argv[(*n)++] = (char *)d;
    new_argv[(*n)++] = (char *)"--config-dir";
    new_argv[(*n)++] = (char *)d;
  } else if (starts(t, "LANGUAGE=") && *n + 2 <= MAX_ARGS) {
    new_argv[(*n)++] = (char *)"--lang";
    new_argv[(*n)++] = (char *)(t + 9);
  } else if (!strcasecmp(t, "SKIPINTRO") && *n + 1 <= MAX_ARGS) {
    new_argv[(*n)++] = (char *)"-n";
  } else if (starts(t, "SHOWFPS")) {
    const char *v = strchr(t, '=');
    amiga_opt_showfps = (v == NULL || !strcasecmp(v + 1, "YES") || !strcasecmp(v + 1, "ON") || !strcmp(v + 1, "1")) ? 1 : 0;
  } else if (!strcasecmp(t, "NOSOUND")) {
    amiga_opt_nosound = 1;
  } else {
    return 0;
  }
  return 1;
}

void amiga_args(int *argc, char ***argv)
{
  char **tt;
  int n = 0, i, k;
  if (amiga_wb_start()) {
    new_argv[n++] = (char *)"devilutionx";
    k = amiga_tooltypes(&tt);
    for (i = 0; i < k; i++) option_word(tt[i], &n);
  } else {
    for (i = 0; i < *argc && n < MAX_ARGS; i++) {
      if (i > 0 && option_word((*argv)[i], &n)) continue;
      new_argv[n++] = (*argv)[i];
    }
  }
  new_argv[n] = NULL;
  *argc = n;
  *argv = new_argv;
}
