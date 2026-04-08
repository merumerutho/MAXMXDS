#ifndef ARM7_WAVE_CAPTURE_H_
#define ARM7_WAVE_CAPTURE_H_

#include <nds.h>
#include "ipc_defs.h"

/* Set the shared WaveCapCh[] buffer address (from ARM9 via CMD_WAVE_CAP_PTR). */
void wave_capture_set_buf(u32 arm9_addr);

/* Per-VBlank: snapshot each mixer channel's position into the shared buffer
 * so ARM9 can render per-channel oscilloscopes. Not IRQ-safe. */
void wave_capture_tick(void);

#endif /* ARM7_WAVE_CAPTURE_H_ */
