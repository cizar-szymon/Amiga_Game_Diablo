#pragma once
// F10 bug report of the Warp port (PORTING_PLAN "Zapis stanu do zgłoszeń
// błędów"): the drawer <save dir>/bugNNN/ and, with TCP/IP running, a window
// in a frame in the middle of the screen that takes the e-mail and the words
// and sends the report to the game server. The game is paused meanwhile.

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include "engine/surface.hpp"

namespace devilution {

/** F10 (the key mapper's "BugReport"): the drawer bugNNN, then the window. */
void AmigaBugReport();
/** Every game tick (GameLogic): the result of the sending, when it is there. */
void AmigaReportPoll();
/** The report's window is on the screen (it takes the keys, the game is paused). */
bool AmigaReportActive();
/** Keys, text and mouse buttons while the window is there: true = taken. */
bool AmigaReportHandleEvent(const SDL_Event &event);
/** The window, over everything else (DrawMain). */
void AmigaReportDraw(const Surface &out);

} // namespace devilution
