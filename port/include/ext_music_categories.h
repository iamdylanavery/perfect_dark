#ifndef EXT_MUSIC_CATEGORIES_H
#define EXT_MUSIC_CATEGORIES_H

// The 11 instrument stems based on your spreadsheet
typedef enum {
    CAT_AMBIENCE = 0,
    CAT_SFX,
    CAT_VOX,
    CAT_PERCUSSION,
    CAT_SYNTHS,
    CAT_PIANO,
    CAT_STRINGS,
    CAT_HORNS,
    CAT_GUITAR,
    CAT_BASS,
    CAT_BEEP,
    CAT_UNKNOWN // Failsafe for any undefined IDs
} InstrumentCategory;

// Global array holding the toggle state of each stem (1 = ON, 0 = MUTED)
extern int g_ExtMusicCategoryEnabled[12];

// Function prototype
InstrumentCategory extAudioGetInstCategory(int inst_idx);

#endif // EXT_MUSIC_CATEGORIES_H