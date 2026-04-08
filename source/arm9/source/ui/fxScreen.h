#ifndef FX_SCREEN_H
#define FX_SCREEN_H

#include <nds.h>

/* Draw the FX screen (called on tab switch). */
void drawFxScreen(void);

/* Per-frame update (called from main loop when FX tab is active). */
void fxScreen_tick(void);

/* Handle touch input on the FX screen.
 * held: true if this is a continued hold (drag), false if first press. */
void handleFxTouch(touchPosition *touchPos, bool held);

/* Handle touch release — applies pending delay change. */
void handleFxRelease(void);

#endif /* FX_SCREEN_H */
