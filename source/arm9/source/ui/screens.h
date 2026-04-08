/*
 * screens.h — Shared bottom-screen infrastructure: tabs, palette, bitmap helpers.
 */

#ifndef ARM9_SOURCE_SCREENS_H_
#define ARM9_SOURCE_SCREENS_H_

#include <nds.h>
#include "arm9_defines.h"

/* DS touchscreen raw → normalized coordinate range */
#define X_MIN 320
#define X_MAX 3808
#define Y_MIN 224
#define Y_MAX 3904

extern const float X_NORM;
extern const float Y_NORM;

extern PrintConsole top, bottom;
extern int sub_bg2;  /* sub-screen BG2 handle; -1 until initWaveBg() */

void drawTabStrip(ScreenMode mode);
void initWaveBg(void);
void initTopBg(void);
void topScreen_tick(void);
void topScreen_nextEffect(void);

/* Palette indices shared across all bottom-screen modes */
#define PAL_BG        0
#define PAL_BORDER    1
#define PAL_WAVE_NORM 2
#define PAL_WAVE_MUTE 3
#define PAL_WAVE_SOLO 4
#define PAL_CUE_FILL  5
#define PAL_CUE_DIM   6
#define PAL_WHITE     7
#define PAL_FX_FILL   8
#define PAL_FX_DIM    9
#define PAL_SLIDER_ON 10
#define PAL_SLIDER_BG 11
#define PAL_UNUSED    12

/* 8bpp bitmap helpers (sub-screen BG2) */
void bmpEnsureInit(void);
void bmpClearAll(void);
void bmpPixel(u16 x, u8 y, u8 c);
void bmpRectFill(u16 x, u8 y, u16 w, u8 h, u8 c);
void bmpHLine(u8 y, u16 x0, u16 x1, u8 c);
void bmpVLine(u16 x, u8 y0, u8 y1, u8 c);

#endif /* ARM9_SOURCE_SCREENS_H_ */
