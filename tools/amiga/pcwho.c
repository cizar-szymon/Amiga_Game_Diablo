/* Which module holds a code address? (profiler samples outside the game)
   Usage: pcwho <hex address> [<hex address>...]
   Looks backwards (up to 512 KB) for a RomTag (0x4AFC whose rt_MatchTag
   points at itself) and prints its name, version and the offset, then
   dumps 32 bytes around the address. Only reads memory. */
#include <exec/resident.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>

static int readable(ULONG a)
{
    /* RAM known to exec, or the ROM */
    if (a >= 0x00F80000UL && a < 0x01000000UL) return 1;
    return TypeOfMem((APTR)a) != 0;
}

int main(int argc, char **argv)
{
    int k;
    for (k = 1; k < argc; k++) {
        ULONG pc = strtoul(argv[k], NULL, 16) & ~1UL, a, i;
        int found = 0;
        if (!readable(pc)) {
            printf("%08lx: not RAM/ROM\n", pc);
            continue;
        }
        for (a = pc; a + 0x80000UL > pc && a >= 2 && readable(a); a -= 2) {
            const struct Resident *r = (const struct Resident *)a;
            if (r->rt_MatchWord == RTC_MATCHWORD && r->rt_MatchTag == r) {
                printf("%08lx: %s +%lx (RomTag at %08lx, \"%s\" v%d)\n", pc, r->rt_Name ? (char *)r->rt_Name : "?",
                       pc - a, a, r->rt_IdString ? (char *)r->rt_IdString : "", r->rt_Version);
                found = 1;
                break;
            }
        }
        if (!found) printf("%08lx: no RomTag within 512 KB below\n", pc);
        printf("  ");
        for (i = pc - 16; i < pc + 16; i += 2) printf("%04x%s", *(UWORD *)i, i + 2 == pc ? " [" : " ");
        printf("\n");
    }
    return 0;
}
