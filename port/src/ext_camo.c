#include "game/menu.h"
#include <stdio.h> // for sprintf
#include "platform.h" // Gives us the PD_CONSTRUCTOR macro
#include "config.h"   // Gives us the configRegister functions

#include "ext_camo.h"

// The Master Toggle (1 = Modern Cloak, 0 = N64 Original Cloak)
int g_ExtCamoEnabled = 1;

// Internal state tracking
int g_ExtCamoActiveThisDraw = 0;
float g_ExtCamoAlpha = 1.0f;
float g_ExtCamoProgress = 0.0f;
int g_ExtCamoIsNPC = 0;

// Default visual settings (will be overridden by options menu / pd.ini later)
float g_ExtCamoDistortionPlayer = 0.10f;
float g_ExtCamoDistortionNPC = 0.01f;
float g_ExtCamoAberrationScale = 1.0f;
float g_ExtCamoOffsetX = 0.0f;
float g_ExtCamoOffsetY = 0.0f;
float g_ExtCamoShimmerR = 0.0f;
float g_ExtCamoShimmerG = 0.8f;
float g_ExtCamoShimmerB = 1.0f;
float g_ExtCamoShimmerThickness = 0.02f;

Gfx* ext_camo_append_command(Gfx* gdl, int active, int alpha, float progress, int is_npc) {
    gdl->words.w0 = (G_EXT_CAMO_TOGGLE << 24);
    int prog_int = (int)(progress * 255.0f);
    
    // Format: [Active] [IsNPC] [Progress] [Alpha]
    gdl->words.w1 = ((active & 0xFF) << 24) | ((is_npc & 0xFF) << 16) | ((prog_int & 0xFF) << 8) | (alpha & 0xFF);
    
    return gdl + 1;
}

void extCamo_UpdatePlayerWeapon(int isHidden, int cloakPause, float updateDelta, int *is_cloaked, float *camo_progress) {
    if (isHidden) {
        *is_cloaked = 1;
        if (cloakPause <= 0) {
            *camo_progress += updateDelta / 60.0f;
            if (*camo_progress > 1.0f) *camo_progress = 1.0f;
        }
    } else {
        if (*camo_progress > 0.0f) {
            *is_cloaked = 1;
            *camo_progress -= updateDelta / 60.0f;
            if (*camo_progress < 0.0f) *camo_progress = 0.0f;
        } else {
            *is_cloaked = 0;
        }
    }
}

void extCamo_UpdateNPC(int raw_cloak_alpha, int isHidden, int cloakFadeFinished, int cloakFadeFrac, int *is_cloaked, float *camo_progress) {
    *is_cloaked = (raw_cloak_alpha < 255);
    *camo_progress = 0.0f;
    
    if (*is_cloaked) {
        if (isHidden) {
            if (cloakFadeFinished) {
                *camo_progress = 1.0f;
            } else {
                *camo_progress = cloakFadeFrac / 128.0f;
            }
        } else {
            if (cloakFadeFrac > 0) {
                *camo_progress = cloakFadeFrac / 128.0f;
            }
        }
        if (*camo_progress > 1.0f) *camo_progress = 1.0f;
        if (*camo_progress < 0.0f) *camo_progress = 0.0f;
    }
}
// =========================================================================
// OPTIONS MENU UI HANDLERS
// =========================================================================

static MenuItemHandlerResult menuhandlerExtCamoEnabled(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GET) return g_ExtCamoEnabled;
    if (operation == MENUOP_SET) g_ExtCamoEnabled = data->checkbox.value;
    return 0;
}

static MenuItemHandlerResult menuhandlerExtCamoDistPlayer(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoDistortionPlayer * 100.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoDistortionPlayer = (f32)data->slider.value / 100.f;
    if (operation == MENUOP_GETSLIDERLABEL) sprintf(data->slider.label, "%.2f", (f32)data->slider.value / 100.f);
    return 0;
}

static MenuItemHandlerResult menuhandlerExtCamoDistNPC(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoDistortionNPC * 100.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoDistortionNPC = (f32)data->slider.value / 100.f;
    if (operation == MENUOP_GETSLIDERLABEL) sprintf(data->slider.label, "%.2f", (f32)data->slider.value / 100.f);
    return 0;
}

static MenuItemHandlerResult menuhandlerExtCamoAberration(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoAberrationScale * 100.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoAberrationScale = (f32)data->slider.value / 100.f;
    if (operation == MENUOP_GETSLIDERLABEL) sprintf(data->slider.label, "%.2f", (f32)data->slider.value / 100.f);
    return 0;
}

static MenuItemHandlerResult menuhandlerExtCamoOffsetX(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = (g_ExtCamoOffsetX + 0.5f) * 100.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoOffsetX = ((f32)data->slider.value / 100.f) - 0.5f;
    if (operation == MENUOP_GETSLIDERLABEL) sprintf(data->slider.label, "%d", (int)(g_ExtCamoOffsetX * 100.f));
    return 0;
}

static MenuItemHandlerResult menuhandlerExtCamoOffsetY(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = (g_ExtCamoOffsetY + 0.5f) * 100.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoOffsetY = ((f32)data->slider.value / 100.f) - 0.5f;
    if (operation == MENUOP_GETSLIDERLABEL) sprintf(data->slider.label, "%d", (int)(g_ExtCamoOffsetY * 100.f));
    return 0;
}

// --- SHIMMER MENU HANDLERS ---
static MenuItemHandlerResult menuhandlerExtCamoShimmerR(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoShimmerR * 255.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoShimmerR = (f32)data->slider.value / 255.f;
    return 0;
}
static MenuItemHandlerResult menuhandlerExtCamoShimmerG(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoShimmerG * 255.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoShimmerG = (f32)data->slider.value / 255.f;
    return 0;
}
static MenuItemHandlerResult menuhandlerExtCamoShimmerB(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoShimmerB * 255.f + 0.5f;
    if (operation == MENUOP_SET) g_ExtCamoShimmerB = (f32)data->slider.value / 255.f;
    return 0;
}
static MenuItemHandlerResult menuhandlerExtCamoShimmerThick(s32 operation, struct menuitem *item, union handlerdata *data) {
    if (operation == MENUOP_GETSLIDER) data->slider.value = g_ExtCamoShimmerThickness * 1000.f + 0.5f; 
    if (operation == MENUOP_SET) g_ExtCamoShimmerThickness = (f32)data->slider.value / 1000.f;
    return 0;
}

// --- SHIMMER SUB-MENU DEFINITION ---
static struct menuitem g_ExtCamoShimmerMenuItems[] = {
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Thickness", 100, menuhandlerExtCamoShimmerThick,
    },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Line Red", 255, menuhandlerExtCamoShimmerR,
    },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Line Green", 255, menuhandlerExtCamoShimmerG,
    },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Line Blue", 255, menuhandlerExtCamoShimmerB,
    },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)" ", 0, NULL },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_SELECTABLE_CLOSESDIALOG | MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Back\n", 0, NULL },
    { MENUITEMTYPE_END }
};

struct menudialogdef g_ExtCamoShimmerMenuDialog = {
    MENUDIALOGTYPE_DEFAULT, (uintptr_t)"Laser Shimmer Settings", g_ExtCamoShimmerMenuItems, NULL, MENUDIALOGFLAG_LITERAL_TEXT, NULL,
};

// --- MAIN CLOAK MENU DEFINITION ---
static struct menuitem g_ExtCamoMenuItems[] = {
    {
        MENUITEMTYPE_CHECKBOX, 0, MENUITEMFLAG_LITERAL_TEXT,
        (uintptr_t)"Enable Modern Cloak", 0, menuhandlerExtCamoEnabled,
    },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Player Distortion", 50, menuhandlerExtCamoDistPlayer,
    },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"NPC Distortion", 50, menuhandlerExtCamoDistNPC,
    },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Chromatic Aberration", 200, menuhandlerExtCamoAberration,
    },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Shift X", 100, menuhandlerExtCamoOffsetX,
    },
    {
        MENUITEMTYPE_SLIDER, 0, MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SLIDER_WIDE,
        (uintptr_t)"Shift Y", 100, menuhandlerExtCamoOffsetY,
    },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    {
        MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_SELECTABLE_OPENSDIALOG | MENUITEMFLAG_LITERAL_TEXT,
        (uintptr_t)"Laser Sweep Settings...\n", 0, (void *)&g_ExtCamoShimmerMenuDialog,
    },
    { MENUITEMTYPE_SEPARATOR, 0, 0, 0, 0, NULL },
    { MENUITEMTYPE_LABEL, 0, MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)" ", 0, NULL },
    { MENUITEMTYPE_SELECTABLE, 0, MENUITEMFLAG_SELECTABLE_CLOSESDIALOG | MENUITEMFLAG_LITERAL_TEXT, (uintptr_t)"Back\n", 0, NULL },
    { MENUITEMTYPE_END }
};

struct menudialogdef g_ExtCamoMenuDialog = {
    MENUDIALOGTYPE_DEFAULT, (uintptr_t)"Modern Cloak Options", g_ExtCamoMenuItems, NULL, MENUDIALOGFLAG_LITERAL_TEXT, NULL,
};

// =========================================================================
// PD.INI CONFIGURATION SAVING/LOADING
// =========================================================================

PD_CONSTRUCTOR static void extCamoConfigInit(void)
{
    // configRegisterInt:   "Key.Name", &variable, min_val, max_val
    configRegisterInt("Video.ExtCamoEnabled", &g_ExtCamoEnabled, 0, 1);
    
    // configRegisterFloat: "Key.Name", &variable, min_val, max_val
    configRegisterFloat("Video.ExtCamoDistPlayer", &g_ExtCamoDistortionPlayer, 0.0f, 1.0f);
    configRegisterFloat("Video.ExtCamoDistNPC", &g_ExtCamoDistortionNPC, 0.0f, 1.0f);
    configRegisterFloat("Video.ExtCamoAberration", &g_ExtCamoAberrationScale, 0.0f, 5.0f);
    configRegisterFloat("Video.ExtCamoOffsetX", &g_ExtCamoOffsetX, -0.5f, 0.5f);
    configRegisterFloat("Video.ExtCamoOffsetY", &g_ExtCamoOffsetY, -0.5f, 0.5f);
    configRegisterFloat("Video.ExtCamoShimmerR", &g_ExtCamoShimmerR, 0.0f, 1.0f);
    configRegisterFloat("Video.ExtCamoShimmerG", &g_ExtCamoShimmerG, 0.0f, 1.0f);
    configRegisterFloat("Video.ExtCamoShimmerB", &g_ExtCamoShimmerB, 0.0f, 1.0f);
    configRegisterFloat("Video.ExtCamoShimmerThick", &g_ExtCamoShimmerThickness, 0.0f, 0.1f);
}