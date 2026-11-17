#ifndef EXT_MUSIC_CATEGORIES_H
#define EXT_MUSIC_CATEGORIES_H

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

// Clean Range Switch: All instruments 0 - 125 mapped
static inline InstrumentCategory extAudioGetInstCategory(int inst_idx) {
    switch (inst_idx) {

        case 28:
            return CAT_GUITAR;

        case 10:
        case 22 ... 23:
            return CAT_HORNS;

        case 9:
        case 17:
        case 32:
            return CAT_STRINGS;

        case 11:
        case 44:
        case 63 ... 64:
        case 70:
            return CAT_PIANO;

        case 12:
        case 14:
        case 16:
        case 50:
        case 52:
        case 80:
            return CAT_SYNTHS;

        case 6:
        case 20:
        case 74:
        case 118:
            return CAT_VOX;

        case 30:
        case 35:
        case 71:
        case 107 ... 108:
        case 124 ... 125:
            return CAT_AMBIENCE;

        case 2:
        case 55:
        case 81:
        case 84 ... 99:
            return CAT_BASS;

        case 0 ... 1:
        case 3:
        case 5:
        case 8:
        case 15:
        case 18 ... 19:
        case 21:
        case 24:
        case 31:
        case 37:
        case 41:
        case 43:
        case 46:
        case 57:
        case 60:
        case 68:
        case 73:
        case 75:
        case 110:
        case 113:
        case 116:
            return CAT_PERCUSSION;

        case 4:
        case 13:
        case 26 ... 27:
        case 29:
        case 33 ... 34:
        case 38:
        case 42:
        case 45:
        case 47:
        case 49:
        case 51:
        case 59:
        case 62:
        case 72:
        case 78 ... 79:
        case 83:
        case 101:
        case 104 ... 106:
        case 114:
        case 119 ... 121:
        case 123:
            return CAT_SFX;

        case 7:
        case 25:
        case 36:
        case 39 ... 40:
        case 48:
        case 53 ... 54:
        case 56:
        case 58:
        case 61:
        case 65 ... 67:
        case 69:
        case 76 ... 77:
        case 82:
        case 100:
        case 102 ... 103:
        case 109:
        case 111 ... 112:
        case 115:
        case 117:
        case 122:
            return CAT_BEEP;

        default:
            return CAT_UNKNOWN;
    }
}

#endif // EXT_MUSIC_CATEGORIES_H