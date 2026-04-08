#include "arm7_fifo.h"
#include "arm7_maxmod_bridge.h"
#include "arm7_wave_capture.h"
#include <maxmod7.h>
#include <nds.h>

void arm7_dbgStep(u32 step)
{
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_DBG_STEP, step));
}

volatile u32  arm7_pendingMASAddr = 0;
volatile bool arm7_pendingStop    = false;
volatile bool arm7_pendingUnload  = false;

/* --- Beat pulse: fires CMD_BEAT_PULSE at BPM-synced intervals --- */

static u8  s_dj_bpm     = 125;
static u32 s_beat_frame = 0;

void arm7_beat_set_bpm(u8 bpm)
{
    if (bpm == 0) return;
    s_dj_bpm     = bpm;
    s_beat_frame = 0;
}

/* 60fps × 60s = 3600 frames/min → frames_per_beat = 3600/bpm */
void arm7_beat_tick(bool playing)
{
    if (!playing) {
        s_beat_frame = 0;
        return;
    }
    s_beat_frame++;
    u32 frames_per_beat = 3600u / (u32)s_dj_bpm;
    if (s_beat_frame >= frames_per_beat) {
        s_beat_frame = 0;
        arm7_sendBeatPulse(0);
    }
}

/* Semitone → Q10 pitch scaler: lut[i] = round(1024 * 2^((i-12)/12)), ±12 range */
static const u16 s_semitone_lut[25] = {
     512, 542, 575, 609, 645, 683, 724, 767,
     813, 861, 912, 966,1024,1085,1149,1218,
    1290,1367,1448,1534,1625,1722,1825,1933,2048
};

static mm_word semitone_to_pitch_q10(s8 semitones)
{
    s8 clamped = semitones;
    if (clamped < -12) clamped = -12;
    if (clamped >  12) clamped =  12;
    return (mm_word)s_semitone_lut[clamped + 12];
}

/* --- FIFO command dispatch --- */

void arm7_XMXValueHandler(u32 value, void *userdata)
{
    u8  cmd   = XMX_CMD_TYPE(value);
    u32 param = XMX_CMD_PARAM_U(value);

    switch (cmd)
    {
        case CMD_SET_TRANSPOSE:
        {
            s8 semitones = (s8)XMX_CMD_PARAM_S(value);
            mmSetModulePitch(semitone_to_pitch_q10(semitones));
            break;
        }

        case CMD_GOTO_HOTCUE:
#ifdef HOTCUE_JUMP_IMMEDIATELY
            mm_bridge_jumpTo((u8)param, 0);
#else
            mm_bridge_queueHotcue((u8)param);
#endif
            break;

        case CMD_SET_CHANNEL_MUTE:
        {
            u8 channel = (u8)((param >> 8) & 0x1Fu);
            u8 mute    = (u8)(param & 0x01u);
            mm_bridge_setMute(channel, mute);
            break;
        }

        case CMD_ROLL_START:    mm_bridge_startLoop((u8)param);    break;
        case CMD_ROLL_STOP:     mm_bridge_stopLoop();              break;
        case CMD_SET_LOOPMODE:  mm_bridge_setLoopMode((u8)param);  break;
        case CMD_STUTTER_START: mm_bridge_startStutter((u8)param); break;
        case CMD_STUTTER_STOP:  mm_bridge_stopStutter();           break;

        case CMD_SET_BPM_LOCK:
            /* Managed on ARM9 via mmSetModuleTempo — ARM7 is a no-op */
            break;

        case CMD_STOP_MODULE:
            arm7_pendingStop = true;
            break;

        case CMD_UNLOAD_MODULE:
            arm7_pendingStop   = true;
            arm7_pendingUnload = true;
            break;

        case CMD_WAVE_CAP_PTR:
            wave_capture_set_buf(0x02000000u | (param & 0x00FFFFFFu));
            break;

        case CMD_NUDGE_TICK:
            mm_bridge_nudgeTick((s8)XMX_CMD_PARAM_S(value));
            break;

        case CMD_SET_BPM:
            arm7_beat_set_bpm((u8)param);
            break;

        default:
            break;
    }
}

/* Queued for main loop — mmPlayModule is not IRQ-safe */
void arm7_playMASHandler(void *addr, void *userdata)
{
    (void)userdata;
    if (addr == NULL) return;
    arm7_dbgStep(DBG_STEP_ADDR_RCVD);
    arm7_pendingMASAddr = (u32)addr;
}

void arm7_sendBeatPulse(u8 row)
{
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_BEAT_PULSE, row));
}
