#ifndef EXT_CAMO_H
#define EXT_CAMO_H

#include <ultra64.h> 

#ifdef __cplusplus
extern "C" {
#endif

// We pick 0x77 because it is safely unused by standard N64 F3DEX2 microcode
#define G_EXT_CAMO_TOGGLE 0x77

// Global toggle for the Options Menu (Default to 1 / ON)
extern int g_ExtCamoEnabled;

// Global state tracking (read by our future shader and OpenGL backend)
extern int g_ExtCamoActiveThisDraw;
extern float g_ExtCamoAlpha;
extern float g_ExtCamoProgress;
extern int g_ExtCamoIsNPC;

// [NEW] Customization Options
extern float g_ExtCamoDistortionPlayer;
extern float g_ExtCamoDistortionNPC;
extern float g_ExtCamoAberrationScale;
extern float g_ExtCamoOffsetX;
extern float g_ExtCamoOffsetY;
extern float g_ExtCamoShimmerR;
extern float g_ExtCamoShimmerG;
extern float g_ExtCamoShimmerB;
extern float g_ExtCamoShimmerThickness;

// UI Menu Expose
extern struct menudialogdef g_ExtCamoMenuDialog;

// RSP Command Appender
Gfx* ext_camo_append_command(Gfx* gdl, int active, int alpha, float progress, int is_npc);

// Game Logic Hooks
void extCamo_UpdatePlayerWeapon(int isHidden, int cloakPause, float updateDelta, int *is_cloaked, float *camo_progress);
void extCamo_UpdateNPC(int raw_cloak_alpha, int isHidden, int cloakFadeFinished, int cloakFadeFrac, int *is_cloaked, float *camo_progress);

#ifdef __cplusplus
}
#endif

#endif // EXT_CAMO_H