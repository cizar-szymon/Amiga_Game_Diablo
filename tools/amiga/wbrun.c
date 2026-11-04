/* Starts a program as from its icon (double click): OpenWorkbenchObjectA of
   workbench.library 44+ (OS 3.5+). The program gets a Workbench start with
   its icon's Tool Types and stack. Usage: wbrun <path of the program> */
#include <proto/exec.h>
#include <proto/wb.h>
#include <stdio.h>

struct Library *WorkbenchBase;

int main(int argc, char **argv)
{
    int rc = 0;
    if (argc < 2) return 5;
    WorkbenchBase = OpenLibrary((CONST_STRPTR) "workbench.library", 44);
    if (WorkbenchBase == NULL) {
        printf("workbench.library 44 needed\n");
        return 20;
    }
    if (!OpenWorkbenchObjectA((STRPTR)argv[1], NULL)) {
        printf("could not open %s\n", argv[1]);
        rc = 10;
    }
    CloseLibrary(WorkbenchBase);
    return rc;
}
