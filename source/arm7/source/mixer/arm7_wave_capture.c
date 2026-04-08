/*
 * arm7_wave_capture.c
 *
 * Each VBlank, snapshots the mixer channel state (sample descriptor offset +
 * playback position) into shared main RAM.  ARM9 uses these to read sample
 * PCM data directly and render the oscilloscope.
 *
 * This is intentionally minimal — ARM7 only copies two words per channel.
 * All sample-data access and rendering happens on ARM9.
 */

#include "arm7_wave_capture.h"
#include "arm7_maxmod_bridge.h"

/* Shared buffer: ARM9 allocates this in main RAM and sends the address. */
static WaveCapCh *s_buf = NULL;

void wave_capture_set_buf(u32 arm9_addr)
{
    s_buf = (WaveCapCh *)arm9_addr;
}

void wave_capture_tick(void)
{
    if (s_buf == NULL) return;
    if (mm_pchannels == NULL) return;

    for (u8 ch = 0; ch < WAVE_CAP_N; ch++) {
        u8 mix_idx = mm_pchannels[ch].alloc;

        if (mix_idx < 32 && mm_mix_channels[mix_idx].samp != 0) {
            s_buf[ch].samp = mm_mix_channels[mix_idx].samp;
            s_buf[ch].read = mm_mix_channels[mix_idx].read;
        } else {
            s_buf[ch].samp = 0;
            s_buf[ch].read = 0;
        }
    }
    /* No DC_FlushRange: ARM7 has no data cache, writes go straight to RAM */
}
