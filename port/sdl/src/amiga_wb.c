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
