#ifndef VIS_SCREEN_H
#define VIS_SCREEN_H

#include <nds.h>

/* Draw the visualizer selection screen (called on tab switch). */
void drawVisScreen(void);

/* Per-frame update. */
void visScreen_tick(void);

/* Handle touch input — select an effect. */
void handleVisTouch(touchPosition *touchPos);

#endif /* VIS_SCREEN_H */
