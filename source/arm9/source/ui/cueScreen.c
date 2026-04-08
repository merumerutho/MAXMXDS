/*
 * cueScreen.c
 *
 * Bottom screen CUE tab: 8 cue-point buttons + tap tempo zone.
 * Rendered via BG2 bitmap (8bpp) with text overlay from BG0 console.
 *
 * Each cue button is split vertically:
 *   Top half  = JUMP to this cue point (green)
 *   Bottom half = SET cue to current song position (blue)
 */
#include <stdio.h>
#include "cueScreen.h"
#include "arm9_defines.h"
#include "arm9_fifo.h"
#include "screens.h"

/* ------------------------------------------------------------------ */
/* Layout (pixel coordinates)                                           */
/* ------------------------------------------------------------------ */

#define CUE_GRID_Y      10
#define CUE_COLS         4
#define CUE_ROWS         2
#define CUE_BTN_W       60
#define CUE_BTN_H       46      /* taller to fit split zones */
#define CUE_SPLIT_H     (CUE_BTN_H / 2)  /* 23px per zone */
#define CUE_PAD_X        4
#define CUE_PAD_Y        4
#define CUE_TOTAL_W     (CUE_COLS * CUE_BTN_W + (CUE_COLS - 1) * CUE_PAD_X)
#define CUE_MARGIN_X    ((256 - CUE_TOTAL_W) / 2)

#define CUE_BTN_X(c)    (CUE_MARGIN_X + (c) * (CUE_BTN_W + CUE_PAD_X))
#define CUE_BTN_Y(r)    (CUE_GRID_Y + (r) * (CUE_BTN_H + CUE_PAD_Y))

#define TAP_ZONE_Y       (CUE_BTN_Y(CUE_ROWS) + 6)
#define TAP_ZONE_H       (192 - TAP_ZONE_Y - 2)

/* ------------------------------------------------------------------ */
/* Tap tempo state                                                       */
/* ------------------------------------------------------------------ */

#define TAP_BUFFER_SIZE     4
#define TAP_TIMEOUT_FRAMES  120

static u32 s_tapIntervals[TAP_BUFFER_SIZE];
static u8  s_tapCount       = 0;
static u32 s_frameCounter   = 0;
static u32 s_lastTapFrame   = 0;

/* ------------------------------------------------------------------ */
/* Drawing                                                               */
/* ------------------------------------------------------------------ */

static void drawCueButton(u8 idx)
{
    u8 col = idx % CUE_COLS;
    u8 row = idx / CUE_COLS;
    u16 x = CUE_BTN_X(col);
    u8  y = CUE_BTN_Y(row);

    /* Top half: JUMP zone (dim green) */
    bmpRectFill(x, y, CUE_BTN_W, CUE_SPLIT_H, PAL_BORDER);

    /* Bottom half: SET zone (dark blue) */
    bmpRectFill(x, y + CUE_SPLIT_H, CUE_BTN_W, CUE_BTN_H - CUE_SPLIT_H, PAL_CUE_DIM);

    /* Outer border */
    bmpHLine(y, x, x + CUE_BTN_W - 1, PAL_WAVE_NORM);
    bmpHLine(y + CUE_BTN_H - 1, x, x + CUE_BTN_W - 1, PAL_UNUSED);
    bmpVLine(x, y, y + CUE_BTN_H - 1, PAL_WAVE_NORM);
    bmpVLine(x + CUE_BTN_W - 1, y, y + CUE_BTN_H - 1, PAL_UNUSED);

    /* Split line between JUMP and SET */
    bmpHLine(y + CUE_SPLIT_H, x + 1, x + CUE_BTN_W - 2, PAL_WAVE_NORM);

    /* Text labels — shifted down 1 row and right 1 column */
    u8 tc = (x + 4) / 8 + 1;
    u8 jump_row = (y + 4) / 8 + 1;
    u8 set_row  = (y + CUE_SPLIT_H + 4) / 8 + 1;

    consoleSelect(&bottom);
    iprintf("\x1b[%d;%dH>CUE%d ", jump_row, tc, idx + 1);
    iprintf("\x1b[%d;%dHSET%3d", set_row, tc, arm9_cuePoints[idx] + 1);
}

static void drawTapZone(void)
{
    bmpRectFill(8, TAP_ZONE_Y, 240, TAP_ZONE_H, PAL_CUE_DIM);
    bmpHLine(TAP_ZONE_Y, 8, 247, PAL_BORDER);
    bmpHLine(TAP_ZONE_Y + TAP_ZONE_H - 1, 8, 247, PAL_BORDER);
    bmpVLine(8, TAP_ZONE_Y, TAP_ZONE_Y + TAP_ZONE_H - 1, PAL_BORDER);
    bmpVLine(247, TAP_ZONE_Y, TAP_ZONE_Y + TAP_ZONE_H - 1, PAL_BORDER);

    consoleSelect(&bottom);
    u8 r = TAP_ZONE_Y / 8 + 1;
    iprintf("\x1b[%d;9H TAP TEMPO ", r);
    iprintf("\x1b[%d;9H BPM: %3d  ", r + 2, arm9_globalBpm);
}

/* ------------------------------------------------------------------ */
/* Public interface                                                     */
/* ------------------------------------------------------------------ */

void cueScreen_tick(void)
{
    s_frameCounter++;
    if (s_tapCount > 0 &&
        (s_frameCounter - s_lastTapFrame) > TAP_TIMEOUT_FRAMES)
        s_tapCount = 0;
}

void drawCueScreen(void)
{
    bmpEnsureInit();
    bmpClearAll();

    consoleSelect(&bottom);
    consoleClear();
    drawTabStrip(SCREEN_MODE_CUE);

    for (u8 i = 0; i < N_CUES; i++)
        drawCueButton(i);

    drawTapZone();
}

/* ------------------------------------------------------------------ */

static void refreshTapBpm(void)
{
    consoleSelect(&bottom);
    u8 r = TAP_ZONE_Y / 8 + 3;
    iprintf("\x1b[%d;9H BPM: %3d  ", r, arm9_globalBpm);
}

static void registerTap(void)
{
    if (s_tapCount > 0) {
        u32 interval = s_frameCounter - s_lastTapFrame;
        s_tapIntervals[(s_tapCount - 1) % TAP_BUFFER_SIZE] = interval;
    }
    s_lastTapFrame = s_frameCounter;
    if (s_tapCount <= TAP_BUFFER_SIZE)
        s_tapCount++;

    u8 n = (s_tapCount - 1 < TAP_BUFFER_SIZE) ? s_tapCount - 1 : TAP_BUFFER_SIZE;
    if (n < 1) return;

    /* Geometrically weighted average: most recent tap counts most.
     * Weight[i] = 2^i, so latest interval has weight 2^(n-1).
     * Intervals are stored oldest-first in the ring buffer. */
    u32 weighted_sum = 0;
    u32 weight_total = 0;
    for (u8 i = 0; i < n; i++) {
        /* i=0 is oldest, i=n-1 is newest */
        u32 w = 1u << i;  /* 1, 2, 4, 8 */
        u8 ring_idx = (s_tapCount - 1 - (n - 1 - i)) % TAP_BUFFER_SIZE;
        weighted_sum += s_tapIntervals[ring_idx] * w;
        weight_total += w;
    }
    u32 avg = weighted_sum / weight_total;
    if (avg == 0) return;

    u32 bpm = 3600u / avg;
    if (bpm < 20)  bpm = 20;
    if (bpm > 255) bpm = 255;

    arm9_globalBpm = (u8)bpm;
    serviceUpdate(0);
    refreshTapBpm();
}

/* ------------------------------------------------------------------ */

void handleCueTouch(touchPosition *touchPos, bool b_held, u8 current_song_pos)
{
    float x = (touchPos->rawx - X_MIN) * X_NORM;
    float y = (touchPos->rawy - Y_MIN) * Y_NORM;
    u8 px = (u8)(x * 256);
    u8 py = (u8)(y * 192);

    if (py < CUE_GRID_Y) return;

    /* Cue button zone */
    u8 cue_bot = CUE_BTN_Y(CUE_ROWS);
    if (py < cue_bot) {
        for (u8 i = 0; i < N_CUES; i++) {
            u8 col = i % CUE_COLS;
            u8 row = i / CUE_COLS;
            u16 bx = CUE_BTN_X(col);
            u8  by = CUE_BTN_Y(row);
            if (px >= bx && px < bx + CUE_BTN_W &&
                py >= by && py < by + CUE_BTN_H) {

                u8 split_y = by + CUE_SPLIT_H;
                if (py < split_y) {
                    /* Top half: JUMP to cue */
                    serviceCmd(CMD_GOTO_HOTCUE, arm9_cuePoints[i]);
                } else {
                    /* Bottom half: SET cue to current position */
                    arm9_cuePoints[i] = current_song_pos;
                    drawCueButton(i);
                }
                return;
            }
        }
        return;
    }

    /* Tap tempo zone */
    if (py >= TAP_ZONE_Y)
        registerTap();
}
