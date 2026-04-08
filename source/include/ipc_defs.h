/*
 * ipc_defs.h — Shared types and command constants for ARM7 ↔ ARM9 IPC.
 * Included by both processors. No ARM-specific code.
 */

#ifndef IPC_DEFS_H
#define IPC_DEFS_H

#include <nds.h>

#define PACKED __attribute__((packed))

#define FIFO_XMX  (FIFO_USER_08)

/* Per-channel oscilloscope state. ARM7 writes each VBlank; ARM9 reads. */
#define WAVE_CAP_N   32

typedef struct {
    volatile u32 samp;   /* sample descriptor offset (24-bit EWRAM, 0=inactive) */
    volatile u32 read;   /* 22.10 fixed-point playback position                */
} PACKED WaveCapCh;

/* Module state: ARM7 writes, ARM9 reads after DC_InvalidateRange. */
typedef struct {
    u8  position;       /* song order index       */
    u8  row;            /* row within pattern     */
    u8  bpm;            /* module-native BPM      */
    u8  active;         /* 1 = playing            */
    u8  nrows;          /* rows in current pattern */
    u8  module_length;  /* total order count      */
    u8  speed;          /* ticks per row          */
    u8  _pad;
} PACKED XMXModuleState_S;

/* Value32 command encoding: bits[31:24]=type, bits[23:0]=param */
#define XMX_MKCMD(cmd, param)   ((((u32)(cmd)) << 24) | ((u32)(s32)(param) & 0x00FFFFFFu))
#define XMX_CMD_TYPE(val)       (((val) >> 24) & 0xFFu)
#define XMX_CMD_PARAM_U(val)    ((val) & 0x00FFFFFFu)
#define XMX_CMD_PARAM_S(val)    ((s32)(((val) << 8) >> 8))

/* ARM9 → ARM7 */
#define CMD_SET_TRANSPOSE       1
#define CMD_SET_LOOPMODE        2
#define CMD_GOTO_HOTCUE         3
#define CMD_SET_CHANNEL_MUTE    4
#define CMD_SET_BPM_LOCK        5
#define CMD_ROLL_START          7
#define CMD_ROLL_STOP           8
#define CMD_SET_FILTER          9
#define CMD_STOP_MODULE         10
#define CMD_WAVE_CAP_PTR        15
#define CMD_NUDGE_TICK          16
#define CMD_SET_BPM             17
#define CMD_UNLOAD_MODULE       18
#define CMD_STUTTER_START       19
#define CMD_STUTTER_STOP        20

/* ARM7 → ARM9 */
#define CMD_BEAT_PULSE          6
#define CMD_DBG_STEP            11
#define CMD_STATE_W0            12
#define CMD_STATE_W1            13

#define DBG_STEP_ADDR_RCVD      1
#define DBG_STEP_PLAY_CALLED    2
#define DBG_STEP_PLAY_DONE      3
#define DBG_STEP_ACTIVE         4

/* mmutil -d prepends 8 bytes before the raw MAS data */
#define MAS_NDS_HEADER_SIZE     8

#endif /* IPC_DEFS_H */
