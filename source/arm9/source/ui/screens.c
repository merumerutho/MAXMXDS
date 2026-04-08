/*
 * screens.c
 */
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "arm9_fifo.h"
#include "demo.h"
#include "fixmath.h"
#include <mm_mas.h>

PrintConsole top, bottom;

const float X_NORM = (float) 1 / (X_MAX - X_MIN);
const float Y_NORM = (float) 1 / (Y_MAX - Y_MIN);

int sub_bg2 = -1;
int main_bg2 = -1;

static u16 *s_top_bmp = NULL;
static u32 s_fx_frame = 0;

/* ------------------------------------------------------------------ */
/* Top screen BG2                                                       */
/* ------------------------------------------------------------------ */

/* Demoscene effect region: rows 64-127 (8 text rows of banner above,
 * stats text below from row 128 = text row 16). */
#define FX_Y0  0
#define FX_Y1  191
#define FX_H   (FX_Y1 - FX_Y0 + 1)  /* full screen */

void initTopBg(void)
{
    vramSetBankA(VRAM_A_MAIN_BG);
    /* mapBase=2 would overlap text console. Use mapBase=4 (64KB offset)
     * for the 64KB 8bpp bitmap. Text console uses tile base 0 + map base 2
     * which fits in the first 16KB. */
    main_bg2 = bgInit(2, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    bgSetPriority(main_bg2, 2);

    /* Plasma palette: 127 entries, dark-to-bright single-hue gradient.
     * Hue is updated each frame by topScreen_tick via updatePlasmaPalette. */
    BG_PALETTE[0]   = RGB15( 0,  0,  0);
    for (int i = 1; i < 128; i++)
        BG_PALETTE[i] = RGB15(0, 0, 0);  /* filled dynamically */
    BG_PALETTE[128] = RGB15(31, 31, 31);
    BG_PALETTE[129] = RGB15( 0, 16,  0);  /* dim green separator */

    s_top_bmp = (u16 *)bgGetGfxPtr(main_bg2);
    if (s_top_bmp)
        memset(s_top_bmp, 0, 256 * 192);
    demo_init(s_top_bmp);
    bgShow(main_bg2);
}


/* ------------------------------------------------------------------ */
/* Audio-reactive brightness                                            */
/* ------------------------------------------------------------------ */

/* Smoothed audio level: 0..255. One-pole IIR filter. */
static u32 s_audio_level_q8 = 0;   /* Q8 fixed-point */

static void updateAudioLevel(void)
{
    if (!arm9_wave_cap || !arm9_playing) {
        /* Decay to zero when not playing */
        s_audio_level_q8 = (s_audio_level_q8 * 240) >> 8;
        return;
    }

    /* Peak detection: find the loudest sample across all active channels */
    u32 peak = 0;
    DC_InvalidateRange((void *)arm9_wave_cap, sizeof(WaveCapCh) * WAVE_CAP_N);

    for (u8 ch = 0; ch < WAVE_CAP_N; ch++) {
        u32 desc_off = arm9_wave_cap[ch].samp;
        if (desc_off == 0) continue;

        const mm_mas_ds_sample *desc =
            (const mm_mas_ds_sample *)(0x02000000u + desc_off);
        const u8 *data = (desc->point != 0) ? (const u8 *)desc->point
                                            : (const u8 *)desc->data;
        u32 pos = arm9_wave_cap[ch].read >> 10;

        s8 sval;
        if (desc->format == 0)
            sval = (s8)data[pos];
        else
            sval = (s8)data[pos * 2 + 1];

        u32 abs_val = (sval < 0) ? (u32)(-sval) : (u32)sval;
        if (abs_val > peak) peak = abs_val;
    }

    /* Normalize: peak is 0..128 → scale to 0..255 */
    u32 level = (peak > 128) ? 255 : peak * 2;

    /* One-pole smoothing: attack fast (coeff 80), release slower (coeff 240) */
    u32 target_q8 = level << 8;
    if (target_q8 > s_audio_level_q8)
        s_audio_level_q8 = s_audio_level_q8 + ((target_q8 - s_audio_level_q8) * 80 >> 8);
    else
        s_audio_level_q8 = s_audio_level_q8 - ((s_audio_level_q8 - target_q8) * 240 >> 8);
}

/* Update the plasma palette — audio-reactive brightness */
static void updatePlasmaPalette(void)
{
    updateAudioLevel();

    u8 base_hue = (u8)(s_fx_frame / 3);
    s32 hue_warp = isin(s_fx_frame / 7) >> 2;

    /* Thresholded audio reactivity — three distinct brightness tiers.
     *   LOW  (audio < 60):  dim, moody        — scale ~0.15
     *   MID  (60..140):     moderate presence  — scale ~0.50
     *   HIGH (> 140):       punchy, vivid      — scale ~1.0
     * Hysteresis: thresholds shift by ±10 based on current tier
     * to prevent rapid flickering at boundaries. */
    static u8 s_tier = 0;  /* 0=low, 1=mid, 2=high */

    u32 audio = s_audio_level_q8 >> 8;  /* 0..255 */

    /* Hysteresis: use slightly different thresholds depending on current tier */
    if (s_tier == 0) {
        if (audio > 25)  s_tier = 1;
    } else if (s_tier == 1) {
        if (audio < 15)  s_tier = 0;
        if (audio > 65)  s_tier = 2;
    } else {
        if (audio < 45)  s_tier = 1;
    }

    u32 bright_scale;
    switch (s_tier) {
        case 0:  bright_scale = 120; break;  /* ~0.47 — baseline */
        case 1:  bright_scale = 200; break;  /* ~0.78 — moderate */
        default: bright_scale = 255; break;  /* ~1.00 — full     */
    }

    /* Smooth the scale transitions (one-pole on the scale itself) */
    static u32 s_smooth_scale_q8 = 120 << 8;
    u32 target_scale_q8 = bright_scale << 8;
    if (target_scale_q8 > s_smooth_scale_q8)
        s_smooth_scale_q8 += (target_scale_q8 - s_smooth_scale_q8) >> 2;  /* fast attack */
    else
        s_smooth_scale_q8 -= (s_smooth_scale_q8 - target_scale_q8) >> 3;  /* slower release */
    u32 final_scale = s_smooth_scale_q8 >> 8;

    for (int i = 1; i < 128; i++) {
        u8 h = base_hue + (u8)(i * 2) + (u8)((hue_warp * i) >> 7);

        s32 bright_mod = isin(i * 4 + (s_fx_frame >> 1));
        s32 v = 10 + ((i * 18) / 127) + (bright_mod >> 4);
        if (v < 6) v = 6;
        if (v > 31) v = 31;

        /* Scale brightness by smoothed tier level */
        v = (v * final_scale) >> 8;
        if (v < 1) v = 1;

        BG_PALETTE[i] = hsv15(h, 255, (u8)v);
    }
}

void topScreen_tick(void)
{
    /* Advance only while music is playing, scaled by BPM. */
    static u32 s_phase_q8 = 0;

    if (arm9_playing) {
        u32 step = (u32)arm9_globalBpm * 2;
        s_phase_q8 += step;
        s_fx_frame = s_phase_q8 >> 8;

        updateAudioLevel();
        u32 audio = s_audio_level_q8 >> 8;
        updatePlasmaPalette();

        u8 row = 0, nrows = 0;
        if (arm9_moduleState) {
            DC_InvalidateRange((void *)arm9_moduleState, sizeof(*arm9_moduleState));
            row   = arm9_moduleState->row;
            nrows = arm9_moduleState->nrows;
        }
        demo_render(s_phase_q8, audio, row, nrows);
    } else if (s_top_bmp) {
        u32 *p = (u32 *)s_top_bmp;
        for (u32 i = 0; i < 256 * 192 / 4; i++)
            p[i] = 0;
    }
}

/* Cycle to the next demo effect */
void topScreen_nextEffect(void)
{
    demo_next();
}

/* Tab strip: 3 bitmap buttons across the top 8 pixels */
#define TAB_H      8
#define NUM_TABS   3
#define TAB_W    (256 / NUM_TABS)   /* 85 px each */
#define TAB_PAD    1

static const ScreenMode s_tab_modes[NUM_TABS] = {
    SCREEN_MODE_CH, SCREEN_MODE_CUE, SCREEN_MODE_VFX
};

void drawTabStrip(ScreenMode mode)
{
    bmpEnsureInit();

    for (u8 t = 0; t < NUM_TABS; t++) {
        u16 x = t * TAB_W;
        u16 w = (t == NUM_TABS - 1) ? (256 - x) : TAB_W;  /* last tab gets remainder */
        bool active = (s_tab_modes[t] == mode);
        u8 fill = active ? PAL_WAVE_NORM : PAL_BORDER;

        bmpRectFill(x + TAB_PAD, 0, w - TAB_PAD * 2, TAB_H, fill);

        bmpHLine(TAB_H - 1, x, x + w - 1,
                 active ? PAL_WAVE_NORM : PAL_SLIDER_BG);
    }

    /* Text overlay (row 0 = top 8 pixels) — 32 chars total */
    consoleSelect(&bottom);
    iprintf("\x1b[0;0H");

    /* ~10 chars per tab zone */
    const char *ch_l  = (mode == SCREEN_MODE_CH)  ? "[ CH ]"  : "  CH  ";
    const char *cue_l = (mode == SCREEN_MODE_CUE) ? "[ CUE ]" : "  CUE  ";
    const char *vfx_l = (mode == SCREEN_MODE_VFX) ? "[ VFX ]" : "  VFX  ";
    iprintf("  %s    %s   %s ", ch_l, cue_l, vfx_l);
}

void initWaveBg(void)
{
    /* BG2 as 256x256 8bpp bitmap.
     * mapBase=4 places bitmap data at 4x16KB = 64KB into VRAM_C,
     * safely above the text console's tile (0KB) and map (4KB) regions. */
    sub_bg2 = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 4, 0);

    /* Draw BG2 behind BG0 (text console).
     * Lower priority number = higher z-order; BG0 stays on top at priority 0. */
    bgSetPriority(sub_bg2, 2);

    /* Shared palette (text console uses same BG_PALETTE_SUB).
     * Index 0 = text transparency = black. */
    BG_PALETTE_SUB[0]  = RGB15( 0,  0,  0);  /* black       — background             */
    BG_PALETTE_SUB[1]  = RGB15( 0, 12,  0);  /* dim green   — borders / lines        */
    BG_PALETTE_SUB[2]  = RGB15( 0, 31,  0);  /* bright green — waveform / active      */
    BG_PALETTE_SUB[3]  = RGB15( 0,  6,  0);  /* very dim green — muted waveform      */
    BG_PALETTE_SUB[4]  = RGB15(31, 31,  0);  /* bright yellow  — soloed / highlight   */
    BG_PALETTE_SUB[5]  = RGB15( 8, 16, 31);  /* blue        — cue button fill         */
    BG_PALETTE_SUB[6]  = RGB15( 4,  8, 16);  /* dark blue   — cue button dim          */
    BG_PALETTE_SUB[7]  = RGB15(31, 31, 31);  /* white       — text / bright UI        */
    BG_PALETTE_SUB[8]  = RGB15(16,  0, 24);  /* purple      — FX zone fill            */
    BG_PALETTE_SUB[9]  = RGB15( 8,  0, 12);  /* dark purple — FX zone dim             */
    BG_PALETTE_SUB[10] = RGB15(31,  8,  0);  /* orange      — slider knob / active    */
    BG_PALETTE_SUB[11] = RGB15(12, 12, 12);  /* grey        — slider track            */
    BG_PALETTE_SUB[12] = RGB15( 4,  4,  4);  /* dark grey   — unused channel          */

    /* Keep hidden until first screen draw */
    bgHide(sub_bg2);
}

/* ------------------------------------------------------------------ */
/* Shared bitmap drawing helpers                                        */
/* ------------------------------------------------------------------ */

static u16 *s_bmp = NULL;

void bmpEnsureInit(void)
{
    if (s_bmp == NULL && sub_bg2 >= 0)
        s_bmp = (u16 *)bgGetGfxPtr(sub_bg2);
}

void bmpClearAll(void)
{
    bmpEnsureInit();
    if (!s_bmp) return;
    u32 *p = (u32 *)s_bmp;
    for (u32 i = 0; i < 256 * 192 / 4; i++)
        p[i] = 0;
}

void bmpPixel(u16 x, u8 y, u8 c)
{
    if (!s_bmp || x > 255 || y > 191) return;
    u32 idx = (u32)y * 256 + x;
    u16 *p  = s_bmp + (idx >> 1);
    if (idx & 1)
        *p = (*p & 0x00FFu) | ((u16)c << 8);
    else
        *p = (*p & 0xFF00u) | c;
}

void bmpRectFill(u16 x, u8 y, u16 w, u8 h, u8 c)
{
    if (!s_bmp) return;
    u16 pair = ((u16)c << 8) | c;
    for (u8 row = y; row < y + h && row < 192; row++) {
        u32 base = (u32)row * 128;
        u16 x0 = x, x1 = x + w - 1;
        if (x1 > 255) x1 = 255;

        if (x0 & 1) {
            bmpPixel(x0, row, c);
            x0++;
        }
        if (x0 > x1) continue;
        if (!(x1 & 1)) {
            bmpPixel(x1, row, c);
            if (x1 == 0) continue;
            x1--;
        }
        for (u16 xi = x0 >> 1; xi <= (x1 >> 1); xi++)
            s_bmp[base + xi] = pair;
    }
}

void bmpHLine(u8 y, u16 x0, u16 x1, u8 c)
{
    bmpRectFill(x0, y, x1 - x0 + 1, 1, c);
}

void bmpVLine(u16 x, u8 y0, u8 y1, u8 c)
{
    for (u8 y = y0; y <= y1 && y < 192; y++)
        bmpPixel(x, y, c);
}

