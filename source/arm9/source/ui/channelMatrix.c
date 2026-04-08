/*
 * channelMatrix.c
 *
 * Bottom screen layout (CH mode):
 *
 *   Row  0 : tab strip  (drawn by drawTabStrip)
 *   Row  1 : ---- separator ----
 *   Row  2 : Ch.1  Ch.2  Ch.3  Ch.4    <- channel label
 *   Row  3 : mute status row
 *   Row  4 : solo status row
 *   Row  5 : ---- separator ----
 *   ...repeats for rows 5-8, 9-12, 13-16...
 *   Row 17 : ---- separator ----  (bottom edge)
 *
 * Touch zones per cell (y within the 4-row content block, excluding sep):
 *   upper half  -> mute toggle
 *   lower half  -> solo toggle
 */
#include <nds.h>
#include <stdio.h>

#include "channelMatrix.h"
#include "screens.h"
#include "arm9_fifo.h"
#include "libXMX.h"
#include "arm9_fifo.h"

/* Row layout constants */
#define CH_BLOCK_H      4       /* rows per channel block (sep + label + mute + solo) */
#define CH_GRID_OFFSET  1       /* first row used by the grid (row 0 = tab strip) */
#define CH_GRID_BLOCKS  4       /* number of channel rows (4 x 8 channels = 32) */
#define CH_COLS         8       /* channels per row */

/* Row indices for a given block i (0..3) */
#define CH_ROW_SEP(i)   (CH_GRID_OFFSET + (i) * CH_BLOCK_H)
#define CH_ROW_LABEL(i) (CH_GRID_OFFSET + (i) * CH_BLOCK_H + 1)
#define CH_ROW_MUTE(i)  (CH_GRID_OFFSET + (i) * CH_BLOCK_H + 2)
#define CH_ROW_SOLO(i)  (CH_GRID_OFFSET + (i) * CH_BLOCK_H + 3)
#define CH_ROW_BOTTOM   (CH_GRID_OFFSET + CH_GRID_BLOCKS * CH_BLOCK_H)  /* row 17 */

/* Normalised y boundaries */
#define TAB_FRAC        (1.0f / 24)
#define GRID_TOP_FRAC   TAB_FRAC
#define GRID_BOT_FRAC   ((float)(CH_ROW_BOTTOM) / 24)

/* ------------------------------------------------------------------ */

void drawChannelCell(u8 idx)
{
    consoleSelect(&bottom);
    u8 block = idx / CH_COLS;
    u8 col   = (idx % CH_COLS) * 4 + 1;

    if (!arm9_moduleLoaded) {
        iprintf("\x1b[%d;%dH%-3s", CH_ROW_MUTE(block), col, "---");
        iprintf("\x1b[%d;%dH%-3s", CH_ROW_SOLO(block), col, "   ");
        return;
    }

    iprintf("\x1b[%d;%dH%-3s", CH_ROW_MUTE(block), col,
            arm9_channelMute[idx] ? "Mut" : "   ");
    iprintf("\x1b[%d;%dH%-3s", CH_ROW_SOLO(block), col,
            (arm9_soloMask & (1u << idx)) ? "Sol" : "   ");
}

/* ------------------------------------------------------------------ */

void drawChannelMatrix(void)
{
    consoleSelect(&bottom);
    consoleClear();

    drawTabStrip(SCREEN_MODE_CH);

    /* Vertical separators (cols 0, 8, 16, 24), rows 1-17 */
    for (u8 r = CH_GRID_OFFSET; r <= CH_ROW_BOTTOM; r++) {
        iprintf("\x1b[%d;0H|",  r);
        iprintf("\x1b[%d;8H|",  r);
        iprintf("\x1b[%d;16H|", r);
        iprintf("\x1b[%d;24H|", r);
    }

    /* Horizontal separators at top of each block and at the bottom edge */
    for (u8 i = 0; i <= CH_GRID_BLOCKS; i++) {
        u8 row = CH_GRID_OFFSET + i * CH_BLOCK_H;
        for (u8 c = 0; c < 32; c++)
            iprintf("\x1b[%d;%dH-", row, c);
    }

    /* Channel labels */
    for (u8 i = 0; i < CH_GRID_BLOCKS; i++)
        for (u8 j = 0; j < CH_COLS; j++)
            iprintf("\x1b[%d;%dHC%-2d", CH_ROW_LABEL(i), j * 4 + 1, i * CH_COLS + j + 1);

    /* Cell statuses */
    for (u8 i = 0; i < 32; i++)
        drawChannelCell(i);
}

/* ------------------------------------------------------------------ */

static void handleChannelSolo(u8 idx)
{
    if (arm9_soloMask & (1u << idx)) {
        /* Un-solo this channel */
        arm9_soloMask &= ~(1u << idx);
        if (arm9_soloMask == 0) {
            /* Last solo removed: restore pre-solo mutes */
            for (u8 i = 0; i < 32; i++) {
                arm9_channelMute[i] = arm9_preSoloMute[i];
                serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)i << 8) | arm9_channelMute[i]);
            }
        } else {
            arm9_channelMute[idx] = 1;
            serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)idx << 8) | 1);
        }
    } else {
        /* Solo this channel */
        if (arm9_soloMask == 0) {
            for (u8 i = 0; i < 32; i++)
                arm9_preSoloMute[i] = arm9_channelMute[i];
        }
        arm9_soloMask |= (1u << idx);
        for (u8 i = 0; i < 32; i++) {
            u8 mute = (arm9_soloMask & (1u << i)) ? 0 : 1;
            arm9_channelMute[i] = mute;
            serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)i << 8) | mute);
        }
    }

    for (u8 i = 0; i < 32; i++)
        drawChannelCell(i);
}

/* ------------------------------------------------------------------ */

void handleChannelTouch(touchPosition *touchPos)
{
    float x = (touchPos->rawx - X_MIN) * X_NORM;
    float y = (touchPos->rawy - Y_MIN) * Y_NORM;

    /* Tab strip touch is handled by main.c before calling here */
    if (y < GRID_TOP_FRAC || y >= GRID_BOT_FRAC) return;

    float y_grid    = (y - GRID_TOP_FRAC) / (GRID_BOT_FRAC - GRID_TOP_FRAC);
    u8    block_i   = (u8)(y_grid * CH_GRID_BLOCKS);
    if (block_i >= CH_GRID_BLOCKS) block_i = CH_GRID_BLOCKS - 1;
    float y_in_block = y_grid * CH_GRID_BLOCKS - block_i;

    u8 col = (u8)(x * CH_COLS);
    if (col >= CH_COLS) col = CH_COLS - 1;

    u8 idx = col + block_i * CH_COLS;

    if (!arm9_moduleLoaded || idx >= 32) return;

    if (y_in_block < 0.5f) {
        /* Upper half: mute toggle */
        arm9_channelMute[idx] ^= 1;
        serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)idx << 8) | arm9_channelMute[idx]);
        drawChannelCell(idx);
    } else {
        /* Lower half: solo toggle */
        handleChannelSolo(idx);
    }
}
