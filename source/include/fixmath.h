#ifndef FIXMATH_H
#define FIXMATH_H

#include <nds.h>

/* Quadratic sine approximation: input 0..255 (full cycle), output -127..+127.
 * C1 continuous — smooth enough for visual effects. */
static inline s32 isin(s32 x)
{
    x &= 255;
    s32 half = (x < 128) ? x : (x - 256);
    s32 norm = (half < 0) ? (-half - 64) : (half - 64);
    s32 val = (127 * (4096 - norm * norm)) >> 12;
    return (x < 128) ? val : -val;
}

static inline s32 icos(s32 x) { return isin(x + 64); }

/* Newton's method integer sqrt */
static inline u32 isqrt(u32 n)
{
    if (n < 2) return n;
    u32 x = n;
    u32 y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return x;
}

/* HSV → RGB15. h=0..255 (hue), s=0..255 (saturation), v=0..31 (brightness). */
static inline u16 hsv15(u8 h, u8 s, u8 v)
{
    u8 region = h / 43;
    u8 remainder = (h - region * 43) * 6;

    u8 p = (v * (255 - s)) >> 8;
    u8 q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    u8 t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  return RGB15(v, t, p);
        case 1:  return RGB15(q, v, p);
        case 2:  return RGB15(p, v, t);
        case 3:  return RGB15(p, q, v);
        case 4:  return RGB15(t, p, v);
        default: return RGB15(v, p, q);
    }
}

#endif /* FIXMATH_H */
