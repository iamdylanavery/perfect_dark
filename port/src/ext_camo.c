#include "ext_camo.h"

int g_ExtCamoActiveThisDraw = 0;
float g_ExtCamoAlpha = 1.0f;
float g_ExtCamoProgress = 0.0f;
int g_ExtCamoIsNPC = 0; // [NEW]

Gfx* ext_camo_append_command(Gfx* gdl, int active, int alpha, float progress, int is_npc) {
    gdl->words.w0 = (0x77 << 24);
    int prog_int = (int)(progress * 255.0f);
    
    // Format: [Active] [IsNPC] [Progress] [Alpha]
    gdl->words.w1 = ((active & 0xFF) << 24) | ((is_npc & 0xFF) << 16) | ((prog_int & 0xFF) << 8) | (alpha & 0xFF);
    return gdl + 1;
}