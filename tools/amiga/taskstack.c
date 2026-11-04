/* Stack size of a running task (tc_SPUpper - tc_SPLower): checks that a
   program started from its icon got the stack it asks for (libnix
   __stack + __stkinit swap). Usage: taskstack <task name> */
#include <exec/tasks.h>
#include <proto/exec.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    struct Task *t;
    ULONG size = 0;
    if (argc < 2) return 5;
    Forbid();
    t = FindTask((CONST_STRPTR)argv[1]);
    if (t != NULL) size = (ULONG)t->tc_SPUpper - (ULONG)t->tc_SPLower;
    Permit();
    if (t == NULL) {
        printf("%s: no such task\n", argv[1]);
        return 10;
    }
    printf("%s: stack %lu bytes\n", argv[1], (unsigned long)size);
    return 0;
}
