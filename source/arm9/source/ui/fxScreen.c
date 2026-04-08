/*
 * fxScreen.c
 *
 * Bottom screen FX tab — placeholder.
 * FX implementation is deferred; see memory note for research findings.
 */

#include <stdio.h>
#include "fxScreen.h"
#include "screens.h"
#include "arm9_defines.h"

void drawFxScreen(void)
{
    bmpEnsureInit();
    bmpClearAll();

    consoleSelect(&bottom);
    consoleClear();
    drawTabStrip(SCREEN_MODE_FX);

    /* Centered placeholder text */
    iprintf("\x1b[10;7H-- FX (coming soon) --");
    iprintf("\x1b[12;5Hfilter / delay / reverb");
}

void fxScreen_tick(void)
{
    /* nothing to update */
}

void handleFxTouch(touchPosition *touchPos, bool held)
{
    (void)touchPos;
    (void)held;
}

void handleFxRelease(void)
{
    /* nothing */
}
