/*
 * demo.c — Demoscene effects for the top screen BG2 bitmap.
 * 8bpp palette-indexed, BPM-synced, audio-reactive.
 *
 * t is Q8 fixed-point (8 fractional bits) for sub-frame smooth motion.
 *
 * Full-grid effects (plasma, metaballs, tunnel, moiré, fire) use 4-phase
 * interlacing when the cell count exceeds INTERLACE_THRESHOLD: each frame
 * renders 1/4 of the cells (even/odd row × even/odd column), cycling
 * through all 4 quadrants over 4 VBlanks. No full-screen clear needed —
 * each cell is overwritten in place.
 *
 * Cheap effects (sine balls, starfield, hex tunnel) render fully each
 * frame since their cost is independent of grid size.
 *
 * 0:Plasma  1:Metaballs  2:Tunnel  3:SineBalls
 * 4:Fire    5:Starfield  6:Moiré   7:HexTunnel
 */

#include "demo.h"
#include "fixmath.h"
#include <string.h>

#define SCREEN_W  256
#define SCREEN_H  192

static u8 s_px     = 4;
static u8 s_stride = 0;

#define NUM_EFFECTS  8

/* Above this cell count, full-grid effects interlace across 4 frames */
#define INTERLACE_THRESHOLD  1024

static u16 *s_bmp = NULL;
static u8   s_effect = 0;
static u8   s_phase  = 0;   /* interlace phase 0..3 */
static bool s_interlace = false;

/* Per-frame grid geometry, set at the start of demo_render */
static s32 g_cell, g_vcols, g_vrows;
#define BX(vx)  ((vx) * g_cell)
#define BY(vy)  ((vy) * g_cell)

/* Interlace phase → (col parity, row parity) */
static u8 s_phase_cx, s_phase_cy;


/* Ramp 0→255 over the last 1/8th of the pattern, for transition morphing. */
static u32 patternTransition(u8 row, u8 nrows)
{
    if (nrows < 8) return 0;
    u8 threshold = nrows - (nrows >> 3);
    if (row < threshold) return 0;
    return ((u32)(row - threshold) * 255) / (nrows - threshold);
}

/* --- Block writer --- */

/* 8bpp bitmap is 256px wide = 128 u16 words per row.
 * Handles odd pixel alignment since two pixels share one u16. */
static inline void putBlock(s32 px0, s32 py0, u8 c)
{
    s32 sz = s_px;
    if (px0 >= SCREEN_W || py0 >= SCREEN_H || px0 + sz <= 0 || py0 + sz <= 0)
        return;

    s32 x0 = (px0 < 0) ? 0 : px0;
    s32 y0 = (py0 < 0) ? 0 : py0;
    s32 x1 = px0 + sz; if (x1 > SCREEN_W) x1 = SCREEN_W;
    s32 y1 = py0 + sz; if (y1 > SCREEN_H) y1 = SCREEN_H;

    u16 pair = (u16)c | ((u16)c << 8);
    for (s32 y = y0; y < y1; y++) {
        u32 row = (u32)y * 128;
        s32 x = x0;

        if (x & 1) {
            u16 *p = s_bmp + ((u32)y * 128 + (x >> 1));
            *p = (*p & 0x00FFu) | ((u16)c << 8);
            x++;
        }
        for (; x + 1 < x1; x += 2)
            s_bmp[row + (x >> 1)] = pair;
        if (x < x1) {
            u16 *p = s_bmp + ((u32)y * 128 + (x >> 1));
            *p = (*p & 0xFF00u) | c;
        }
    }
}

static void clearScreen(void)
{
    u32 *p = (u32 *)s_bmp;
    for (u32 i = 0; i < SCREEN_W * SCREEN_H / 4; i++)
        p[i] = 0;
}

/* --- Bresenham line on the virtual grid --- */

static void gridLine(s32 x0, s32 y0, s32 x1, s32 y1, u8 c, bool stipple)
{
    s32 dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    s32 dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    s32 sx = (x0 < x1) ? 1 : -1;
    s32 sy = (y0 < y1) ? 1 : -1;
    s32 err = dx - dy;
    u8 step = 0;

    for (;;) {
        if (!stipple || (step & 1) == 0)
            putBlock(BX(x0), BY(y0), c);
        if (x0 == x1 && y0 == y1) break;
        s32 e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
        step++;
    }
}

/* --- Interlaced grid iteration macros ---
 * When interlacing, only process cells matching the current phase parity.
 * When not interlacing, process all cells (step=1, start=0). */
#define GRID_STEP   (s_interlace ? 2 : 1)
#define GRID_Y0     (s_interlace ? s_phase_cy : 0)
#define GRID_X0     (s_interlace ? s_phase_cx : 0)

/* ---- Effect 0: Plasma ---- */

/* Three sine waves interfere on a fixed grid.
 * trans: shrinks zoom, amplifies warp for a "tightening" feel. */
static void renderPlasma(u32 t, u32 audio, u32 trans)
{
    s32 t8 = t >> 8;
    s32 step = GRID_STEP;

    s32 zoom   = 10/s_px + isin(t / (17 << 8)) - (s32)(trans >> 2);
    s32 drift  = isin(t / (5 << 8)) >> 1;
    s32 warp   = 64 + (isin(t / (7 << 8)) >> 1) + (s32)(trans >> 2);
    s32 speed1 = 2 + (isin(t / (11 << 8)) >> 5);
    s32 speed2 = 3 + (isin(t / (13 << 8)) >> 5);

    for (s32 vy = GRID_Y0; vy < g_vrows; vy += step) {
        s32 sy = (vy * zoom) >> 7;
        s32 v1 = isin(sy + t8 * speed1);

        for (s32 vx = GRID_X0; vx < g_vcols; vx += step) {
            s32 sx = (vx * zoom) >> 7;

            s32 v0 = isin(sx + t8 * speed2 + drift);
            s32 v2 = isin((((sx + sy) * warp) >> 6) + t8);
            s32 val = (v0 + v1 + v2 + 381) / 3;
            u8 c = 1 + (u8)(val >> 1);
            if (c > 127) c = 127;

            putBlock(BX(vx), BY(vy), c);
        }
    }
}

/* ---- Effect 1: Metaballs ---- */

/* Inverse-square field from 4 orbiting blobs.
 * trans: orbits shrink, blobs converge toward center. */

#define NUM_BALLS  4

static void renderMetaballs(u32 t, u32 audio, u32 trans)
{
    s32 step = GRID_STEP;
    s32 ball_vx[NUM_BALLS], ball_vy[NUM_BALLS];
    s32 radius = 800 + (audio * 12);

    for (u8 b = 0; b < NUM_BALLS; b++) {
        s32 speed = (b + 1) * 3 + b;
        s32 rx = 20 + b * 5;
        s32 ry = 12 + b * 3;
        s32 shrink = (s32)(trans * rx) >> 9;
        ball_vx[b] = g_vcols / 2 + (isin((t * speed) / (4 << 8) + b * 64) * (rx - shrink)) / 127;
        ball_vy[b] = g_vrows / 2 + (icos((t * speed) / (5 << 8) + b * 80) * (ry - (shrink * ry / rx))) / 127;
    }

    for (s32 vy = GRID_Y0; vy < g_vrows; vy += step) {
        for (s32 vx = GRID_X0; vx < g_vcols; vx += step) {
            u32 field = 0;
            for (u8 b = 0; b < NUM_BALLS; b++) {
                s32 dx = vx - ball_vx[b];
                s32 dy = vy - ball_vy[b];
                u32 dist_sq = dx * dx + dy * dy;
                if (dist_sq < 1) dist_sq = 1;
                field += radius / dist_sq;
            }

            if (field > 254) field = 254;
            u8 c = 1 + (u8)(field >> 1);
            if (c > 127) c = 127;

            putBlock(BX(vx), BY(vy), c);
        }
    }
}

/* ---- Effect 2: Tunnel ---- */

/* Polar→texture mapping: distance controls depth, atan2 controls angle.
 * Rotates the texture space; XOR gives a checker-like pattern.
 * trans: increases zoom_base → tunnel "pulls closer". */
static void renderTunnel(u32 t, u32 audio, u32 trans)
{
    s32 t8 = t >> 8;
    s32 step = GRID_STEP;
    s32 angle = (t >> 7);
    s32 zoom_base = 22 + (audio >> 2) + (s32)(trans >> 3);
    s32 cos_a = icos(angle);
    s32 sin_a = isin(angle);
    s32 tx_off = t8 * 5;
    s32 ty_off = t8 * 3;

    s32 cx = g_vcols / 2;
    s32 cy = g_vrows / 2;

    for (s32 vy = GRID_Y0; vy < g_vrows; vy += step) {
        for (s32 vx = GRID_X0; vx < g_vcols; vx += step) {
            s32 dx = vx - cx;
            s32 dy = vy - cy;

            u32 dist = isqrt(dx * dx + dy * dy);
            if (dist < 1) dist = 1;

            s32 depth = (zoom_base * 64) / dist + tx_off;

            s32 ang = 0;
            if (dx != 0 || dy != 0) {
                s32 adx = (dx < 0) ? -dx : dx;
                s32 ady = (dy < 0) ? -dy : dy;
                if (adx > ady)
                    ang = (ady * 32) / adx;
                else
                    ang = 64 - (adx * 32) / ady;
                if (dx < 0) ang = 128 - ang;
                if (dy < 0) ang = 256 - ang;
            }

            s32 u = (depth * cos_a - ang * sin_a) >> 7;
            s32 v = (depth * sin_a + ang * cos_a) >> 7;

            u8 tex = ((u + ty_off) ^ (v + tx_off)) & 0x7F;
            u8 c = 1 + tex;
            if (c > 127) c = 127;

            putBlock(BX(vx), BY(vy), c);
        }
    }
}

/* ---- Effect 3: Sine balls ---- */

/* 24 dots on slowly drifting Lissajous orbits; 2×2 core + dim halo.
 * trans: orbits tighten → all dots converge to center.
 * Always renders fully — cost is O(NUM_SINE_BALLS), not O(grid). */

#define NUM_SINE_BALLS  24

static void renderSineBalls(u32 t, u32 audio, u32 trans)
{
    s32 t8 = t >> 8;

    s32 phase_spread = 11 + (isin(t / (19 << 8)) >> 3);

    s32 rx = g_vcols / 3 + (isin(t / (11 << 8)) >> 4) + (audio >> 6);
    s32 ry = g_vrows / 3 + (icos(t / (13 << 8)) >> 4) + (audio >> 7);

    rx = rx - ((s32)(trans * rx) >> 9);
    ry = ry - ((s32)(trans * ry) >> 9);
    if (rx < 2) rx = 2;
    if (ry < 2) ry = 2;

    s32 cx = g_vcols / 2;
    s32 cy = g_vrows / 2;

    for (u8 b = 0; b < NUM_SINE_BALLS; b++) {
        s32 phase = b * phase_spread + (isin(t / (23 << 8) + b * 11) >> 3);
        s32 vx = cx + (isin(t8 + phase) * rx) / 127;
        s32 vy = cy + (isin(t8 + phase * 3 / 2 + 64) * ry) / 127;

        s32 hue_shift = t8 / 2 + b * (127 / NUM_SINE_BALLS) + (audio >> 3);
        u8 c = 1 + ((u8)hue_shift & 0x7E);

        /* 2×2 bright core + 4 dim neighbors for a soft-dot look */
        putBlock(BX(vx),     BY(vy),     c);
        putBlock(BX(vx + 1), BY(vy),     c);
        putBlock(BX(vx),     BY(vy + 1), c);
        putBlock(BX(vx + 1), BY(vy + 1), c);
        u8 dim = (c > 30) ? c - 30 : 1;
        putBlock(BX(vx - 1), BY(vy),     dim);
        putBlock(BX(vx + 2), BY(vy),     dim);
        putBlock(BX(vx),     BY(vy - 1), dim);
        putBlock(BX(vx),     BY(vy + 2), dim);
    }
}

/* ---- Effect 4: Fire ---- */

/* Bottom-up heat propagation: seed random heat at the bottom,
 * average 4 neighbors upward with -1 decay each row.
 * trans: boosts heat seed threshold → bigger flames at pattern end.
 * Simulation always runs on full grid; only the blit phase interlaces. */

/* Worst case PX=1 GAP=0: 256×(192+2) = ~49KB. Fits in DS main RAM. */
#define FIRE_MAX_W  SCREEN_W
#define FIRE_MAX_H  (SCREEN_H + 2)
static u8 s_fire_buf[FIRE_MAX_W * FIRE_MAX_H];

static void renderFire(u32 t, u32 audio, u32 trans)
{
    s32 t8 = t >> 8;
    s32 step = GRID_STEP;
    u8 *buf = s_fire_buf;
    s32 w = g_vcols;
    s32 h = g_vrows + 2;

    /* Xorshift PRNG — seeded from time+audio for non-repeating patterns */
    u32 rng = t8 * 2654435761u + audio * 31;
    s32 heat_boost = 80 + (audio >> 1) + (s32)(trans >> 1);
    if (heat_boost > 200) heat_boost = 200;

    /* Seed bottom two rows */
    for (s32 x = 0; x < w; x++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        u8 seed = (rng & 0xFF);
        seed = (seed < heat_boost) ? 127 : (seed >> 2);
        buf[(h - 1) * w + x] = seed;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        seed = (rng & 0xFF);
        seed = (seed < heat_boost) ? 127 : (seed >> 2);
        buf[(h - 2) * w + x] = seed;
    }

    /* Full propagation (cheap — just adds/shifts, no trig) */
    for (s32 y = 0; y < h - 2; y++) {
        for (s32 x = 0; x < w; x++) {
            s32 xl = (x > 0) ? x - 1 : w - 1;
            s32 xr = (x < w - 1) ? x + 1 : 0;
            s32 sum = buf[(y + 1) * w + xl]
                    + buf[(y + 1) * w + x]
                    + buf[(y + 1) * w + xr]
                    + buf[(y + 2) * w + x];
            s32 val = (sum >> 2) - 1;
            if (val < 0) val = 0;
            buf[y * w + x] = (u8)val;
        }
    }

    /* Blit phase: interlaced when grid is large */
    for (s32 vy = GRID_Y0; vy < g_vrows; vy += step) {
        for (s32 vx = GRID_X0; vx < w; vx += step) {
            u8 c = buf[vy * w + vx];
            if (c > 127) c = 127;
            putBlock(BX(vx), BY(vy), c);
        }
    }
}

/* ---- Effect 5: Starfield ---- */

/* 80 particles in 3D space, perspective-projected.
 * z decreases each frame (fly-through); recycled when z < threshold.
 * trans: increases fly speed → warp-drive acceleration.
 * Always renders fully — cost is O(NUM_STARS). */

#define NUM_STARS  80

static struct { s32 x, y, z; } s_stars[NUM_STARS];
static bool s_stars_init = false;

static u32 s_star_rng = 12345;
static inline u32 star_rand(void)
{
    s_star_rng ^= s_star_rng << 13;
    s_star_rng ^= s_star_rng >> 17;
    s_star_rng ^= s_star_rng << 5;
    return s_star_rng;
}

static void renderStarfield(u32 t, u32 audio, u32 trans)
{
    s32 cx = SCREEN_W / 2;
    s32 cy = SCREEN_H / 2;
    s32 speed = 12 + (audio >> 4) + (s32)(trans >> 3);

    if (!s_stars_init) {
        for (u8 i = 0; i < NUM_STARS; i++) {
            s_stars[i].x = (s32)(star_rand() % (SCREEN_W * 2)) - SCREEN_W;
            s_stars[i].y = (s32)(star_rand() % (SCREEN_H * 2)) - SCREEN_H;
            s_stars[i].z = (s32)(star_rand() % 512) + 64;
        }
        s_stars_init = true;
    }

    for (u8 i = 0; i < NUM_STARS; i++) {
        s_stars[i].z -= speed;
        if (s_stars[i].z <= 8) {
            s_stars[i].x = (s32)(star_rand() % (SCREEN_W * 2)) - SCREEN_W;
            s_stars[i].y = (s32)(star_rand() % (SCREEN_H * 2)) - SCREEN_H;
            s_stars[i].z = 512 + (s32)(star_rand() % 128);
        }

        s32 sx = cx + (s_stars[i].x * 256) / s_stars[i].z;
        s32 sy = cy + (s_stars[i].y * 256) / s_stars[i].z;

        s32 bright = 127 - (s_stars[i].z >> 2);
        if (bright < 1) bright = 1;
        if (bright > 127) bright = 127;

        if (s_stars[i].z < 128) {
            putBlock(sx, sy, (u8)bright);
            putBlock(sx + g_cell, sy, (u8)(bright >> 1));
            putBlock(sx, sy + g_cell, (u8)(bright >> 1));
        } else {
            putBlock(sx, sy, (u8)bright);
        }
    }
}

/* ---- Effect 6: Moiré ---- */

/* XOR of two concentric ring patterns from orbiting centers.
 * trans: tightens ring spacing → more frantic interference. */
static void renderMoire(u32 t, u32 audio, u32 trans)
{
    s32 t8 = t >> 8;
    s32 step = GRID_STEP;

    s32 cx1 = g_vcols / 2 + (isin(t / (3 << 8)) * (g_vcols / 4)) / 127;
    s32 cy1 = g_vrows / 2 + (icos(t / (4 << 8)) * (g_vrows / 4)) / 127;
    s32 cx2 = g_vcols / 2 + (isin(t / (5 << 8) + 128) * (g_vcols / 4)) / 127;
    s32 cy2 = g_vrows / 2 + (icos(t / (3 << 8) + 64) * (g_vrows / 4)) / 127;

    s32 ring_scale = 8 + (audio >> 5) - (s32)(trans >> 4);
    if (ring_scale < 3) ring_scale = 3;

    for (s32 vy = GRID_Y0; vy < g_vrows; vy += step) {
        for (s32 vx = GRID_X0; vx < g_vcols; vx += step) {
            s32 dx1 = vx - cx1;
            s32 dy1 = vy - cy1;
            s32 dx2 = vx - cx2;
            s32 dy2 = vy - cy2;

            u32 d1 = isqrt(dx1 * dx1 + dy1 * dy1);
            u32 d2 = isqrt(dx2 * dx2 + dy2 * dy2);

            u8 r1 = (d1 * ring_scale) & 0x7F;
            u8 r2 = (d2 * ring_scale) & 0x7F;
            u8 c = (r1 ^ r2);

            c = (c + (u8)(t8 * 2)) & 0x7F;
            c = 1 + (c >> 0);
            if (c > 127) c = 127;

            putBlock(BX(vx), BY(vy), c);
        }
    }
}

/* ---- Effect 7: Hex tunnel ---- */

/* Nested hexagons scaling outward from center, with slow rotation.
 * Inner (closer) rings are drawn thicker by layering parallel hex outlines
 * at slightly smaller scales, each dimmer — gives a glowing depth effect.
 * Thickness varies with ring index, time, and audio.
 * Outer rings drawn stippled (dashed) for a CRT vector-display look.
 * Always renders fully — cost is O(HEX_RINGS × layers × 6 edges). */

#define HEX_RINGS  10
#define HEX_MAX_LAYERS  4

/* Q7 unit hex vertices: cos/sin at 60° steps, scaled to ±127 */
static const s32 hex_dx[6] = { 127,  63, -63, -127, -63,  63 };
static const s32 hex_dy[6] = {   0, 110, 110,    0,-110,-110 };

static void renderHexTunnel(u32 t, u32 audio, u32 trans)
{
    s32 cx = g_vcols / 2;
    s32 cy = g_vrows / 2;
    s32 speed_q8 = 80 + (audio >> 1) + (s32)(trans >> 1);

    s32 rot_idx = t / (8 << 8) + (s32)(trans >> 3);
    s32 cos_r = icos(rot_idx);
    s32 sin_r = isin(rot_idx);

    /* Depth layer count pulses slowly with time, boosted by audio */
    s32 layer_pulse = 2 + (isin(t / (6 << 8)) >> 6) + (audio >> 7);
    if (layer_pulse < 1) layer_pulse = 1;
    if (layer_pulse > HEX_MAX_LAYERS) layer_pulse = HEX_MAX_LAYERS;

    u32 phase_q8 = (u32)((t >> 4) * speed_q8) >> 8;

    for (u8 r = 0; r < HEX_RINGS; r++) {
        s32 max_r = (g_vcols > g_vrows ? g_vcols : g_vrows) / 2 + 4;
        u32 ring_phase = (phase_q8 + r * ((max_r << 8) / HEX_RINGS)) & 0xFFFF;
        s32 scale = (s32)((ring_phase >> 8) % max_r);

        if (scale < 1) continue;

        /* Base brightness: closer to center = brighter */
        s32 bright = 127 - (scale * 100 / max_r);
        if (bright < 10) bright = 10;

        /* Thickness: inner rings get more layers than outer rings */
        s32 depth_frac = 256 - (scale * 256 / max_r);  /* 256=center, 0=edge */
        s32 layers = 1 + (depth_frac * layer_pulse) / 256;
        if (layers > HEX_MAX_LAYERS) layers = HEX_MAX_LAYERS;

        bool stipple = (scale > max_r * 2 / 3);

        /* Draw layers from outermost (dimmest) to innermost (brightest)
         * so the bright core overwrites the dim halo. */
        for (s32 L = layers - 1; L >= 0; L--) {
            s32 layer_scale = scale - L;
            if (layer_scale < 1) continue;

            /* Each layer dims by ~30% from the previous */
            s32 lbright = bright - L * (bright / 3);
            if (lbright < 5) lbright = 5;

            s32 vx[6], vy[6];
            for (u8 i = 0; i < 6; i++) {
                s32 rx = (hex_dx[i] * cos_r - hex_dy[i] * sin_r) >> 7;
                s32 ry = (hex_dx[i] * sin_r + hex_dy[i] * cos_r) >> 7;
                vx[i] = cx + (rx * layer_scale) / 127;
                vy[i] = cy + (ry * layer_scale) / 127;
            }

            /* Only stipple the outermost layer */
            bool stip = stipple && (L == layers - 1);
            for (u8 i = 0; i < 6; i++) {
                u8 j = (i + 1) % 6;
                gridLine(vx[i], vy[i], vx[j], vy[j], (u8)lbright, stip);
            }
        }
    }
}

/* ---- Public interface ---- */

void demo_init(u16 *bmp)
{
    s_bmp = bmp;
    s_effect = 7;
    s_phase  = 0;
    s_stars_init = false;
    memset(s_fire_buf, 0, sizeof(s_fire_buf));
}

void demo_next(void)
{
    s_effect = (s_effect + 1) % NUM_EFFECTS;
    if (s_effect == 4) memset(s_fire_buf, 0, sizeof(s_fire_buf));
    if (s_effect == 5) s_stars_init = false;
}

void demo_select(u8 idx)
{
    if (idx >= NUM_EFFECTS) return;
    s_effect = idx;
    if (idx == 4) memset(s_fire_buf, 0, sizeof(s_fire_buf));
    if (idx == 5) s_stars_init = false;
}

u8 demo_current(void) { return s_effect; }
u8 demo_count(void)   { return NUM_EFFECTS; }

void demo_set_px(u8 px)     { if (px >= 1 && px <= 8) s_px = px; }
void demo_set_stride(u8 st) { if (st <= 8) s_stride = st; }
u8   demo_get_px(void)      { return s_px; }
u8   demo_get_stride(void)  { return s_stride; }

/* t is Q8 fixed-point (BPM-synced phase accumulator). */
void demo_render(u32 t, u32 audio, u8 row, u8 nrows)
{
    if (!s_bmp) return;

    /* Recompute grid geometry from current PX/STRIDE */
    g_cell  = s_px + s_stride;
    if (g_cell < 1) g_cell = 1;
    g_vcols = (SCREEN_W + g_cell - 1) / g_cell;
    g_vrows = (SCREEN_H + g_cell - 1) / g_cell;

    /* Decide whether to interlace this frame */
    s32 total_cells = g_vcols * g_vrows;
    s_interlace = (total_cells > INTERLACE_THRESHOLD);

    /* Advance interlace phase: 0=(0,0) 1=(1,1) 2=(0,1) 3=(1,0) */
    static const u8 phase_cx[4] = {0, 1, 0, 1};
    static const u8 phase_cy[4] = {0, 1, 1, 0};
    s_phase_cx = phase_cx[s_phase];
    s_phase_cy = phase_cy[s_phase];
    s_phase = (s_phase + 1) & 3;

    /* Cheap effects clear every frame. Full-grid effects clear once per
     * 4-phase cycle (at phase 0) to wipe ghost trails from moving cells. */
    bool is_cheap = (s_effect == 3 || s_effect == 5 || s_effect == 7);
    if (is_cheap || !s_interlace || s_phase == 1)
        clearScreen();

    u32 trans = patternTransition(row, nrows);

    switch (s_effect) {
        case 0: renderPlasma(t, audio, trans);      break;
        case 1: renderMetaballs(t, audio, trans);    break;
        case 2: renderTunnel(t, audio, trans);       break;
        case 3: renderSineBalls(t, audio, trans);    break;
        case 4: renderFire(t, audio, trans);         break;
        case 5: renderStarfield(t, audio, trans);    break;
        case 6: renderMoire(t, audio, trans);        break;
        case 7: renderHexTunnel(t, audio, trans);    break;
    }
}
