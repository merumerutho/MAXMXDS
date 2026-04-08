#include "arm9_fifo.h"
#include "arm9_defines.h"
#include <maxmod9.h>
#include <stdlib.h>
#include <string.h>

vu8  arm9_globalBpm       = DEFAULT_BPM;
vu8  arm9_globalTempo     = DEFAULT_TEMPO;
vs8  arm9_globalTranspose = 0;
vu8  arm9_globalLoopMode  = 0;
vu8  arm9_bpmLock         = 0;
u8   arm9_channelMute[32] = {0};
vu8  arm9_beatCounter     = 0;

vu8  arm9_rollActive    = 0;
vu8  arm9_rollN         = 4;
vu8  arm9_stutterActive = 0;

u8   arm9_cuePoints[N_CUES] = {0};
u32  arm9_soloMask           = 0;
u8   arm9_preSoloMute[32]    = {0};

static XMXModuleState_S s_arm9_state = {0};
XMXModuleState_S *arm9_moduleState  = NULL;
volatile u32      arm9_dbgStep     = 0;
u8                arm9_baseBpm     = DEFAULT_BPM;
bool              arm9_moduleLoaded = false;
bool              arm9_playing      = false;
WaveCapCh        *arm9_wave_cap     = NULL;

void arm9_wave_cap_init(void)
{
    arm9_wave_cap = (WaveCapCh *)malloc(sizeof(WaveCapCh) * WAVE_CAP_N);
    if (arm9_wave_cap == NULL) return;
    memset(arm9_wave_cap, 0, sizeof(WaveCapCh) * WAVE_CAP_N);
    DC_FlushRange(arm9_wave_cap, sizeof(WaveCapCh) * WAVE_CAP_N);
    u32 addr = (u32)arm9_wave_cap;
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_WAVE_CAP_PTR, addr & 0x00FFFFFFu));
}

/* Q10 tempo scaler: desired/base BPM. 1024 = 1.0×. */
static mm_word bpm_ratio_q10(u8 desired, u8 base)
{
    if (base == 0) return 1024;
    u32 ratio = ((u32)desired * 1024u) / base;
    if (ratio < 512)  ratio = 512;
    if (ratio > 2048) ratio = 2048;
    return (mm_word)ratio;
}

void serviceUpdate(int8 nudge)
{
    if (arm9_moduleLoaded && arm9_baseBpm > 0) {
        u32 ratio = bpm_ratio_q10(arm9_globalBpm, arm9_baseBpm);
#ifndef TICK_NUDGE
        if      (nudge > 0) ratio = ratio * 104 / 100;
        else if (nudge < 0) ratio = ratio *  96 / 100;
#else
        (void)nudge;
#endif
        mmSetModuleTempo(ratio);
    }
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_SET_BPM, arm9_globalBpm));
}

void serviceCmd(u32 cmd, s32 param)
{
    switch (cmd)
    {
        case CMD_GOTO_HOTCUE:
            /* Stopped: mmPosition works directly. Playing: defer to ARM7
             * which queues it for end of pattern (or jumps immediately
             * with -DHOTCUE_JUMP_IMMEDIATELY). */
            if (!arm9_playing)
                mmPosition((mm_word)(u8)param);
            else
                fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_GOTO_HOTCUE, param));
            break;

        case CMD_SET_TRANSPOSE:
        case CMD_SET_CHANNEL_MUTE:
        case CMD_ROLL_START:
        case CMD_ROLL_STOP:
        case CMD_SET_BPM_LOCK:
        case CMD_SET_LOOPMODE:
        case CMD_NUDGE_TICK:
        case CMD_STUTTER_START:
        case CMD_STUTTER_STOP:
            fifoSendValue32(FIFO_XMX, XMX_MKCMD(cmd, param));
            break;

        default:
            break;
    }
}

void arm9_resetChannelState(void)
{
    arm9_soloMask = 0;
    for (u8 i = 0; i < 32; i++) {
        arm9_channelMute[i]  = 0;
        arm9_preSoloMute[i]  = 0;
    }
}

void arm9_XMXValueHandler(u32 value, void *userdata)
{
    switch (XMX_CMD_TYPE(value))
    {
        case CMD_BEAT_PULSE:
            arm9_beatCounter = 3;
            break;

        case CMD_DBG_STEP:
            arm9_dbgStep = XMX_CMD_PARAM_U(value);
            break;

        case CMD_STATE_W0: {
            if (!arm9_moduleLoaded) break;
            u32 p = XMX_CMD_PARAM_U(value);
            s_arm9_state.position = (p >> 16) & 0xFF;
            s_arm9_state.row      = (p >>  8) & 0xFF;
            s_arm9_state.bpm      =  p        & 0xFF;
            arm9_moduleState = &s_arm9_state;
            break;
        }

        case CMD_STATE_W1: {
            if (!arm9_moduleLoaded) break;
            u32 p = XMX_CMD_PARAM_U(value);
            s_arm9_state.active        = (p >> 16) & 0xFF;
            s_arm9_state.nrows         = (p >>  8) & 0xFF;
            s_arm9_state.module_length =  p        & 0xFF;
            /* Latch BPM on first active state — gated on active==1 to
             * ignore stale values from between mmStop and mmPlayModule. */
            if (arm9_baseBpm == 0 && s_arm9_state.bpm > 0 && s_arm9_state.active) {
                arm9_baseBpm   = s_arm9_state.bpm;
                arm9_globalBpm = s_arm9_state.bpm;
                serviceUpdate(0);
            }
            break;
        }

        default:
            break;
    }
}
