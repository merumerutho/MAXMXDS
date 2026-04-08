#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fat.h>
#include "filesystem.h"
#include "libXMX.h"
#include "arm9_fifo.h"
#include "screens.h"
#include <maxmod9.h>

void XMX_FileSystem_displayHeader(void)
{
    /* Hide BG2 bitmap so the file list is readable */
    if (sub_bg2 >= 0) bgHide(sub_bg2);

    consoleSelect(&bottom);
    consoleClear();
    iprintf("-------------------------------\n");
    iprintf(" XMXDS - File browser\n");
    iprintf("-------------------------------\n");
}

u16 getFileCount(DIR *folder)
{
    u16 counter = 0;
    rewinddir(folder);
    for (counter = 0; counter < 0xFFFF; counter++)
        if (readdir(folder) == NULL) break;
    rewinddir(folder);
    return counter;
}

void my_snprintf(char *dest, char *src, u8 len, bool fillWSpace)
{
    u8 c;
    for (c = 0; c < len; c++) {
        if (src[c] == '\0') break;
        dest[c] = src[c];
    }
    if (c == len) {
        dest[len - 2] = '~';
        dest[len - 1] = '\0';
    } else if (fillWSpace) {
        for (; c < len; c++) {
            if (c == len - 1) dest[c] = '\0';
            else              dest[c] = ' ';
        }
    } else {
        dest[c] = '\0';
    }
}

void listFolderOnPosition(DIR *folder, u16 pPosition, u16 fileCount)
{
    struct dirent *dirContent;
    u8  ff = 0;
    char filename[20] = "";

    consoleSelect(&bottom);

    seekdir(folder, (u32)(pPosition / ENTRIES_PER_SCREEN) * ENTRIES_PER_SCREEN);
    dirContent = readdir(folder);

    while (dirContent && ff < ENTRIES_PER_SCREEN) {
        char p = (ff == (pPosition % ENTRIES_PER_SCREEN)) ? '>' : ' ';
        char *d = (dirContent->d_type == TYPE_FOLDER) ? "dir" : "   ";

        my_snprintf(filename, dirContent->d_name, 20, TRUE);
        iprintf("\x1b[%d;%dH%c %s %s \n", ff + 4, 0, p, d, filename);
        dirContent = readdir(folder);
        ff++;
    }
    while (ff < ENTRIES_PER_SCREEN) {
        iprintf("\x1b[%d;%dH  %s \n", ff + 4, 0, "                               ");
        ff++;
    }
    rewinddir(folder);

    consoleSelect(&bottom);
    iprintf("\x1b[21;0H-------------------------------");
    iprintf("\x1b[22;0H%d/%d          ", pPosition + 1, fileCount);
    iprintf("\x1b[23;0H-------------------------------");
}

struct dirent *getSelection(DIR *folder, u32 position)
{
    seekdir(folder, position);
    return readdir(folder);
}

void insertSlashAtEnd(char *path)
{
    for (u8 i = 1; i < 254; i++) {
        if (path[i] == '\0' && path[i - 1] != '/') {
            path[i]     = '/';
            path[i + 1] = '\0';
            break;
        }
    }
}

void navigateToFolder(DIR **folder, char *path, char *folderName)
{
    insertSlashAtEnd(path);
    strcat(path, folderName);
    closedir(*folder);
    *folder = opendir(path);
}

void navigateBackwards(DIR **folder, char *path)
{
    closedir(*folder);
    u8   lastSlash = 0;
    bool found     = false;
    for (u8 i = 1; i < 255 && path[i] != '\0'; i++)
        if (path[i] == '/') { lastSlash = i; found = true; }
    if (found) path[lastSlash] = '\0';
    else       strcpy(path, ".");
    *folder = opendir(path);
}

/* Accept files ending in .mas (case-insensitive last 4 chars) */
static bool isMAS(const char *filename)
{
    int len = strlen(filename);
    if (len < 4) return false;
    const char *ext = filename + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'm' || ext[1] == 'M') &&
            (ext[2] == 'a' || ext[2] == 'A') &&
            (ext[3] == 's' || ext[3] == 'S'));
}

void composeFileName(char *filepath, char *folder, char *filename)
{
    strcpy(filepath, folder);
    insertSlashAtEnd(filepath);
    strcat(filepath, filename);
    consoleSelect(&bottom);
    iprintf("%s\n", filepath);
}

/* -------------------------------------------------------------------------
 * XMX_FileSystem_loadMAS
 *
 * Read the .mas file at 'filepath' into a malloc'd buffer, flush the ARM9
 * cache so ARM7 can read it, then call mmPlayModule.
 * Returns 0 on success, non-zero on error.
 * ------------------------------------------------------------------------- */
static int loadMAS(const char *filepath, const char *displayName)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) { iprintf("Cannot open file!\n"); return 1; }

    fseek(f, 0L, SEEK_END);
    u32 size = (u32)ftell(f);
    rewind(f);

    if (size == 0) { fclose(f); iprintf("Empty file!\n"); return 2; }

    void *buf = malloc(size);
    if (!buf) { fclose(f); iprintf("Out of memory!\n"); return 3; }

    if (fread(buf, 1, size, f) != size) {
        fclose(f); free(buf); iprintf("Read error!\n"); return 4;
    }
    fclose(f);

    /* Flush ARM9 data cache so ARM7 sees the module in main RAM */
    DC_FlushRange(buf, size);

    /* Store in deck info */
    deckInfo.masBuffer = buf;
    deckInfo.masSize   = size;
    my_snprintf(deckInfo.name, (char*)displayName, 31, false);

    /* Reset ARM9 state for new module */
    arm9_baseBpm      = 0;   /* will be set from first state update */
    arm9_moduleState  = NULL;
    arm9_moduleLoaded = true;
    arm9_resetChannelState();
    for (u8 ci = 0; ci < N_CUES; ci++)
        arm9_cuePoints[ci] = 0;

    arm9_playing = false;

    iprintf("Loaded: %s\n", deckInfo.name);
    return 0;
}

u8 XMX_FileSystem_selectModule(char *folderPath)
{
    DIR *folder = opendir(folderPath);
    u16  fileCount;
    u32  pPosition = 0;
    char filepath[255] = "";

    XMX_FileSystem_displayHeader();
    fileCount = getFileCount(folder);
    listFolderOnPosition(folder, pPosition, fileCount);
    keysSetRepeat(20, 4);

    while (true) {
        scanKeys();
        u32 kdr = keysDownRepeat();

        if (kdr & KEY_UP) {
            pPosition = (pPosition == 0) ? 0 : pPosition - 1;
            listFolderOnPosition(folder, pPosition, fileCount);
        }
        if (kdr & KEY_DOWN) {
            pPosition = (pPosition == fileCount - 1) ? pPosition : pPosition + 1;
            listFolderOnPosition(folder, pPosition, fileCount);
        }
        if (keysDown() & KEY_RIGHT) {
            pPosition = ((pPosition + ENTRIES_PER_SCREEN) > (fileCount - 1))
                        ? fileCount - 1 : pPosition + ENTRIES_PER_SCREEN;
            listFolderOnPosition(folder, pPosition, fileCount);
        }
        if (keysDown() & KEY_LEFT) {
            pPosition = ((int32)(pPosition - ENTRIES_PER_SCREEN) < 0)
                        ? 0 : pPosition - ENTRIES_PER_SCREEN;
            listFolderOnPosition(folder, pPosition, fileCount);
        }
        if (keysDown() & KEY_B) {
            closedir(folder);
            return 1;   /* user cancelled */
        }
        if (keysDown() & KEY_A) {
            struct dirent *sel = getSelection(folder, pPosition);
            if (sel->d_type == TYPE_FOLDER) {
                if (!strcmp(sel->d_name, ".")) { swiWaitForVBlank(); continue; }
                if (!strcmp(sel->d_name, ".."))
                    navigateBackwards(&folder, folderPath);
                else
                    navigateToFolder(&folder, folderPath, sel->d_name);
                pPosition = 0;
                XMX_FileSystem_displayHeader();
                fileCount = getFileCount(folder);
                listFolderOnPosition(folder, pPosition, fileCount);
            } else if (isMAS(sel->d_name)) {
                /* Unload previous module */
                if (deckInfo.masBuffer != NULL)
                    XMX_UnloadMAS();

                composeFileName(filepath, folderPath, sel->d_name);
                consoleClear();

                int err = loadMAS(filepath, sel->d_name);
                closedir(folder);
                return (err == 0) ? 0 : 2;
            }
        }

        swiWaitForVBlank();
    }
}

static bool s_using_fat = false;

bool XMX_FileSystem_init(void)
{
    /* Try NitroFS first (embedded files inside the .nds — for emulators) */
    if (nitroFSInit(NULL))
        return true;

    /* Fall back to FAT (SD card — for flashcarts / native hardware) */
    if (fatInitDefault()) {
        s_using_fat = true;
        return true;
    }

    return false;
}

bool XMX_FileSystem_isFat(void)
{
    return s_using_fat;
}
