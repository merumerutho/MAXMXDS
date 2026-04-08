#ifndef ARM7_SOURCE_ARM7_FIFO_H_
#define ARM7_SOURCE_ARM7_FIFO_H_

#include <nds.h>
#include "ipc_defs.h"

/* Pending commands: set from FIFO IRQ, consumed in main loop
 * (MaxMod functions are not IRQ-safe). */
extern volatile u32  arm7_pendingMASAddr;
extern volatile bool arm7_pendingStop;
extern volatile bool arm7_pendingUnload;

void arm7_XMXValueHandler(u32 value, void *userdata);
void arm7_dbgStep(u32 step);
void arm7_playMASHandler(void *addr, void *userdata);
void arm7_sendBeatPulse(u8 row);
void arm7_beat_set_bpm(u8 bpm);

/* Per-VBlank: fires CMD_BEAT_PULSE at BPM rate (3600/bpm frames). */
void arm7_beat_tick(bool playing);

#endif /* ARM7_SOURCE_ARM7_FIFO_H_ */
