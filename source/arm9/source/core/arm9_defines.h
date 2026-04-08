#ifndef ARM9_SOURCE_ARM9_DEFINES_H_
#define ARM9_SOURCE_ARM9_DEFINES_H_

#define MAX(a,b)    ((a>b)?a:b)
#define MIN(a,b)    ((a>b)?b:a)

#define SCREEN_WIDTH    256
#define SCREEN_HEIGHT   192

#define DEFAULT_BPM     125
#define DEFAULT_TEMPO   6
#define DEFAULT_CUEPOS  0
#define DEFAULT_NUDGE   0

#define N_CUES  8

typedef enum {
    SCREEN_MODE_CH  = 0,
    SCREEN_MODE_CUE = 1,
    SCREEN_MODE_FX  = 2,
    SCREEN_MODE_VFX = 3,
} ScreenMode;

#endif /* ARM9_SOURCE_ARM9_DEFINES_H_ */
