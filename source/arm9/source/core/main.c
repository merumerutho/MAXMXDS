#include <nds.h>
#include <stdio.h>
#include <string.h>
#include <maxmod9.h>

#include "arm9_defines.h"
#include "arm9_fifo.h"
#include "filesystem.h"
#include "play.h"
#include "libXMX.h"
#include "cueScreen.h"
#include "waveScreen.h"
#include "fxScreen.h"
#include "visScreen.h"
#include "screens.h"

/* NitroFS root is "/" (files embedded in .nds).
 * FAT root for native hardware: "/data/" on the SD card. */
#define NITRO_ROOT_PATH  "./"
#define FAT_ROOT_PATH    "./data/"

void drawTitleBanner(void);
void drawTitleStatus(void);

void arm9_VBlankHandler(void) {}

void drawIntro(void)
{
    consoleSelect(&top);
    consoleClear();
    if (sub_bg2 >= 0) bgHide(sub_bg2);
    consoleSelect(&bottom);
    iprintf("\x1b[8;12Hmaxmxds");
    iprintf("\x1b[9;8H{.mas dj player}");
    iprintf("\x1b[12;10H@merumerutho");
    iprintf("\x1b[13;3Hbased on maxmod by @mukunda");
    while (1) {
        scanKeys();
        if (keysDown()) break;
    }
}

/* Draw the ASCII art banner. On first call clears console; subsequent
 * calls just overwrite in place to avoid flicker. */
static bool s_banner_drawn = false;

void drawTitleBanner(void)
{
    consoleSelect(&top);
    if (!s_banner_drawn) {
        consoleClear();
        s_banner_drawn = true;
    }
    iprintf("\x1b[1;4H8b    d8    db    Yb  dP       ");
    iprintf("\x1b[2;4H88b  d88   dPYb    YbdP        ");
    iprintf("\x1b[3;4H88YbdP88  dP__Yb   dPYb        ");
    iprintf("\x1b[4;4H88 YY 88 dP''''Yb dP  Yb       ");
    iprintf("\x1b[5;1H8b    d8 Yb  dP 8888b.  .dP'Y8 ");
    iprintf("\x1b[6;1H88b  d88  YbdP   8I  Yb `Ybo.' ");
    iprintf("\x1b[7;1H88YbdP88  dPYb   8I  dY o.`Y8b ");
    iprintf("\x1b[8;1H88 YY 88 dP  Yb 8888Y'  8bodP' ");
}

/* Redraw everything on the top screen each frame:
 * plasma effect fills BG2, then text overlay redrawn on BG0. */
void drawTitleStatus(void)
{
    /* Animate plasma first (BG2 underneath) */
    topScreen_tick();

    /* Redraw all text on top (consoleClear + reprint) */
    drawTitleBanner();

    if (!arm9_moduleLoaded) {
        iprintf("\x1b[12;4H  load a track to begin  ");
        return;
    }

    u8 pos      = arm9_moduleState ? arm9_moduleState->position     : 0;
    u8 row      = arm9_moduleState ? arm9_moduleState->row          : 0;
    u8 nrows    = arm9_moduleState ? arm9_moduleState->nrows        : 0;
    u8 mod_len  = arm9_moduleState ? arm9_moduleState->module_length: 0;

    if (arm9_beatCounter > 0) arm9_beatCounter--;

    iprintf("\x1b[11;1HBPM: %3d  Tempo: %2d  %s",
            (int)arm9_globalBpm, (int)arm9_globalTempo,
            arm9_beatCounter > 0 ? "*" : " ");
    iprintf("\x1b[13;1HPos: %03d/%03d  Row: %03d/%03d",
            (int)(pos + 1), (int)mod_len, (int)row, (int)nrows);
    iprintf("\x1b[14;1HCue: %03d Loop: %s Roll: %s Stt: %s",
            (int)(arm9_cuePoints[0] + 1),
            arm9_globalLoopMode ? "Y" : "N",
            arm9_rollActive ? "ON" : "--",
            arm9_stutterActive ? "ON" : "--");
    iprintf("\x1b[15;1HTranspose: %+d  BPMLock: %s  ",
            (int)arm9_globalTranspose,
            arm9_bpmLock ? "ON" : "OFF");

    iprintf("\x1b[23;0H%.32s", deckInfo.name);
}

static void redrawBottomScreen(ScreenMode mode)
{
    bgShow(sub_bg2);
    switch (mode) {
        case SCREEN_MODE_CH:  drawWaveScreen(); break;
        case SCREEN_MODE_CUE: drawCueScreen();  break;
        case SCREEN_MODE_FX:  drawFxScreen();    break;
        case SCREEN_MODE_VFX: drawVisScreen();   break;
    }
}

int main(int argc, char **argv)
{
    touchPosition touchPos;
    char folderPath[255];
    ScreenMode screenMode = SCREEN_MODE_CH;

    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);

    /* Install FIFO_XMX value32 handler (state arrives inline, no address ptr) */
    fifoSetValue32Handler(FIFO_XMX, arm9_XMXValueHandler, NULL);

    /* Init consoles */
    consoleInit(&top,    0, BgType_Text4bpp, BgSize_T_256x256, 2, 0, true,  true);
    consoleInit(&bottom, 0, BgType_Text4bpp, BgSize_T_256x256, 2, 0, false, true);

    /* Bitmap layers for both screens */
    initWaveBg();     /* sub screen BG2 */
    initTopBg();      /* main screen BG2 */

    drawIntro();

    /* Turn on master sound */
    fifoSendValue32(FIFO_SOUND, SOUND_MASTER_ENABLE);

    /* Init filesystem */
    XMX_FileSystem_init();
    strcpy(folderPath, XMX_FileSystem_isFat() ? FAT_ROOT_PATH : NITRO_ROOT_PATH);

    /* Init MaxMod on ARM9 (ARM7 already called mmInstall in its main).
     * We use an empty bank: all playback goes through mmPlayModule(). */
    /* No soundbank modules — playback uses mmPlayModule on ARM7 directly.
     * mm_bank still needed as MaxMod requires a non-NULL mem_bank. */
    static mm_word mm_bank[1];
    mm_ds_system sys = {
        .mod_count    = 0,
        .samp_count   = 0,
        .mem_bank     = mm_bank,
        .fifo_channel = FIFO_MAXMOD,
    };
    mmInit(&sys);
    mmSelectMode(MM_MODE_C);

    /* Allocate oscilloscope capture buffer and tell ARM7 where it is */
    arm9_wave_cap_init();

    /* Initial bottom screen */
    bgShow(sub_bg2);
    drawWaveScreen();

    bool inputTouching = false;
    bool y_dirty       = false;  /* prevents hotcue jump if Y was part of a combo */

    drawTitleBanner();

    while (TRUE)
    {
        bool forceUpdate = false;
        bool bAnyUsrInput = false;
        int8 nudge = 0;

        drawTitleStatus();
        scanKeys();
        u32 keys_down = keysDown();
        u32 keys_held = keysHeld();
        u32 keys_up   = keysUp();

        if (screenMode == SCREEN_MODE_CUE)
            cueScreen_tick();
        else if (screenMode == SCREEN_MODE_FX)
            fxScreen_tick();
        else if (screenMode == SCREEN_MODE_VFX)
            visScreen_tick();
        else
            waveScreen_tick();

        /* BPM lock: re-apply tempo scaler every frame */
        if (arm9_bpmLock && arm9_moduleLoaded)
            forceUpdate = true;

        /* Touch handling */
        if (keys_held & KEY_TOUCH) {
            if (!inputTouching) {
                touchRead(&touchPos);
                float y_norm = (touchPos.rawy - Y_MIN) * Y_NORM;
                if (y_norm < 1.0f / 24) {
                    float x_norm = (touchPos.rawx - X_MIN) * X_NORM;
                    ScreenMode newMode;
                    if (x_norm < 1.0f / 3)
                        newMode = SCREEN_MODE_CH;
                    else if (x_norm < 2.0f / 3)
                        newMode = SCREEN_MODE_CUE;
                    else
                        newMode = SCREEN_MODE_VFX;
                    if (newMode != screenMode) {
                        screenMode = newMode;
                        redrawBottomScreen(screenMode);
                    }
                } else if (screenMode == SCREEN_MODE_CH) {
                    handleWaveTouch(&touchPos);
                } else if (screenMode == SCREEN_MODE_CUE) {
                    u8 song_pos = (arm9_moduleState != NULL)
                                  ? arm9_moduleState->position : 0;
                    handleCueTouch(&touchPos, false, song_pos);
                } else if (screenMode == SCREEN_MODE_VFX) {
                    handleVisTouch(&touchPos);
                } else {
                    handleFxTouch(&touchPos, false);
                }
                inputTouching = true;
            } else if (screenMode == SCREEN_MODE_VFX) {
                /* VFX sliders: continuous tracking while held */
                touchRead(&touchPos);
                float y_norm = (touchPos.rawy - Y_MIN) * Y_NORM;
                if (y_norm >= 1.0f / 24)
                    handleVisTouch(&touchPos);
            } else if (screenMode == SCREEN_MODE_FX) {
                touchRead(&touchPos);
                float y_norm = (touchPos.rawy - Y_MIN) * Y_NORM;
                if (y_norm >= 1.0f / 24)
                    handleFxTouch(&touchPos, true);
            }
        } else {
            if (inputTouching && screenMode == SCREEN_MODE_FX)
                handleFxRelease();
            inputTouching = false;
        }

        /* Commands requiring a loaded module */
        if (arm9_moduleLoaded) {
            /* PLAY / STOP */
            if (keys_down & KEY_A)
                play_stop();

            /* TRANSPOSE DOWN */
            if (keys_down & KEY_L) {
                arm9_globalTranspose--;
                serviceCmd(CMD_SET_TRANSPOSE, arm9_globalTranspose);
            }

            /* TRANSPOSE UP */
            if (keys_down & KEY_R) {
                arm9_globalTranspose++;
                serviceCmd(CMD_SET_TRANSPOSE, arm9_globalTranspose);
            }

            /* SET HOT CUE (cue[0]) */
            if (keys_down & KEY_B) {
                if (arm9_moduleState != NULL)
                    arm9_cuePoints[0] = arm9_moduleState->position;
                forceUpdate = true;
            }

            /* CUE MOVE */
            if (keys_held & KEY_B) {
                u8 mod_len = arm9_moduleState ? arm9_moduleState->module_length : 0;
                if ((keys_down & KEY_LEFT) && arm9_cuePoints[0] > 0) {
                    arm9_cuePoints[0]--;
                    forceUpdate = true;
                }
                if ((keys_down & KEY_RIGHT) && mod_len > 0 &&
                    arm9_cuePoints[0] < mod_len - 1) {
                    arm9_cuePoints[0]++;
                    forceUpdate = true;
                }
            }

            /* LOOP MODE */
            if (keys_down & KEY_X) {
                arm9_globalLoopMode = !arm9_globalLoopMode;
                serviceCmd(CMD_SET_LOOPMODE, arm9_globalLoopMode);
                forceUpdate = true;
            }

            /* BPM LOCK */
            if ((keys_down & KEY_START) && !(keys_held & KEY_SELECT)) {
                arm9_bpmLock = !arm9_bpmLock;
                serviceCmd(CMD_SET_BPM_LOCK, arm9_bpmLock);
            }

            /* GOTO HOT CUE — fires on release, only if no other key was pressed
             * while Y was held (e.g. Y+LEFT / Y+RIGHT combos are excluded). */
            if (keys_down & KEY_Y)
                y_dirty = false;
            if ((keys_held & KEY_Y) && (keys_down & (u32)~KEY_Y))
                y_dirty = true;
            if ((keys_up & KEY_Y) && !y_dirty)
                serviceCmd(CMD_GOTO_HOTCUE, arm9_cuePoints[0]);

            /* BEAT STUTTER (SELECT+DOWN) — uses same division as roll */
            if ((keys_held & KEY_SELECT) && (keys_down & KEY_DOWN)) {
                if (!arm9_stutterActive) {
                    arm9_stutterActive = 1;
                    serviceCmd(CMD_STUTTER_START, arm9_rollN);
                } else {
                    serviceCmd(CMD_STUTTER_STOP, 0);
                    arm9_stutterActive = 0;
                }
            }

            /* LOOP ROLL */
            if (keys_held & KEY_SELECT) {
                if (keys_down & KEY_UP) {
                    if (!arm9_rollActive) {
                        arm9_rollActive = 1;
                        serviceCmd(CMD_ROLL_START, arm9_rollN);
                    } else {
                        serviceCmd(CMD_ROLL_STOP, 0);
                        arm9_rollActive = 0;
                    }
                }
                if (keys_down & KEY_RIGHT) {
                    if (arm9_rollN < 128) arm9_rollN *= 2;
#ifndef FREE_ROLLING_LOOP
                    if (arm9_rollN < 4) arm9_rollN = 4;
#endif
                    if (arm9_rollActive) serviceCmd(CMD_ROLL_START, arm9_rollN);
                    if (arm9_stutterActive) serviceCmd(CMD_STUTTER_START, arm9_rollN);
                    forceUpdate = true;
                }
                if (keys_down & KEY_LEFT) {
#ifdef FREE_ROLLING_LOOP
                    if (arm9_rollN > 1) arm9_rollN /= 2;
#else
                    if (arm9_rollN > 4) arm9_rollN /= 2;
#endif
                    if (arm9_rollActive) serviceCmd(CMD_ROLL_START, arm9_rollN);
                    if (arm9_stutterActive) serviceCmd(CMD_STUTTER_START, arm9_rollN);
                    forceUpdate = true;
                }
            }

            /* BPM UP / DOWN (not while SELECT held for roll) */
            if ((keys_down & KEY_UP) && !(keys_held & KEY_SELECT)) {
                if (arm9_globalBpm < 255) arm9_globalBpm++;
                forceUpdate = true;
            }
            if ((keys_down & KEY_DOWN) && !(keys_held & KEY_SELECT)) {
                if (arm9_globalBpm > 1) arm9_globalBpm--;
                forceUpdate = true;
            }


#ifndef TICK_NUDGE
            /* NUDGE: ±4% tempo bend while key held (default) */
            if ((keys_held & KEY_RIGHT) && !(keys_held & KEY_B) && !(keys_held & KEY_SELECT))
                nudge = 1;
            else if ((keys_held & KEY_LEFT) && !(keys_held & KEY_B) && !(keys_held & KEY_SELECT))
                nudge = -1;
#else
            /* NUDGE: advance or retard the module tick by 1 (once per press) */
            if ((keys_down & KEY_RIGHT) && !(keys_held & KEY_B) && !(keys_held & KEY_SELECT))
                serviceCmd(CMD_NUDGE_TICK, +1);
            else if ((keys_down & KEY_LEFT) && !(keys_held & KEY_B) && !(keys_held & KEY_SELECT))
                serviceCmd(CMD_NUDGE_TICK, -1);
#endif

            bAnyUsrInput = (keys_down != 0);

            if ((arm9_playing && bAnyUsrInput) || forceUpdate || nudge != 0)
                serviceUpdate(nudge);
        }

        /* FILE BROWSER (SELECT + START) */
        if ((keys_held & KEY_SELECT) && (keys_down & KEY_START)) {
            XMX_FileSystem_selectModule(folderPath);
            s_banner_drawn = false;  /* force full redraw after file browser */
            drawTitleBanner();
            redrawBottomScreen(screenMode);
            if (arm9_moduleLoaded)
                serviceUpdate(0);
        }

        swiWaitForVBlank();
    }

    return 0;
}
