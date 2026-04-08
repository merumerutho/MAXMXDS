/*
 * devkitpro_compat.h
 *
 * Compatibility shim: maps BlocksDS libnds symbol names used by maxmod
 * to their devkitPro libnds equivalents.
 *
 * Injected via -include from maxmod.mak so that no maxmod source
 * files need editing.  This file lives in MAXMXDS, not in the library.
 */
#ifndef DEVKITPRO_COMPAT_H
#define DEVKITPRO_COMPAT_H

#ifdef __NDS__

/* ---------- Timer ---------- */

/* BlocksDS defines LIBNDS_DEFAULT_TIMER_MUSIC; devkitPro does not.
 * The original ASM maxmod hard-codes Timer 0 (0x4000100). */
#ifndef LIBNDS_DEFAULT_TIMER_MUSIC
#define LIBNDS_DEFAULT_TIMER_MUSIC  0
#endif

/* ---------- Master sound control register ---------- */

/* devkitPro: SOUND_ENABLE, SOUND_VOL(n)
 * BlocksDS: SOUNDCNT_ENABLE, SOUNDCNT_VOL(n) */
#ifndef SOUNDCNT_ENABLE
#define SOUNDCNT_ENABLE     SOUND_ENABLE
#endif
#ifndef SOUNDCNT_VOL
#define SOUNDCNT_VOL(n)     SOUND_VOL(n)
#endif

/* Master sound control output routing (REG_SOUNDCNT bits 8-11).
 * devkitPro does not define named constants for these. */
#ifndef SOUNDCNT_LEFT_OUTPUT_MASK
#define SOUNDCNT_LEFT_OUTPUT_MASK       (3 << 8)
#define SOUNDCNT_LEFT_OUTPUT_MIXER      (0 << 8)
#define SOUNDCNT_LEFT_OUTPUT_CH1        (1 << 8)
#define SOUNDCNT_LEFT_OUTPUT_CH3        (2 << 8)
#define SOUNDCNT_LEFT_OUTPUT_CH1_CH3    (3 << 8)
#endif
#ifndef SOUNDCNT_RIGHT_OUTPUT_MASK
#define SOUNDCNT_RIGHT_OUTPUT_MASK      (3 << 10)
#define SOUNDCNT_RIGHT_OUTPUT_MIXER     (0 << 10)
#define SOUNDCNT_RIGHT_OUTPUT_CH1       (1 << 10)
#define SOUNDCNT_RIGHT_OUTPUT_CH3       (2 << 10)
#define SOUNDCNT_RIGHT_OUTPUT_CH1_CH3   (3 << 10)
#endif

/* ---------- Per-channel sound control register ---------- */

/* devkitPro: SCHANNEL_CR(n)
 * BlocksDS: REG_SOUNDXCNT(n) — same address, different name. */
#ifndef REG_SOUNDXCNT
#define REG_SOUNDXCNT(n)    SCHANNEL_CR(n)
#endif

/* Per-channel control bit definitions.
 * devkitPro names: SCHANNEL_ENABLE, SOUND_REPEAT, SOUND_FORMAT_8BIT, etc.
 * BlocksDS names:  SOUNDXCNT_ENABLE, SOUNDXCNT_REPEAT, SOUNDXCNT_FORMAT_8BIT, etc. */
#ifndef SOUNDXCNT_ENABLE
#define SOUNDXCNT_ENABLE        SCHANNEL_ENABLE
#endif
#ifndef SOUNDXCNT_REPEAT
#define SOUNDXCNT_REPEAT        SOUND_REPEAT
#endif
#ifndef SOUNDXCNT_FORMAT_8BIT
#define SOUNDXCNT_FORMAT_8BIT   SOUND_FORMAT_8BIT
#endif
#ifndef SOUNDXCNT_FORMAT_16BIT
#define SOUNDXCNT_FORMAT_16BIT  SOUND_FORMAT_16BIT
#endif

/* Volume multiplier and panning — BlocksDS uses these for per-channel CR. */
#ifndef SOUNDXCNT_VOL_MUL
#define SOUNDXCNT_VOL_MUL(n)    (n)           /* bits 0-6 of SCHANNEL_CR */
#endif
#ifndef SOUNDXCNT_PAN
#define SOUNDXCNT_PAN(n)        ((n) << 16)   /* bits 16-22 of SCHANNEL_CR */
#endif

/* ---------- Sound capture control bits ---------- */

#ifndef SNDCAPCNT_FORMAT_8BIT
#define SNDCAPCNT_FORMAT_8BIT   (1 << 3)
#endif
#ifndef SNDCAPCNT_START_BUSY
#define SNDCAPCNT_START_BUSY    (1 << 7)
#endif

/* ---------- DMA registers ---------- */

#ifndef REG_DMA1_DEST
#define REG_DMA1_DEST   DMA1_DEST
#endif

/* ---------- Per-channel sound sub-registers ---------- */

/* BlocksDS provides individual register accessors; devkitPro uses SCHANNEL_*. */
#ifndef REG_SOUNDXSAD
#define REG_SOUNDXSAD(n)    SCHANNEL_SOURCE(n)
#endif
#ifndef REG_SOUNDXTMR
#define REG_SOUNDXTMR(n)    SCHANNEL_TIMER(n)
#endif
#ifndef REG_SOUNDXPNT
#define REG_SOUNDXPNT(n)    SCHANNEL_REPEAT_POINT(n)
#endif
#ifndef REG_SOUNDXLEN
#define REG_SOUNDXLEN(n)    SCHANNEL_LENGTH(n)
#endif
#ifndef REG_SOUNDXVOL
#define REG_SOUNDXVOL(n)    SCHANNEL_VOL(n)
#endif
#ifndef REG_SOUNDXPAN
#define REG_SOUNDXPAN(n)    SCHANNEL_PAN(n)
#endif

/* ---------- fifoWaitAddressAsync (BlocksDS only) ---------- */

/* BlocksDS provides fifoWaitAddressAsync(); devkitPro does not.
 * Spin until an address is available, matching fifoWaitValue32 pattern. */
#include <nds/fifocommon.h>
#include <nds/interrupts.h>
#ifndef fifoWaitAddressAsync
static inline __attribute__((unused))
void fifoWaitAddressAsync(int channel)
{
    while (!fifoCheckAddress(channel))
        swiIntrWait(1, IRQ_FIFO_NOT_EMPTY);
}
#endif

/* ---------- ARM_CODE attribute ---------- */

/* BlocksDS uses ARM_CODE to force ARM (non-Thumb) code generation.
 * devkitPro equivalent is __attribute__((target("arm"))). */
#ifndef ARM_CODE
#define ARM_CODE __attribute__((target("arm")))
#endif

/* ---------- CPSR helpers (ARM7 only) ---------- */

/* BlocksDS libnds provides getCPSR/setCPSR and CPSR_FLAG_IRQ_DIS.
 * devkitPro exposes getCPSR on ARM9 (nds/arm9/exceptions.h) but not ARM7. */
#ifndef CPSR_FLAG_IRQ_DIS
#define CPSR_FLAG_IRQ_DIS   (1 << 7)  /* I-bit in CPSR */
#endif

#ifdef ARM7

#ifndef getCPSR
__attribute__((target("arm"), noinline, unused))
static unsigned int getCPSR(void)
{
    unsigned int cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr;
}
#endif

#ifndef setCPSR
__attribute__((target("arm"), noinline, unused))
static void setCPSR(unsigned int val)
{
    __asm__ volatile ("msr cpsr, %0" :: "r"(val));
}
#endif

#endif /* ARM7 */

/* ---------- libndsCrash stub ---------- */

/* BlocksDS provides libndsCrash() for fatal assertions.
 * devkitPro does not.  Provide a minimal stub that halts. */
#ifndef libndsCrash
static inline __attribute__((noreturn, unused))
void libndsCrash(const char *msg)
{
    (void)msg;
    for (;;) ;
}
#endif

#endif /* __NDS__ */
#endif /* DEVKITPRO_COMPAT_H */
