#include "libXMX.h"
#include "arm9_fifo.h"
#include "arm9_defines.h"
#include <nds.h>
#include <stdlib.h>

XMX_DeckInfo deckInfo = {
    .masBuffer = NULL,
    .masSize   = 0,
    .name      = "",
};

void XMX_UnloadMAS(void)
{
    /* Tell ARM7 to stop playback AND reset all DJ state.
     * ARM7 processes this in its main loop (once per VBlank). */
    fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_UNLOAD_MODULE, 0));

    /* Reset ARM9 tracking state BEFORE waiting, so the FIFO state handler
     * (arm9_XMXValueHandler) ignores any stale updates from ARM7. */
    arm9_moduleLoaded = false;
    arm9_playing      = false;
    arm9_moduleState  = NULL;
    arm9_baseBpm      = 0;
    arm9_globalBpm    = DEFAULT_BPM;

    /* Reset ARM9 DJ controls */
    arm9_globalTranspose = 0;
    arm9_globalLoopMode  = 0;
    arm9_rollActive      = 0;
    arm9_rollN           = 4;
    arm9_bpmLock         = 0;

    /* Wait for ARM7 to process mmStop — ARM7's Timer0 ISR (mmFrame) reads
     * pattern/sample data from the MAS buffer, so we must not free it until
     * ARM7 has called mmStop and cleared all channel pointers.
     * 2 VBlanks guarantees ARM7's main loop has executed at least once. */
    swiWaitForVBlank();
    swiWaitForVBlank();

    if (deckInfo.masBuffer != NULL) {
        free(deckInfo.masBuffer);
        deckInfo.masBuffer = NULL;
    }
    deckInfo.masSize = 0;
    deckInfo.name[0] = '\0';
}
