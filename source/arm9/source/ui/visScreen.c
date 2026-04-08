/*
 * visScreen.c — VFX tab: effect selector + PX/STRIDE sliders.
 */

#include <stdio.h>
#include "visScreen.h"
#include "screens.h"
#include "arm9_defines.h"
#include "demo.h"

/* --- Effect button grid (char-aligned) --- */

#define VIS_COLS     4
#define VIS_ROWS     2
#define VIS_BTN_CW   7
#define VIS_BTN_CH   5
#define VIS_BTN_W    (VIS_BTN_CW * 8)
#define VIS_BTN_H    (VIS_BTN_CH * 8)
#define VIS_PAD_CX   1
#define VIS_PAD_CY   2
#define VIS_MARGIN_CX  0
#define VIS_Y0_CH    2

static const char *s_fx_names[] = {
    " PLASMA ",
    " META   ",
    " TUNNEL ",
    " SINE   ",
    " FIRE   ",
    " STARS  ",
    " MOIRE  ",
    " HEX    ",
};

static void btnPos(u8 idx, u8 *cx, u8 *cy)
{
    u8 col = idx % VIS_COLS;
    u8 row = idx / VIS_COLS;
    *cx = VIS_MARGIN_CX + col * (VIS_BTN_CW + VIS_PAD_CX);
    *cy = VIS_Y0_CH + row * (VIS_BTN_CH + VIS_PAD_CY);
}

static void drawButton(u8 idx, bool active)
{
    u8 cx, cy;
    btnPos(idx, &cx, &cy);

    u16 px = cx * 8;
    u16 py = cy * 8;

    u8 fill = active ? PAL_CUE_FILL : PAL_BORDER;
    bmpRectFill(px, (u8)py, VIS_BTN_W, VIS_BTN_H, fill);

    /* Beveled border: bright top/left, dark bottom/right */
    u8 hi = active ? PAL_WAVE_NORM : PAL_WAVE_MUTE;
    u8 lo = PAL_UNUSED;
    bmpHLine((u8)py, px, px + VIS_BTN_W - 1, hi);
    bmpVLine(px, (u8)py, (u8)(py + VIS_BTN_H - 1), hi);
    bmpHLine((u8)(py + VIS_BTN_H - 1), px, px + VIS_BTN_W - 1, lo);
    bmpVLine(px + VIS_BTN_W - 1, (u8)py, (u8)(py + VIS_BTN_H - 1), lo);

    u8 text_row = cy + VIS_BTN_CH / 2;
    u8 text_col = cx;

    consoleSelect(&bottom);
    iprintf("\x1b[%d;%dH%s", text_row, text_col, s_fx_names[idx]);
}

/* --- Sliders for PX and STRIDE --- */

/* Slider geometry (pixel coords, below button grid) */
#define SLD_Y0       148      /* pixel Y of first slider */
#define SLD_SPACING  20       /* vertical gap between sliders */
#define SLD_X        48       /* left edge of slider track */
#define SLD_W        160      /* track width in pixels */
#define SLD_H        10       /* track height */
#define SLD_LABEL_X  0        /* label char column */

#define SLD_PX_MIN   1
#define SLD_PX_MAX   8
#define SLD_ST_MIN   0
#define SLD_ST_MAX   8

static void drawSlider(u8 idx, const char *label, u8 val, u8 vmin, u8 vmax)
{
    u16 y = SLD_Y0 + idx * SLD_SPACING;

    /* Track background */
    bmpRectFill(SLD_X, (u8)y, SLD_W, SLD_H, PAL_SLIDER_BG);

    /* Filled portion */
    u16 fill_w = ((u32)(val - vmin) * SLD_W) / (vmax - vmin);
    if (fill_w > 0)
        bmpRectFill(SLD_X, (u8)y, fill_w, SLD_H, PAL_WAVE_NORM);

    /* Text label + value */
    u8 text_row = y / 8;
    consoleSelect(&bottom);
    iprintf("\x1b[%d;%dH%-4s %d ", text_row, SLD_LABEL_X, label, val);
}

static void drawSliders(void)
{
    drawSlider(0, "SIZE", demo_get_px(),     SLD_PX_MIN, SLD_PX_MAX);
    drawSlider(1, "GAP",  demo_get_stride(), SLD_ST_MIN, SLD_ST_MAX);
}

/* Returns true if touch was on a slider, and updates the value */
static bool handleSliderTouch(s32 px, s32 py)
{
    for (u8 i = 0; i < 2; i++) {
        s32 sy = SLD_Y0 + i * SLD_SPACING;
        if (px >= SLD_X && px < SLD_X + SLD_W &&
            py >= sy && py < sy + SLD_H) {
            u32 frac = (u32)(px - SLD_X) * 256 / SLD_W;
            if (i == 0) {
                u8 val = SLD_PX_MIN + (frac * (SLD_PX_MAX - SLD_PX_MIN + 1)) / 256;
                if (val < SLD_PX_MIN) val = SLD_PX_MIN;
                if (val > SLD_PX_MAX) val = SLD_PX_MAX;
                demo_set_px(val);
                drawSlider(0, "SIZE", val, SLD_PX_MIN, SLD_PX_MAX);
            } else {
                u8 val = SLD_ST_MIN + (frac * (SLD_ST_MAX - SLD_ST_MIN + 1)) / 256;
                if (val > SLD_ST_MAX) val = SLD_ST_MAX;
                demo_set_stride(val);
                drawSlider(1, "GAP", val, SLD_ST_MIN, SLD_ST_MAX);
            }
            return true;
        }
    }
    return false;
}

/* --- Public interface --- */

void drawVisScreen(void)
{
    bmpEnsureInit();
    bmpClearAll();

    consoleSelect(&bottom);
    consoleClear();
    drawTabStrip(SCREEN_MODE_VFX);

    u8 cur = demo_current();
    u8 n   = demo_count();
    for (u8 i = 0; i < n; i++)
        drawButton(i, i == cur);

    drawSliders();
}

void visScreen_tick(void)
{
}

void handleVisTouch(touchPosition *touchPos)
{
    float x_norm = (touchPos->rawx - X_MIN) * X_NORM;
    float y_norm = (touchPos->rawy - Y_MIN) * Y_NORM;
    s32 px = (s32)(x_norm * 256);
    s32 py = (s32)(y_norm * 192);

    /* Sliders first (continuous drag) */
    if (handleSliderTouch(px, py))
        return;

    /* Effect buttons */
    u8 n = demo_count();
    for (u8 i = 0; i < n; i++) {
        u8 cx, cy;
        btnPos(i, &cx, &cy);
        s32 bx = cx * 8;
        s32 by = cy * 8;

        if (px >= bx && px < bx + VIS_BTN_W &&
            py >= by && py < by + VIS_BTN_H) {
            u8 prev = demo_current();
            if (i != prev) {
                demo_select(i);
                drawButton(prev, false);
                drawButton(i, true);
            }
            return;
        }
    }
}
