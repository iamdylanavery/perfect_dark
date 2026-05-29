#ifndef EXT_FLARES_H
#define EXT_FLARES_H

#include "types.h"

// --- Global Menu Configurations (Exposed for Options Menu) ---
extern s32 g_FlaresEnabled;
extern s32 g_FlareCoreEnabled;
extern s32 g_FlareStreakEnabled;
extern s32 g_FlareGhostsEnabled;

extern f32 g_FlareCoreBrightness;
extern f32 g_FlareStreakBrightness;
extern f32 g_FlareGhostBrightness;

extern f32 g_FlareStreakWidth;
extern f32 g_FlareStreakHeight;
extern f32 g_FlareStreakDrift;
extern f32 g_FlareStreakDriftBoost;
extern f32 g_FlareStreakScatter;

extern f32 g_FlareGhostDriftClose;
extern f32 g_FlareGhostDriftFar;

extern f32 g_FlareGhostBloom;
extern f32 g_FlareGhostFade;
extern s32 g_FlareGhostCount;

// --- Core Functions ---
void ext_flares_init(void);
void ext_flares_push(f32 x, f32 y, f32 depth, u8 r, u8 g, u8 b, u8 a, f32 scale_x, f32 scale_y);
void ext_flares_render(void);
void ext_flares_register_config(void); // New! Handles self-contained saving

#endif // EXT_FLARES_H
