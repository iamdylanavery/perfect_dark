#ifndef EXT_CAMO_H
#define EXT_CAMO_H

#include <ultra64.h> // Or whichever header gives you the 'Gfx' struct in your port

#ifdef __cplusplus
extern "C" {
#endif

// We pick 0x77 because it is safely unused by standard N64 F3DEX2 microcode
#define G_EXT_CAMO_TOGGLE 0x77

// Global state tracking (read by our future shader)
extern int g_ExtCamoActiveThisDraw;
extern float g_ExtCamoAlpha;

extern int g_ExtCamoActiveThisDraw;
extern float g_ExtCamoAlpha;
extern float g_ExtCamoProgress; // [NEW]
extern int g_ExtCamoIsNPC; // [NEW]

// [NEW] Added 'is_npc'
Gfx* ext_camo_append_command(Gfx* gdl, int active, int alpha, float progress, int is_npc);

#ifdef __cplusplus
}
#endif

#endif // EXT_CAMO_H