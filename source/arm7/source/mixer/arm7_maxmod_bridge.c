#include "arm7_maxmod_bridge.h"
#include "arm7_fifo.h"
#include <nds.h>
#include <maxmod7.h>

#include "core/mas.h"
#include "core/effect.h"
#include "core/mixer.h"
#include "ds/arm7/mixer.h"
#include "ds/arm7/comms_ds7.h"

/* --- Frame hook ---
 * Replace MaxMod's Timer0 ISR with our wrapper to insert mm_apply_mutes
 * between mmPulse (which sets pattern volumes) and mmMixerMix. */

static bool s_frame_hooked = false;

void mm_bridge_hookFrame(void)
{
    if (s_frame_hooked) return;
    if (!mmIsInitialized()) return;
    irqSet(IRQ_TIMER(0), mm_bridge_wrappedFrame);
    s_frame_hooked = true;
}

void mm_bridge_wrappedFrame(void)
{
    if (mmIsInitialized())
    {
        mmMixerPre();
        REG_IME = 1;
        mmUpdateEffects();
        mmPulse();
        mm_apply_mutes();
        mmMixerMix();
        mmSendUpdateToARM9();
    }
    mmProcessComms();
}

u8 arm7_loopOnce = 0;

/* Pattern-lock: re-arms patt_jump each last row to stay on same pattern */
static bool s_patt_loop_enabled = false;

static XMXModuleState_S s_state;

/* Mute flags: written by FIFO handler, read by mm_apply_mutes in Timer0 ISR.
 * Extern so arm7_wave_capture.c can skip silent channels. */
u8 arm7_mute_flags[32] = {0};

/* Stutter: 50/50 volume gate synced to row position */
static bool s_stutter_active = false;
static u8   s_stutter_n      = 4;

/* --- Mute / stutter gate --- */

/* Mode C: all mixing in software, HW channel bits irrelevant.
 * We zero mixer volumes per-frame so muted channels produce no output. */
void mm_bridge_setMute(u8 mod_ch, u8 mute)
{
    if (mod_ch >= 32) return;
    arm7_mute_flags[mod_ch] = mute ? 1 : 0;
}

/* Runs in Timer0 ISR between mmPulse and mmMixerMix. Keep fast. */
void mm_apply_mutes(void)
{
    if (mm_pchannels == NULL) return;

    /* Stutter: silence all channels during second half of each N-row cycle */
    bool stutter_silent = false;
    if (s_stutter_active && s_stutter_n > 0) {
        u8 phase = mmLayerMain.row % s_stutter_n;
        stutter_silent = (phase >= (s_stutter_n >> 1));
    }

    for (u8 ch = 0; ch < 32; ch++) {
        if (!arm7_mute_flags[ch] && !stutter_silent) continue;

        u8 mix_idx = mm_pchannels[ch].alloc;
        if (mix_idx >= 32) continue;

        mm_mix_channels[mix_idx].vol  = 0;
        mm_mix_channels[mix_idx].cvol = 0;
    }
}

/* --- Roll ---
 *
 * Loops a section of N rows using MaxMod's patt_jump mechanism (like XM Bxx).
 * patt_jump != 255 triggers a position jump at the next tick-0 boundary;
 * MaxMod resets it to 255 after consuming. We pre-arm on the LAST row of
 * the window so the jump fires at the row transition with no overshoot.
 *
 * Shadow tracker: remembers where the song *would* be without the loop.
 * On stopLoop the sequencer jumps to the shadow position (unless -DROLL_NOSHADOW).
 * Sentinel 255 = no pending jump (position 0 is valid). */

static bool s_roll_active    = false;
static bool s_roll_stopping  = false;
static u8   s_roll_start_pos = 0;
static u8   s_roll_start_row = 0;
static u8   s_roll_n         = 4;
static bool s_jump_armed     = false;

static u8   s_shadow_pos     = 0;
static u8   s_shadow_row     = 0;
static u8   s_shadow_tick    = 0;

static u8   s_pending_hotcue = 255;

/* Advance shadow by s_roll_n rows. Uses looped pattern's nrows for
 * boundary arithmetic — good approximation for XM files. */
static void mm_bridge_advance_shadow(void)
{
    u8 rows_per_patt = mmLayerMain.nrows + 1;  /* nrows is 0-indexed last row */
    u8 mod_len       = (mmLayerMain.songadr != NULL)
                       ? mmLayerMain.songadr->order_count : 1;
    if (rows_per_patt == 0) rows_per_patt = 1;
    if (mod_len       == 0) mod_len       = 1;

    s_shadow_row += s_roll_n;

    if (s_patt_loop_enabled) {
        while (s_shadow_row >= rows_per_patt)
            s_shadow_row -= rows_per_patt;
    } else {
        while (s_shadow_row >= rows_per_patt) {
            s_shadow_row -= rows_per_patt;
            if (++s_shadow_pos >= mod_len) s_shadow_pos = 0;
        }
    }

    s_shadow_tick = mmLayerMain.tick;
}

/* -DFREE_ROLLING_LOOP: no quantization. Default: snap to ROLL_QUANT multiples. */
#ifndef FREE_ROLLING_LOOP
#define ROLL_QUANT  4
#endif

/* Capture current position as loop anchor; no immediate jump. */
void mm_bridge_startLoop(u8 n_rows)
{
    if (!mmLayerMain.isplaying) return;

#ifdef FREE_ROLLING_LOOP
    u8 new_n = (n_rows == 0) ? 1 : n_rows;
#else
    u8 new_n = (n_rows < ROLL_QUANT) ? ROLL_QUANT
             : (n_rows + ROLL_QUANT - 1) & ~(ROLL_QUANT - 1);
#endif

    if (s_roll_active) {
        s_roll_stopping = false;
        s_roll_n     = new_n;
        s_jump_armed = false;
        mmLayerMain.pattjump = 255;
    } else {
#ifdef FREE_ROLLING_LOOP
        u8 start_row = mmLayerMain.row;
#else
        /* Quantize start to lower multiple of roll length (power of 2) */
        u8 cur_row = mmLayerMain.row;
        u8 start_row = cur_row & ~(new_n - 1);
#endif

        s_roll_start_pos = mmLayerMain.position;
        s_roll_start_row = start_row;
        s_roll_n         = new_n;
        s_roll_active    = true;
        s_jump_armed     = false;
        s_shadow_pos     = s_roll_start_pos;
        s_shadow_row     = start_row;
        s_shadow_tick    = mmLayerMain.tick;
        mmLayerMain.pattjump = 255;

        /* No immediate jump — let playback continue to the end of the
         * roll window naturally. mm_bridge_sendState will arm the
         * loop-back jump when the last row is reached. */
    }
}

/* Graceful stop: finishes current cycle, then jumps to shadow position.
 * -DROLL_NOSHADOW: continues from wherever the sequencer currently is. */
void mm_bridge_stopLoop(void)
{
    if (!s_roll_active) return;
    s_roll_stopping = true;
}

/* Deferred jump via patt_jump — fires at next tick-0 boundary */
void mm_bridge_jumpTo(u8 pos, u8 row)
{
    mmLayerMain.pattjump     = pos;
    mmLayerMain.pattjump_row = row;
}

/* Queue a jump to 'pos' at end of current pattern. Cancels active roll. */
void mm_bridge_queueHotcue(u8 pos)
{
    s_roll_active         = false;
    s_roll_stopping       = false;
    s_jump_armed          = false;
    mmLayerMain.pattjump = 255;
    s_pending_hotcue      = pos;
}

void mm_bridge_nudgeTick(s8 dir)
{
    if (!mmLayerMain.isplaying) return;
    u8 speed = mmLayerMain.speed;
    if (speed == 0) return;
    s16 tick = (s16)mmLayerMain.tick + dir;
    if (tick >= (s16)speed) tick = (s16)(speed - 1);
    if (tick < 0)           tick = 0;
    mmLayerMain.tick = (u8)tick;
}

/* "Pattern loop" = lock on current order position (distinct from MM_PLAY_LOOP).
 * mm_bridge_sendState re-arms patt_jump at last row. */
void mm_bridge_setLoopMode(u8 loop_enabled)
{
    s_patt_loop_enabled = (bool)loop_enabled;
}

/* --- Stutter --- */

void mm_bridge_startStutter(u8 n_rows)
{
    s_stutter_n = (n_rows < 2) ? 2 : n_rows;
    s_stutter_active = true;
}

void mm_bridge_stopStutter(void)
{
    s_stutter_active = false;
}

/* --- State reset / send --- */

/* Must be called after mmStop (channels already cleared). */
void mm_bridge_resetDJState(void)
{
    for (u8 i = 0; i < 32; i++)
        arm7_mute_flags[i] = 0;

    s_roll_active    = false;
    s_roll_stopping  = false;
    s_jump_armed     = false;
    s_roll_start_pos = 0;
    s_roll_start_row = 0;
    s_roll_n         = 4;
    s_shadow_pos     = 0;
    s_shadow_row     = 0;
    s_shadow_tick    = 0;

    s_stutter_active = false;
    s_stutter_n      = 4;

    s_patt_loop_enabled = false;
    s_pending_hotcue    = 255;

    mmSetModulePitch(1024);
    mmSetModuleTempo(1024);
}

/* Populate s_state from mmLayerMain and send to ARM9 via two value32 messages.
 * (fifoSendAddress ARM7→ARM9 is unreliable, so we pack into CMD_STATE_W0/W1.) */
void mm_bridge_sendState(void)
{
    if (s_roll_active && mmLayerMain.isplaying) {
        u8 cur_pos  = mmLayerMain.position;
        u8 cur_row  = mmLayerMain.row;
        u8 last_row = (u8)(s_roll_start_row + s_roll_n - 1);
        if (last_row > mmLayerMain.nrows)
            last_row = mmLayerMain.nrows;

        /* Detect jump consumption (pattjump reset to 255 = MaxMod fired it) */
        if (s_jump_armed && mmLayerMain.pattjump == 255) {
            mm_bridge_advance_shadow();
            s_jump_armed = false;
        }

        /* Arm on LAST row of window so jump fires at row transition */
        bool arm = (cur_pos != s_roll_start_pos) || (cur_row >= last_row);
        if (arm && !s_jump_armed) {
            if (s_roll_stopping) {
                /* Final cycle: jump to shadow position instead of looping back */
                mm_bridge_advance_shadow();
                s_roll_active   = false;
                s_roll_stopping = false;
#ifndef ROLL_NOSHADOW
                mmLayerMain.pattjump     = s_shadow_pos;
                mmLayerMain.pattjump_row = s_shadow_row;
#endif
                s_jump_armed = true;
                goto roll_done;
            }

            mmLayerMain.pattjump     = s_roll_start_pos;
            mmLayerMain.pattjump_row = s_roll_start_row;
            s_jump_armed = true;
        }
    }
    roll_done:

    /* Hotcue: jump at end of current pattern */
    if (s_pending_hotcue != 255 && mmLayerMain.isplaying) {
        if (mmLayerMain.row >= mmLayerMain.nrows) {
            mmLayerMain.pattjump     = s_pending_hotcue;
            mmLayerMain.pattjump_row = 0;
            s_pending_hotcue          = 255;
        }
    }

    /* Pattern lock: re-arm patt_jump at last row to stay on current position.
     * Roll and hotcue take priority. */
    if (s_patt_loop_enabled && !s_roll_active && s_pending_hotcue == 255
            && mmLayerMain.isplaying) {
        if (mmLayerMain.row >= mmLayerMain.nrows) {
            mmLayerMain.pattjump     = mmLayerMain.position;
            mmLayerMain.pattjump_row = 0;
        }
    }

    if (!mmLayerMain.isplaying) return;

    s_state.position = mmLayerMain.position;
    s_state.row      = mmLayerMain.row;
    s_state.bpm      = mmLayerMain.bpm;
    s_state.active   = mmLayerMain.isplaying;
    s_state.nrows    = mmLayerMain.nrows;
    s_state.speed    = mmLayerMain.speed;

    s_state.module_length = (mmLayerMain.songadr != NULL)
                            ? mmLayerMain.songadr->order_count
                            : 0;

    u32 w0 = ((u32)s_state.position     << 16)
           | ((u32)s_state.row          <<  8)
           | ((u32)s_state.bpm               );
    u32 w1 = ((u32)s_state.active       << 16)
           | ((u32)s_state.nrows        <<  8)
           | ((u32)s_state.module_length     );
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_STATE_W0, w0));
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_STATE_W1, w1));
}
