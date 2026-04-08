#ifndef ARM7_MAXMOD_BRIDGE_H
#define ARM7_MAXMOD_BRIDGE_H

#include <nds.h>
#include <mm_types.h>
#include <mm_mas.h>

/* BlocksDS maxmod internals — binary-compatible with the old ASM layout */
#include "core/player_types.h"
#include "core/channel_types.h"

extern mpl_layer_information mmLayerMain;
extern mm_module_channel *mm_pchannels;
extern mm_mixer_channel mm_mix_channels[];

extern u8 arm7_mute_flags[32];  /* per-channel mute: 1=muted */
extern u8 arm7_loopOnce;        /* 0=loop, 1=play once */

/* Muting: zeroes mixer volumes between mmPulse and mmMixerMix.
 * Also applies stutter gate when active. Runs in Timer0 ISR — keep fast. */
void mm_bridge_setMute(u8 mod_ch, u8 mute);
void mm_apply_mutes(void);

/* Roll: loops a section of N rows using MaxMod's patt_jump mechanism.
 * Shadow tracker remembers where the song *would* be without the loop. */
void mm_bridge_startLoop(u8 times);
void mm_bridge_stopLoop(void);

/* Position jump via patt_jump — fires at next tick-0 boundary (like XM Bxx). */
void mm_bridge_jumpTo(u8 pos, u8 row);

/* Queued hotcue: jumps to 'pos' at end of current pattern.
 * Cancels any active roll. (-DHOTCUE_JUMP_IMMEDIATELY skips the queue.) */
void mm_bridge_queueHotcue(u8 pos);

void mm_bridge_sendState(void);

/* Nudge tick ±1 within current row. Clamped to [0, speed-1]. */
void mm_bridge_nudgeTick(s8 dir);

/* Pattern loop mode: locks sequencer on current order position. */
void mm_bridge_setLoopMode(u8 loop_once);

/* Stutter: 50/50 volume gate at the given row division.
 * Shares division parameter with the rolling looper. */
void mm_bridge_startStutter(u8 n_rows);
void mm_bridge_stopStutter(void);

/* Full DJ state reset after module unload. Call after mmStop. */
void mm_bridge_resetDJState(void);

/* Re-register our mmFrame wrapper as Timer0 ISR (once only). */
void mm_bridge_hookFrame(void);
void mm_bridge_wrappedFrame(void);

#endif /* ARM7_MAXMOD_BRIDGE_H */
