/* Mini-SDL2: glue to the shared Amiga helpers. The program's main() is
   NovaCoder's game/src/amiga_main.c (stack check, PROGDIR, dune_main). */
#include "amiga_wb.h"
#include "msdl_internal.h"

int msdl_wb_start(void) { return amiga_wb_start(); }
