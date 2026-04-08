#ifndef ARM9_SOURCE_LIBXMX_H_
#define ARM9_SOURCE_LIBXMX_H_

#include <nds.h>

/*
 * Deck state — holds the loaded MAS buffer so we can restart playback.
 */
typedef struct {
    void  *masBuffer;   /* malloc'd buffer holding the loaded .mas file */
    u32    masSize;     /* size in bytes                                 */
    char   name[32];    /* display name (filename without extension)     */
} XMX_DeckInfo;

extern XMX_DeckInfo deckInfo;

/* Free masBuffer and reset deckInfo. Stops playback before freeing. */
void XMX_UnloadMAS(void);

#endif /* ARM9_SOURCE_LIBXMX_H_ */
