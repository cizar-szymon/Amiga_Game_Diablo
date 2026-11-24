/* A short description of the Amiga for bug reports (F10, diablo.cpp):
   CPU and FPU, exec version, the Warp card (product, FPGA id), free memory. */
#include <stdio.h>
#include <string.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/expansion.h>

extern struct ExecBase *SysBase;

int amiga_machine_info(char *buf, int size)
{
    struct Library *ExpansionBase;
    struct ConfigDev *cd = NULL;
    UWORD attn = SysBase->AttnFlags;
    const char *cpu = (attn & AFF_68060) ? "68060" : (attn & AFF_68040) ? "68040" : (attn & AFF_68030) ? "68030"
                    : (attn & AFF_68020) ? "68020" : "68000";
    const char *fpu = (attn & (AFF_68881 | AFF_68882 | AFF_FPU40)) ? "FPU" : "no FPU";
    char warp[96];
    int n;

    strcpy(warp, "no Warp card found");
    ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 37);
    if (ExpansionBase != NULL) {
        cd = FindConfigDev(NULL, 0x1400, -1);
        while (cd != NULL && cd->cd_Rom.er_Product != 101) cd = FindConfigDev(cd, 0x1400, -1);
        if (cd == NULL) cd = FindConfigDev(NULL, 0x1400, -1);
        if (cd != NULL) {
            ULONG id = 0;
            if (cd->cd_Rom.er_Product == 101 && cd->cd_BoardAddr != NULL)
                id = *(volatile ULONG *)((UBYTE *)cd->cd_BoardAddr + 0x5000);
            sprintf(warp, "Warp board, product %u, FPGA id %08lx", (unsigned)cd->cd_Rom.er_Product, (unsigned long)id);
        }
        CloseLibrary(ExpansionBase);
    }
    n = snprintf(buf, (size_t)size, "CPU %s, %s; exec %u.%u; %s; free Fast %lu KB, Chip %lu KB",
                 cpu, fpu, (unsigned)SysBase->LibNode.lib_Version, (unsigned)SysBase->SoftVer, warp,
                 (unsigned long)(AvailMem(MEMF_FAST) / 1024), (unsigned long)(AvailMem(MEMF_CHIP) / 1024));
    return n;
}
