#include <nds.h>
#include <maxmod7.h>

#include "arm7_fifo.h"
#include "arm7_maxmod_bridge.h"
#include "arm7_wave_capture.h"

volatile bool exitflag = false;

static void VblankHandler(void)
{
    /* MaxMod installs mmFrame on Timer0 IRQ (IRQ_TIMER0) via mmInit7/mmInstall.
     * VBlank and Timer0 are independent IRQs — do NOT call mmFrame() here. */
}

static void VcountHandler(void)
{
    inputGetAndSend();
}

static void powerButtonCB(void)
{
    exitflag = true;
}

int main(void)
{
    /* Clear sound registers */
    dmaFillWords(0, (void*)0x04000400, 0x100);

    irqInit();
    fifoInit();

    SetYtrigger(80);

    installSoundFIFO();

    /* Install MaxMod on ARM7 — MaxMod handles its own FIFO channel internally */
    mmInstall(FIFO_MAXMOD);

    /* Install FIFO_XMX handlers: value32 for DJ commands, address for MAS play */
    fifoSetValue32Handler(FIFO_XMX,   arm7_XMXValueHandler, NULL);
    fifoSetAddressHandler(FIFO_XMX,   arm7_playMASHandler,  NULL);

    installSystemFIFO();

    irqSet(IRQ_VCOUNT, VcountHandler);
    irqSet(IRQ_VBLANK, VblankHandler);
    irqEnable(IRQ_VBLANK | IRQ_VCOUNT);

    setPowerButtonCB(powerButtonCB);

    /* Main loop: process deferred MaxMod commands once per VBlank.
     * MaxMod functions must NOT be called from IRQ context (timer race).
     *
     * Stop/play is processed FIRST so that sendState and wave_capture never
     * read from MAS buffer pointers that ARM9 has already freed. */
    while (!exitflag) {
        swiWaitForVBlank();

        /* Hook our custom mmFrame wrapper once MaxMod is initialized */
        mm_bridge_hookFrame();

        if (arm7_pendingStop) {
            arm7_pendingStop = false;
            mmStop();
            if (arm7_pendingUnload) {
                arm7_pendingUnload = false;
                mm_bridge_resetDJState();
            }
        }

        if (arm7_pendingMASAddr) {
            u32 addr = arm7_pendingMASAddr;
            arm7_pendingMASAddr = 0;
            mmStop();
            arm7_dbgStep(DBG_STEP_PLAY_CALLED);
            mmPlayMAS(addr, MM_PLAY_LOOP, 0);
            /* Send active (bits 15:8) and BPM (bits 7:0) packed with tag 3 */
            {
                u32 ab = (3u << 16)
                       | ((u32)mmLayerMain.isplaying << 8)
                       | (u32)mmLayerMain.bpm;
                fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_DBG_STEP, ab));
            }
        }

        mm_bridge_sendState();              /* safe to call fifoSendAddress outside IRQ */
        wave_capture_tick();                /* sample one value per channel into shared RAM */
        arm7_beat_tick(mmLayerMain.isplaying); /* fire CMD_BEAT_PULSE at current BPM rate */
    }

    return 0;
}
