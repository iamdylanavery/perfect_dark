#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h> 

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#include "gbiex.h"
#include "types.h"

#include "system.h"
#include "fs.h"
#include "data.h"
#include "romdata.h"
#include "ext_tex.h"

#define MAX_DEBUG_TEX 128
u16 debug_tex_id_list[MAX_DEBUG_TEX];
u16 debug_tex_list[MAX_DEBUG_TEX];
int debug_tex_count = 0;

extern bool g_DebugIsMenuOpen; // <--- Now it just points to debug2.c
bool g_DebugLaserFocus = false; // <--- Keep this one as is
bool g_DebugShowHud = false; // For F3 toggle
bool g_DebugFreezeList = false; // For F5 toggle
bool g_DebugFlatMode = false;

// At the top of the file:
int g_DebugMatrixMode = 0; // 0 = Off, 1 = Missing Only, 2 = All

// Update this line to match the new 3-argument version
void track_debug_tex(u8 type, u16 id, s32 texnum);

#define EXT_TEX_DIRNAME "ext_tex"
#define FONT_OUTLINES_DIR "outlines"

static char extTexPath[FS_MAXPATH + 1];

#define MAX_EXT_TEX 8192
#define NUM_FONTS 5
const u16 IDMASK_FONT_OUTLINE = MASK_FONT_OUTLINE << 8;

// A tiny 3x5 pixel font for Hex Characters (0-9, A-F)
static const u8 hex_font[16][15] = {
    {1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1}, // 0
    {0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1}, // 1
    {1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1}, // 2
    {1,1,1, 0,0,1, 1,1,1, 0,0,1, 1,1,1}, // 3
    {1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1}, // 4
    {1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1}, // 5
    {1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1}, // 6
    {1,1,1, 0,0,1, 0,1,0, 0,1,0, 0,1,0}, // 7
    {1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1}, // 8
    {1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1}, // 9
    {1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1}, // A
    {1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0}, // B
    {1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1}, // C
    {1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0}, // D
    {1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1}, // E
    {1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0}  // F
};

struct ExtTexture
{
	u8 *texdata;
	s32 texnum;
	char extension[5];
};

struct ModelTextures
{
	s16 fileNum;
	s16 numTextures;
	struct ExtTexture *textures;
};

static struct ExtTexture extTextures[MAX_EXT_TEX];

static struct ModelTextures *modelTextures;
static s32 numModels;

#if VERSION == VERSION_PAL_FINAL
#define NCHARS 135
#else
#define NCHARS 94
#endif

static struct ExtTexture fontExtTextures[NUM_FONTS][NCHARS];
static struct ExtTexture fontOutlineExtTextures[NUM_FONTS][NCHARS];

#define FONT_HANDELGOTHICSM 0
#define FONT_HANDELGOTHICMD 1
#define FONT_HANDELGOTHICXS 2
#define FONT_HANDELGOTHICLG 3
#define FONT_NUMERIC 4

s32 fileInfo(const char *filename, s32 *texNum, char extension[5])
{
	char *ext = strrchr(filename, '.');

	// no extension
	if (!ext) return 1;

	++ext;
	strncpy(extension, ext, 5);

	// get the filename without extension
	char basename[16] = { 0 };
	memcpy(basename, filename, strlen(filename) - strlen(ext) - 1);

	*texNum = strtol(basename, NULL, 16);

	return 0;
}

struct ExtTexture *lookupModelTex(u16 fileNum, s32 texNum)
{
	if (fileNum > NUM_FILES) {
		sysLogPrintf(LOG_WARNING, "Invalid fileNum in lookupModelTex: %04x, texNum: %04x", fileNum, texNum);
		return 0;
	}

	struct ModelTextures *modelTex = NULL;
	for (int i = 0; i < numModels; ++i) {
		if (modelTextures[i].fileNum == fileNum) {
			modelTex = &modelTextures[i];
			break;
		}
	}

	if (modelTex == NULL)
		return NULL;

	for (int i = 0; i < modelTex->numTextures; ++i) {
		if (modelTex->textures[i].texnum == texNum)
			return &modelTex->textures[i];
	}

	return NULL;
}

struct ExtTexture *getExtTexture(u8 type, u16 id, s32 texnum)
{
    track_debug_tex(type, id, texnum); // Added 'type' here

    struct ExtTexture *texlist;
    switch (type) {
        case G_TEXTYPE_NONE:
            return NULL;
        case G_TEXTYPE_GENERAL:
            return &extTextures[texnum];
        case G_TEXTYPE_MODEL:
            return lookupModelTex(id, texnum);
		case G_TEXTYPE_FONT: {
			if (id & IDMASK_FONT_OUTLINE)
				return &fontOutlineExtTextures[id & ~IDMASK_FONT_OUTLINE][texnum];

			return &fontExtTextures[id][texnum];
		}
		default:
			sysLogPrintf(LOG_WARNING, "Invalid Texture type: %d, texnum: %04x", type, texnum);
			return NULL;
	}
}

u8 extTexExists(u8 type, u16 id, s32 texnum)
{
    // If ANY debug mode is active, we bypass and return 1.
    // This allows extTexLoad to dynamically decide whether to load your HD art or generate a debug block.
    if ((g_DebugMatrixMode > 0 || g_DebugFlatMode) && (type == 1 || type == 2)) {
        return 1; 
    }

    // Standard game logic (no hacks active)
    struct ExtTexture *tex = getExtTexture(type, id, texnum);
    return tex && tex->texnum >= 0;
}

char *resolveFontname(const u8 fontId)
{
	switch (fontId) {
		case FONT_HANDELGOTHICSM: return "fonthandelgothicsm";
		case FONT_HANDELGOTHICMD: return "fonthandelgothicmd";
		case FONT_HANDELGOTHICXS: return "fonthandelgothicxs";
		case FONT_HANDELGOTHICLG: return "fonthandelgothiclg";
		case FONT_NUMERIC: return "fontnumeric";
		default: return "";
	}
}

u8 getTexPath(char *dst, u8 type, u16 id, s32 texnum)
{
	struct ExtTexture *tex;
	const char *name;

	switch (type) {
		case G_TEXTYPE_GENERAL: {
			tex = &extTextures[texnum];
			snprintf(dst, FS_MAXPATH, "%s/%04x.%s", extTexPath, texnum, tex->extension);
			return 0;
		}
		case G_TEXTYPE_FONT: {
			name = resolveFontname(id & ~IDMASK_FONT_OUTLINE);

			if (id & IDMASK_FONT_OUTLINE) {
				tex = &fontOutlineExtTextures[id & ~IDMASK_FONT_OUTLINE][texnum];
				snprintf(dst, FS_MAXPATH, "%s/%s/" FONT_OUTLINES_DIR "/%02x.%s", extTexPath, name, texnum, tex->extension);
				return 0;
			}

			tex = &fontExtTextures[id][texnum];
			snprintf(dst, FS_MAXPATH, "%s/%s/%02x.%s", extTexPath, name, texnum, tex->extension);
			return 0;
		}
		case G_TEXTYPE_MODEL: {
			name = romdataFileGetName(id);
			tex = lookupModelTex(id, texnum);
			snprintf(dst, FS_MAXPATH, "%s/%s/%05x.%s", extTexPath, name, texnum, tex->extension);
			return 0;
		}
		default: return 1;
	}
}


void track_debug_tex(u8 type, u16 id, s32 texnum) {
    if (g_DebugFreezeList) return;

    // FIX: Only filter out Type 3 (Fonts). 
    // Type 1 (General) and Type 2 (Models/Props) stay in the list!
    if (type == 3) return; 

    if (debug_tex_count >= MAX_DEBUG_TEX) return;

    for (int i = 0; i < debug_tex_count; i++) {
        if (debug_tex_list[i] == (u16)texnum && debug_tex_id_list[i] == id) return;
    }

    debug_tex_id_list[debug_tex_count] = id;
    debug_tex_list[debug_tex_count] = (u16)texnum;
    debug_tex_count++;
}

u8 *extTexLoad(u8 type, u16 id, s32 texnum, u32 *width, u32 *height)
{
    // --- 1. ATTEMPT STANDARD HD LOAD FIRST ---
    // We try to load your PNG before making any debug decisions (unless in Matrix Mode 2, which overrides all)
    u8 *loaded_data = NULL;
    char path[FS_MAXPATH];
    u8 err = getTexPath(path, type, id, texnum);
    struct ExtTexture *tex = getExtTexture(type, id, texnum);

    if (g_DebugMatrixMode != 2 && !err && tex) {
        #ifdef __APPLE__
            stbi_set_flip_vertically_on_load(1);
        #endif
        u32 channels;
        loaded_data = stbi_load(path, width, height, &channels, 4);
    }

    // --- 2. DECIDE IF WE SHOULD GENERATE THE MATRIX BLOCK ---
    bool generate_matrix = false;
    if (type == 1 || type == 2) {
        if (g_DebugMatrixMode == 2) {
            generate_matrix = true; // State 2: Tag everything
        } else if (g_DebugMatrixMode == 1) {
            if (loaded_data == NULL) {
                generate_matrix = true; // State 1: ONLY tag if your HD texture is missing!
            }
        }
    }

    // --- 3. THE MATRIX BLOCK GENERATOR ---
    if (generate_matrix) {
        // Free loaded_data if we loaded it but decided to override (should be NULL anyway)
        if (loaded_data) {
            stbi_image_free(loaded_data);
            loaded_data = NULL;
        }

        *width = 64; 
        *height = 64;
        u8 *data = (u8 *)malloc(64 * 64 * 4);
        
        // Fill background with Dark Blue
        for (int i = 0; i < 64 * 64 * 4; i += 4) {
            data[i] = 0; data[i+1] = 0; data[i+2] = 80; data[i+3] = 255;
        }
        
        int digits[6];
        digits[0] = (id >> 4) & 0xF;      digits[1] = id & 0xF;
        digits[2] = (texnum >> 12) & 0xF; digits[3] = (texnum >> 8) & 0xF;
        digits[4] = (texnum >> 4) & 0xF;  digits[5] = texnum & 0xF;
        
        // Center File ID (Pink)
        for (int d = 0; d < 2; d++) {
            int startX = 22 + (d * 5);
            int startY = 24;
            for (int fy = 0; fy < 5; fy++) {
                for (int fx = 0; fx < 3; fx++) {
                    if (hex_font[digits[d]][fy * 3 + fx]) {
                        int py = startY + fy; 
                        int px = startX + fx;
                        int idx = ((63 - py) * 64 + px) * 4;
                        data[idx]=255; data[idx+1]=0; data[idx+2]=255; data[idx+3]=255;
                    }
                }
            }
        }
        
        // Center Tex ID (Yellow)
        for (int d = 0; d < 4; d++) {
            int startX = 18 + (d * 5);
            int startY = 32;
            for (int fy = 0; fy < 5; fy++) {
                for (int fx = 0; fx < 3; fx++) {
                    if (hex_font[digits[d+2]][fy * 3 + fx]) {
                        int py = startY + fy;
                        int px = startX + fx;
                        int idx = ((63 - py) * 64 + px) * 4;
                        data[idx]=255; data[idx+1]=255; data[idx+2]=0; data[idx+3]=255;
                    }
                }
            }
        }

        // Top Border
        for (int d = 0; d < 4; d++) {
            int startX = 24 + (d * 4);
            int startY = 2;
            for (int fy = 0; fy < 5; fy++) {
                for (int fx = 0; fx < 3; fx++) {
                    if (hex_font[digits[d+2]][fy * 3 + fx]) {
                        int px = startX + fx;
                        int py = startY + fy;
                        int idx = ((63 - py) * 64 + px) * 4;
                        data[idx]=255; data[idx+1]=255; data[idx+2]=0; data[idx+3]=255;
                    }
                }
            }
        }

        // Bottom Border
        for (int d = 0; d < 4; d++) {
            int startX = 36 - (d * 4);
            int startY = 57;
            for (int fy = 0; fy < 5; fy++) {
                for (int fx = 0; fx < 3; fx++) {
                    if (hex_font[digits[d+2]][fy * 3 + fx]) {
                        int px = startX + (2 - fx);
                        int py = startY + (4 - fy);
                        int idx = ((63 - py) * 64 + px) * 4;
                        data[idx]=255; data[idx+1]=255; data[idx+2]=0; data[idx+3]=255;
                    }
                }
            }
        }

        // Left Border
        for (int d = 0; d < 4; d++) {
            int startX = 2;
            int startY = 24 + (d * 4);
            for (int fy = 0; fy < 5; fy++) {
                for (int fx = 0; fx < 3; fx++) {
                    if (hex_font[digits[d+2]][fy * 3 + fx]) {
                        int px = startX + fy;
                        int py = startY + (2 - fx);
                        int idx = ((63 - py) * 64 + px) * 4;
                        data[idx]=255; data[idx+1]=255; data[idx+2]=0; data[idx+3]=255;
                    }
                }
            }
        }

        // Right Border
        for (int d = 0; d < 4; d++) {
            int startX = 57;
            int startY = 24 + (d * 4);
            for (int fy = 0; fy < 5; fy++) {
                for (int fx = 0; fx < 3; fx++) {
                    if (hex_font[digits[d+2]][fy * 3 + fx]) {
                        int px = startX + (4 - fy);
                        int py = startY + fx;
                        int idx = ((63 - py) * 64 + px) * 4;
                        data[idx]=255; data[idx+1]=255; data[idx+2]=0; data[idx+3]=255;
                    }
                }
            }
        }

        if (tex) tex->texdata = data;
        return data;
    }

    // --- 4. FLAT SHADING OVERRIDE ---
    if (g_DebugFlatMode && (type == 1 || type == 2)) {
        if (loaded_data) {
            // 1. Texture exists! Make the interior white, and the outer 1-pixel border black.
            u32 w = *width;
            u32 h = *height;
            
            for (u32 y = 0; y < h; y++) {
                for (u32 x = 0; x < w; x++) {
                    u32 i = (y * w + x) * 4;
                    
                    // Check if the pixel is on the outermost border of the texture
                    if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                        loaded_data[i]   = 0;   // R (Black outline)
                        loaded_data[i+1] = 0;   // G
                        loaded_data[i+2] = 0;   // B
                        // We leave Alpha [i+3] untouched so grates/glass keep their shapes!
                    } else {
                        loaded_data[i]   = 255; // R (White fill)
                        loaded_data[i+1] = 255; // G
                        loaded_data[i+2] = 255; // B
                    }
                }
            }
            
            if (tex) tex->texdata = loaded_data;
            return loaded_data;

        } else {
            // 2. Texture is missing! Generate a 16x16 grid tile with a black border.
            *width = 16;
            *height = 16;
            u8 *proc_data = (u8 *)malloc(16 * 16 * 4);
            
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    int i = (y * 16 + x) * 4;
                    
                    if (x == 0 || x == 15 || y == 0 || y == 15) {
                        proc_data[i]   = 0;   // R (Black outline)
                        proc_data[i+1] = 0;   // G
                        proc_data[i+2] = 0;   // B
                        proc_data[i+3] = 255; // Solid opacity
                    } else {
                        proc_data[i]   = 255; // R (White fill)
                        proc_data[i+1] = 255; // G
                        proc_data[i+2] = 255; // B
                        proc_data[i+3] = 255;
                    }
                }
            }
            
            if (tex) tex->texdata = proc_data;
            return proc_data;
        }
    }

    // --- 5. STANDARD RETURN (Normal HD Texture) ---
    if (loaded_data) {
        if (tex) tex->texdata = loaded_data;
        return loaded_data;
    }

    return NULL;
}

u8 extTexFontID(struct font *font) {
	if (font == g_FontHandelGothicSm)
		return FONT_HANDELGOTHICSM;
	else if (font == g_FontHandelGothicMd)
		return FONT_HANDELGOTHICMD;
	else if (font == g_FontHandelGothicXs)
		return FONT_HANDELGOTHICXS;
	else if (font == g_FontHandelGothicLg)
		return FONT_HANDELGOTHICLG;
	else if (font == g_FontNumeric)
		return FONT_NUMERIC;

	return 0xff;
}

u8 resolveFontID(const char *fontname)
{
	if (strcmp(fontname, "fonthandelgothicsm") == 0)
		return FONT_HANDELGOTHICSM;
	else if (strcmp(fontname, "fonthandelgothicmd") == 0)
		return FONT_HANDELGOTHICMD;
	else if (strcmp(fontname, "fonthandelgothicxs") == 0)
		return FONT_HANDELGOTHICXS;
	else if (strcmp(fontname, "fonthandelgothiclg") == 0)
		return FONT_HANDELGOTHICLG;
	else if (strcmp(fontname, "fontnumeric") == 0)
		return FONT_NUMERIC;

	return 0xff;
}

void setTex(struct ExtTexture *texlist, s32 index, s32 texNum, char extension[5])
{
	struct ExtTexture *tex = &texlist[index];
	tex->texnum = texNum;
	strcpy(tex->extension, extension);
}

void readModelTextures(const char *path, s16 fileNum, s32 *modelOffset, struct ModelTextures *modelTex)
{
	DIR *dr = opendir(path);
	struct dirent *de;

	s32 MAX_TEX = 16;
	modelTex->textures = sysMemAlloc(MAX_TEX * sizeof(struct ExtTexture));
	modelTex->numTextures = 0;
	modelTex->fileNum = fileNum;

	char extension[5] = { 0 };

	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		s32 texNum;
		s32 err = fileInfo(name, &texNum, extension);
		// no extension: skip
		if (err) continue;

		setTex(modelTex->textures, modelTex->numTextures, texNum, extension);
		modelTex->numTextures++;

		// allocate more memory for model textures if needed
		if (modelTex->numTextures > MAX_TEX) {
			MAX_TEX *= 2;
			modelTex->textures = sysMemRealloc(modelTex->textures, MAX_TEX * sizeof(struct ExtTexture));
		}
	}
	closedir(dr);

	// shrink the textures array to the actual number of textures found
	s32 numTex = modelTex->numTextures;

	if (numTex > 0)
		modelTex->textures = sysMemRealloc(modelTex->textures, numTex * sizeof(struct ExtTexture));

	for (int i = 0; i < modelTex->numTextures; ++i) {
		modelTex->textures[i].texdata = 0;
	}
}

void readFontTextures(const char *path, const char *fontName)
{
	DIR *dr = opendir(path);
	struct dirent *de;

	u8 fontID = resolveFontID(fontName);
	char extension[5] = { 0 };

	char outlinesPath[FS_MAXPATH];
	sprintf(outlinesPath , "%s/" FONT_OUTLINES_DIR, path);
	u8 outlines = false;

	while (true) {
		de = readdir(dr);
		// after done processing the font folder, do the same for the outlines folder if any
		if (de == NULL) {
			if (outlines) break;

			outlines = true;
			closedir(dr);
			dr = opendir(outlinesPath);
			de = readdir(dr);

			if (de == NULL) break;
		}

		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		s32 texNum;
		s32 err = fileInfo(name, &texNum, extension);
		// no extension: skip
		if (err) continue;

		if (outlines)
			setTex(fontOutlineExtTextures[fontID], texNum, texNum, extension);
		else
			setTex(fontExtTextures[fontID], texNum, texNum, extension);
	}

	closedir(dr);
}

void extTexFree()
{
	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		if (extTextures[i].texdata)
			stbi_image_free(extTextures[i].texdata);

		extTextures[i].texdata = 0;
	}

	for (int i = 0; i < NUM_FONTS; ++i) {
		for (int j = 0; j < NCHARS; ++j) {
			if (fontExtTextures[i][j].texdata)
				stbi_image_free(fontExtTextures[i][j].texdata);

			if (fontOutlineExtTextures[i][j].texdata)
				stbi_image_free(fontOutlineExtTextures[i][j].texdata);

			fontExtTextures[i][j].texdata = 0;
			fontOutlineExtTextures[i][j].texdata = 0;
		}
	}

	for (int i = 0; i < numModels; ++i) {
		struct ModelTextures *modelTex = &modelTextures[i];
		for (int j = 0; j < modelTex->numTextures; ++j) {
			if (modelTex->textures[j].texdata)
				stbi_image_free(modelTex->textures[j].texdata);

			modelTex->textures[j].texdata = 0;
		}
	}
}

s32 extTexInit()
{
	const char *path = fsFullPath(EXT_TEX_DIRNAME);
	strcpy(extTexPath, path);

	for (int i = 0; i < MAX_EXT_TEX; ++i) {
		extTextures[i].texnum = -1;
		extTextures[i].texdata = 0;
	}

	for (int i = 0; i < NUM_FONTS; ++i) {
		for (int j = 0; j < NCHARS; ++j) {
			fontExtTextures[i][j].texnum = -1;
			fontExtTextures[i][j].texdata = 0;

			fontOutlineExtTextures[i][j].texnum = -1;
			fontOutlineExtTextures[i][j].texdata = 0;
		}
	}

	struct dirent *de;
	DIR *dr = opendir(extTexPath);

	char filepath[FS_MAXPATH];
	s32 modelOffset = 0;

	s32 MAX_MODELS = 16;
	numModels = 0;
	modelTextures = sysMemAlloc(MAX_MODELS * sizeof(struct ModelTextures));

	while ((de = readdir(dr)) != NULL) {
		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

		struct stat stbuf;
		sprintf(filepath , "%s/%s", extTexPath, de->d_name);
		if (stat(filepath, &stbuf) == -1) {
			sysLogPrintf(LOG_WARNING, "Unable to stat file: %s\n", filepath);
			continue;
		}

		// is a directory
		if (S_ISDIR(stbuf.st_mode)) {
			// models
			char s = name[0];
			if (s == 'P' || s == 'C' || s == 'G') {
				s16 fileNum = (s16)romdataFileGetNumForName(name);
				if (fileNum < 0) {
					sysLogPrintf(LOG_WARNING, "extTexInit invalid file: %s\n", name);
					continue;
				}

				struct ModelTextures *modelTex = &modelTextures[numModels++];
				readModelTextures(filepath, fileNum, &modelOffset, modelTex);

				// allocate more memory if necessary
				if (numModels > MAX_MODELS) {
					MAX_MODELS *= 2;
					modelTextures = sysMemRealloc(modelTextures, MAX_MODELS);
				}

			}
			// fonts
			else if (s == 'f') {
				readFontTextures(filepath, name);
			}
		} else {
			s32 texNum = 0;
			char extension[5] = { 0 };
			s32 err = fileInfo(name, &texNum, extension);

			// no extension: skip
			if (err) continue;

			setTex(extTextures, texNum, texNum, extension);
		}
	}

	closedir(dr);

	// shrink this array to the actual number of model folders found
	if (numModels > 0)
		modelTextures = sysMemRealloc(modelTextures, numModels * sizeof(struct ModelTextures));

	return 0;
}
