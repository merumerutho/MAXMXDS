#ifndef ARM9_SOURCE_FILESYSTEM_H_
#define ARM9_SOURCE_FILESYSTEM_H_

#include <nds.h>
#include <dirent.h>
#include <filesystem.h>
#include "libXMX.h"

#define ENTRIES_PER_SCREEN  16
#define TYPE_FOLDER         DT_DIR

bool XMX_FileSystem_init(void);
bool XMX_FileSystem_isFat(void);
u8   XMX_FileSystem_selectModule(char *folderPath);

void my_snprintf(char *dest, char *src, u8 len, bool fillWSpace);

#endif /* ARM9_SOURCE_FILESYSTEM_H_ */
