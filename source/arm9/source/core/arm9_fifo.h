#ifndef ARM9_SOURCE_ARM9_FIFO_H_
#define ARM9_SOURCE_ARM9_FIFO_H_

#include "ipc_defs.h"
#include "arm9_defines.h"

/* ARM9 shadow state — sent to ARM7 via IPC */
extern vu8  arm9_globalBpm;
extern vu8  arm9_globalTempo;
extern vs8  arm9_globalTranspose;
extern vu8  arm9_globalLoopMode;
extern vu8  arm9_bpmLock;
extern u8   arm9_channelMute[32];
extern vu8  arm9_beatCounter;      /* counts down each frame; >0 = beat flash */

extern vu8  arm9_rollActive;
extern vu8  arm9_rollN;
extern vu8  arm9_stutterActive;

extern u8   arm9_cuePoints[N_CUES];
extern u32  arm9_soloMask;         /* bit N set = channel N soloed */
extern u8   arm9_preSoloMute[32];

/* NULL until first ARM7 state update after module load.
 * DC_InvalidateRange before reading. */
extern XMXModuleState_S *arm9_moduleState;
extern volatile u32      arm9_dbgStep;

/* Module's native BPM, captured on first state update. Basis for tempo ratio. */
extern u8 arm9_baseBpm;

extern bool arm9_moduleLoaded;
extern bool arm9_playing;  /* tracked on ARM9 since mmActive() is unreliable */

/* Oscilloscope buffer in main RAM. ARM7 writes samp/read; ARM9 reads PCM. */
extern WaveCapCh *arm9_wave_cap;

void arm9_wave_cap_init(void);

/* Apply BPM scaler + optional ±4% nudge (or tick-nudge with -DTICK_NUDGE). */
void serviceUpdate(int8 nudge);

/* Send a DJ command to ARM7 via FIFO_XMX. */
void serviceCmd(u32 cmd, s32 param);

void arm9_resetChannelState(void);
void arm9_XMXValueHandler(u32 value, void *userdata);

#endif /* ARM9_SOURCE_ARM9_FIFO_H_ */
