#ifndef DEMO_H
#define DEMO_H

#include <nds.h>

void demo_init(u16 *bmp);
void demo_render(u32 t, u32 audio, u8 row, u8 nrows);
void demo_next(void);
void demo_select(u8 idx);
u8   demo_current(void);
u8   demo_count(void);

/* Runtime block size (1..8) and gap between blocks (0..8). */
void demo_set_px(u8 px);
void demo_set_stride(u8 stride);
u8   demo_get_px(void);
u8   demo_get_stride(void);

#endif /* DEMO_H */
