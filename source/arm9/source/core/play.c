#include "play.h"
#include "libXMX.h"
#include "arm9_fifo.h"
#include <maxmod9.h>

/*
 * Toggle playback.  If playing, stop.  If stopped, restart from the hot cue.
 */
void play_stop(void)
{
    if (!arm9_moduleLoaded || deckInfo.masBuffer == NULL)
        return;

    if (arm9_playing) {
        fifoSendValue32(FIFO_XMX, XMX_MKCMD(CMD_STOP_MODULE, 0));
        arm9_playing = false;
    } else {
        /* Re-send the MAS buffer address — ARM7 restarts mmPlayModule */
        fifoSendAddress(FIFO_XMX, deckInfo.masBuffer);
        arm9_playing = true;
        if (arm9_cuePoints[0] > 0)
            mmPosition((mm_word)arm9_cuePoints[0]);
    }
}
