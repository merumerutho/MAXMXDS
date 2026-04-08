/*
 * waveScreen.c
 *
 * Bottom screen CH mode: 8×4 oscilloscope grid (32 channels, no text labels).
 *
 * BG2 (8bpp bitmap) renders waveforms; BG0 (text console) used only for the
 * tab strip row.  ARM7 appends one live sample per channel to a ring buffer
 * each VBlank; ARM9 DC_InvalidateRange's and draws the scrolling trace.
 *
 * Touch zones per cell:
 *   top    half (y < WAVE_CELL_H/2) -> mute toggle
 *   bottom half                     -> solo toggle
 */
#include <string.h>
#include "waveScreen.h"
#include "screens.h"
#include "arm9_defines.h"
#include "arm9_fifo.h"   /* arm9_channelMute, arm9_soloMask, arm9_wave_cap, serviceCmd */
#include "libXMX.h"
#include <mm_mas.h>      /* mm_mas_ds_sample descriptor struct */

/* ------------------------------------------------------------------ */
/* Grid geometry                                                         */
/* ------------------------------------------------------------------ */

#define WAVE_GRID_TOP   8   /* first pixel row of the grid              */
#define WAVE_COLS       8   /* number of channel columns                  */
#define WAVE_ROWS       4   /* number of channel rows                     */
#define WAVE_CELL_W    32   /* pixels per column (including left border)  */
#define WAVE_CELL_H    46   /* pixels per row    (including top border)   */
#define WAVE_INNER_W   30   /* usable waveform width  (cell - 2 borders)  */
#define WAVE_INNER_H   44   /* usable waveform height (cell - 2 borders)  */

#define WAVE_INNER_X(c)  ((c) * WAVE_CELL_W + 1)
#define WAVE_INNER_Y(r)  (WAVE_GRID_TOP + (r) * WAVE_CELL_H + 1)
#define WAVE_CENTER_Y(r) (WAVE_GRID_TOP + (r) * WAVE_CELL_H + WAVE_CELL_H / 2)

/* Palette indices and bitmap helpers come from screens.h */

/* ------------------------------------------------------------------ */
/* Grid borders                                                          */
/* ------------------------------------------------------------------ */

static void drawWaveGrid(void)
{
    for (u8 r = 0; r <= WAVE_ROWS; r++) {
        u8 y = WAVE_GRID_TOP + r * WAVE_CELL_H;
        if (y > 191) y = 191;
        bmpHLine(y, 0, 255, PAL_BORDER);
    }
    for (u8 c = 0; c <= WAVE_COLS; c++) {
        u8 x = c * WAVE_CELL_W;
        if (x > 255) x = 255;
        bmpVLine(x, WAVE_GRID_TOP, 191, PAL_BORDER);
    }
    for (u8 r = 0; r < WAVE_ROWS; r++) {
        u8 cy = WAVE_CENTER_Y(r);
        for (u8 c = 0; c < WAVE_COLS; c++)
            bmpHLine(cy, WAVE_INNER_X(c), WAVE_INNER_X(c) + WAVE_INNER_W - 1, PAL_BORDER);
    }
}

/* ------------------------------------------------------------------ */
/* Change detection state (mute/solo)                                   */
/* ------------------------------------------------------------------ */

static u8 s_lastMute[32];
static u32 s_lastSolo;

/* ------------------------------------------------------------------ */
/* Per-cell oscilloscope drawing                                        */
/* ------------------------------------------------------------------ */

/*
 * Samples-per-pixel stride for the oscilloscope display.
 * 4 samples/pixel × 30 pixels = 120 samples displayed.
 * At ~21.8 kHz that's ~5.5 ms — about 2-3 cycles of a mid-range note.
 */
#define WAVE_STRIDE  4

/* One-pole IIR blend weight (0-256).
 * Higher = snappier response, lower = smoother/less flashy.
 * 48 ≈ ~19% new data per frame, full settle in ~12 frames (~200 ms). */
#define WAVE_BLEND  96

/* Persistent per-channel blended waveform (s8 values, one per display pixel). */
static s8 s_wave_buf[WAVE_CAP_N][WAVE_INNER_W];

/* Bitmask: bit N set = channel N has been seen active since last song load.
 * Channels never seen active are drawn grayed out and non-interactible. */
static u32 s_ch_seen_active = 0;

/* Read one signed sample from PCM data. */
static inline s8 readSample(const u8 *data, u32 pos, u8 fmt)
{
    if (fmt == 0) /* 8-bit PCM */
        return (s8)data[pos];
    else          /* 16-bit PCM — take high byte */
        return (s8)data[pos * 2 + 1];
}

static void drawWaveCell(u8 ch)
{
    u8 r  = ch / WAVE_COLS;
    u8 c  = ch % WAVE_COLS;
    u8 x0 = WAVE_INNER_X(c);
    u8 y0 = WAVE_INNER_Y(r);
    u8 cy = WAVE_CENTER_Y(r);

    bool ch_used = (s_ch_seen_active >> ch) & 1;

    /* Unused channels: gray fill, no waveform, no interaction */
    if (!ch_used) {
        for (u8 y = y0; y < y0 + WAVE_INNER_H; y++)
            bmpHLine(y, x0, x0 + WAVE_INNER_W - 1, PAL_UNUSED);
        return;
    }

    /* Choose colour based on mute/solo state */
    u8 wave_col = PAL_WAVE_NORM;
    if (arm9_channelMute[ch])
        wave_col = PAL_WAVE_MUTE;
    else if (arm9_soloMask & (1u << ch))
        wave_col = PAL_WAVE_SOLO;

    /* Clear inner area, restore centre line */
    for (u8 y = y0; y < y0 + WAVE_INNER_H; y++)
        bmpHLine(y, x0, x0 + WAVE_INNER_W - 1, 0);
    bmpHLine(cy, x0, x0 + WAVE_INNER_W - 1, PAL_BORDER);

    if (arm9_wave_cap == NULL) return;

    /* Invalidate ARM9 cache so we read fresh mixer state written by ARM7 */
    DC_InvalidateRange((void *)&arm9_wave_cap[ch], sizeof(WaveCapCh));

    u32 desc_off = arm9_wave_cap[ch].samp;

    if (desc_off == 0) {
        /* Channel inactive — decay existing trace toward silence */
        for (u8 x = 0; x < WAVE_INNER_W; x++) {
            s16 blended = ((s16)s_wave_buf[ch][x] * (256 - WAVE_BLEND)) >> 8;
            s_wave_buf[ch][x] = (s8)blended;
        }
    } else {
        /* Resolve sample descriptor in main RAM */
        const mm_mas_ds_sample *desc =
            (const mm_mas_ds_sample *)(0x02000000u + desc_off);

        const u8 *data = (desc->point != 0) ? (const u8 *)desc->point
                                            : (const u8 *)desc->data;
        u8  fmt        = desc->format;
        u32 loop_start = desc->loop_start;
        u32 loop_len   = desc->loop_length;
        u32 loop_end   = loop_start + loop_len;

        /* Current playback position (integer part of 22.10 fixed-point) */
        u32 pos = arm9_wave_cap[ch].read >> 10;

        for (u8 x = 0; x < WAVE_INNER_W; x++) {
            /* Average over WAVE_STRIDE samples per pixel (anti-alias) */
            s16 acc = 0;
            for (u8 s = 0; s < WAVE_STRIDE; s++) {
                u32 p = pos + s;
                if (loop_len > 0 && p >= loop_end)
                    p = loop_start + (p - loop_end);
                acc += readSample(data, p, fmt);
            }
            s8 v = (s8)(acc / WAVE_STRIDE);

            /* IIR blend: smooth transition between frames */
            s16 blended = ((s16)v                * WAVE_BLEND
                        +  (s16)s_wave_buf[ch][x] * (256 - WAVE_BLEND)) >> 8;
            s_wave_buf[ch][x] = (s8)blended;

            /* Advance by stride, wrapping at loop end */
            pos += WAVE_STRIDE;
            if (loop_len > 0 && pos >= loop_end)
                pos = loop_start + (pos - loop_end);
        }
    }

    /* Draw the blended waveform as a connected trace */
    const u8 y_min = y0;
    const u8 y_max = y0 + WAVE_INNER_H - 1;
    const s16 half_h = WAVE_INNER_H / 2 - 1;
    u8 prev_py = cy;

    for (u8 x = 0; x < WAVE_INNER_W; x++) {
        s16 amps = ((s16)s_wave_buf[ch][x] * half_h) >> 7;
        s16 raw  = (s16)cy - amps;
        u8  py   = (raw < (s16)y_min) ? y_min :
                   (raw > (s16)y_max) ? y_max : (u8)raw;

        /* Vertical line from prev_py to py for connected trace */
        u8 ya = (prev_py < py) ? prev_py : py;
        u8 yb = (prev_py < py) ? py      : prev_py;
        if (ya < y_min) ya = y_min;
        if (yb > y_max) yb = y_max;
        bmpVLine(x0 + x, ya, yb, wave_col);

        prev_py = py;
    }
}

/* ------------------------------------------------------------------ */
/* Touch handler                                                         */
/* ------------------------------------------------------------------ */

void handleWaveTouch(touchPosition *touchPos)
{
    float fx = (touchPos->rawx - X_MIN) * X_NORM;
    float fy = (touchPos->rawy - Y_MIN) * Y_NORM;
    u8 px    = (u8)(fx * SCREEN_WIDTH);
    u8 py    = (u8)(fy * SCREEN_HEIGHT);

    if (py < WAVE_GRID_TOP) return;

    u8 col = px / WAVE_CELL_W;
    u8 row = (py - WAVE_GRID_TOP) / WAVE_CELL_H;
    if (col >= WAVE_COLS || row >= WAVE_ROWS) return;

    u8 ch = row * WAVE_COLS + col;
    if (!arm9_moduleLoaded || ch >= 32) return;
    if (!((s_ch_seen_active >> ch) & 1)) return;  /* ignore unused channels */

    if ((py - WAVE_GRID_TOP) % WAVE_CELL_H < WAVE_CELL_H / 2) {
        /* Top half of cell: mute toggle */
        arm9_channelMute[ch] ^= 1;
        serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)ch << 8) | arm9_channelMute[ch]);
        drawWaveCell(ch);
    } else {
        /* Bottom half of cell: toggle solo on this channel */
        if (arm9_soloMask & (1u << ch)) {
            /* Un-solo this channel */
            arm9_soloMask &= ~(1u << ch);

            if (arm9_soloMask == 0) {
                /* Last solo removed: restore pre-solo mutes */
                for (u8 i = 0; i < 32; i++) {
                    arm9_channelMute[i] = arm9_preSoloMute[i];
                    serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)i << 8) | arm9_channelMute[i]);
                }
            } else {
                /* Other solos remain: mute this channel again */
                arm9_channelMute[ch] = 1;
                serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)ch << 8) | 1);
            }
        } else {
            /* Solo this channel: save pre-solo mutes if entering fresh */
            if (arm9_soloMask == 0) {
                for (u8 i = 0; i < 32; i++)
                    arm9_preSoloMute[i] = arm9_channelMute[i];
            }
            arm9_soloMask |= (1u << ch);

            /* Mute all non-soloed channels, unmute soloed ones */
            for (u8 i = 0; i < 32; i++) {
                u8 mute = (arm9_soloMask & (1u << i)) ? 0 : 1;
                arm9_channelMute[i] = mute;
                serviceCmd(CMD_SET_CHANNEL_MUTE, ((s32)i << 8) | mute);
            }
        }
        for (u8 i = 0; i < 32; i++)
            drawWaveCell(i);
    }
}

/* ------------------------------------------------------------------ */
/* Public interface                                                      */
/* ------------------------------------------------------------------ */

void drawWaveScreen(void)
{
    bmpEnsureInit();
    bmpClearAll();
    drawWaveGrid();

    /* Invalidate change-detection state so every cell redraws on entry */
    memset(s_lastMute, 0xFF, sizeof(s_lastMute));
    memset(s_wave_buf, 0, sizeof(s_wave_buf));
    s_ch_seen_active = 0;
    s_lastSolo = 0xFFFFFFFF;  /* force redraw */

    /* Tab strip only — no per-channel text labels */
    consoleSelect(&bottom);
    consoleClear();
    drawTabStrip(SCREEN_MODE_CH);

    for (u8 ch = 0; ch < 32; ch++)
        drawWaveCell(ch);

    for (u8 ch = 0; ch < 32; ch++)
        s_lastMute[ch] = arm9_channelMute[ch];
    s_lastSolo = arm9_soloMask;
}

void waveScreen_tick(void)
{
    if (sub_bg2 < 0) return;

    /* Update "ever seen active" bitmask from wave capture data */
    if (arm9_wave_cap) {
        DC_InvalidateRange((void *)arm9_wave_cap, sizeof(WaveCapCh) * WAVE_CAP_N);
        for (u8 ch = 0; ch < 32; ch++) {
            if (arm9_wave_cap[ch].samp != 0)
                s_ch_seen_active |= (1u << ch);
        }
    }

    bool solo_changed = (arm9_soloMask != s_lastSolo);
    if (solo_changed) s_lastSolo = arm9_soloMask;

    for (u8 ch = 0; ch < 32; ch++) {
        bool mute_changed = (arm9_channelMute[ch] != s_lastMute[ch]);
        if (mute_changed) s_lastMute[ch] = arm9_channelMute[ch];
        /* Always redraw: oscilloscope scrolls every frame */
        drawWaveCell(ch);
    }
}
