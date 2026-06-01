#ifndef EXT_AUDIO_TRACKS_H
#define EXT_AUDIO_TRACKS_H

#include "types.h" // Needed for s32 if not already included

// 1:1 Map with the generated sequences.h
// 1:1 Sorted Map with Human-Readable Names
static const char *const g_MusicTrackNames[] = {
	// --- SYSTEM / MENUS / GENERAL ---
	"None",
	"Title 1",
	"Title 2",
	"Main Menu",
	"Pause Menu",
	"Combat Sim Menu",
	"Carrington Institute",
	"Credits",
	"Mission Success",
	"Combat Sim End",
	"Mission Failed",

	// --- CAMPAIGN LEVELS (STANDARD / X PAIRS) ---
	"Defection",
	"  Defection X",
	"Investigation",
	"  Investigation X",
	"Extraction",
	"  Extraction X",
	"Villa",
	"  Villa X",
	"Chicago",
	"  Chicago X",
	"G5 Building",
	"  G5 Building X",
	"A51 Infiltration",
	"  A51 Infiltration X",
	"A51 Rescue",
	"  A51 Rescue X",
	"A51 Escape",
	"  A51 Escape X",
	"Air Base",
	"  Air Base X",
	"Air Force One",
	"  Air Force One X",
	"Crash Site",
	"  Crash Site X",
	"Pelagic II",
	"  Pelagic II X",
	"Deep Sea",
	"  Deep Sea X",
	"Institute Defense",
	"  Institute Defense X",
	"Attack Ship",
	"  Attack Ship X",
	"Skedar Ruins",
	"  Skedar Ruins X",
	"Skedar Leader",

	// --- MULTIPLAYER SPECIFIC THEMES ---
	"Dark Combat",
	"Skedar Mystery",
	"CI Operative",
	"dataDyne Action",
	"Maian Tears",
	"Alien Conflict",

	// --- INTROS, OUTROS, & CUTSCENES ---
	"CI Training",
	"CI Intro",
	"Defection Intro",
	"Defection Outro",
	"Investigation Intro",
	"Investigation Outro",
	"Chicago Intro",
	"Chicago Outro",
	"Extraction Intro",
	"Extraction Outro",
	"Villa Intro 1",
	"Villa Intro 2",
	"Villa Intro 3",
	"Villa Outro",
	"G5 Intro",
	"G5 Outro",
	"G5 Mid-Cutscene",
	"Infiltration Intro",
	"Infiltration Outro",
	"Rescue Intro",
	"Rescue Outro",
	"Escape Intro",
	"Escape Mid-Cut",
	"Escape Outro Lng",
	"Escape Outro Sht",
	"Air Base Intro",
	"Air Base Outro",
	"Air Base Outro Lng",
	"AFO Intro",
	"AFO Mid-Cutscene",
	"AFO Outro",
	"Crash Site Intro",
	"Crash Site Outro",
	"Pelagic Intro",
	"Pelagic Outro",
	"Deep Sea Intro",
	"Deep Sea Mid-Cut",
	"Deep Sea Outro",
	"Defense Intro",
	"Defense Outro",
	"Attack Ship Intro",
	"Attack Ship Outro",
	"Skedar Ruins Intro",
	"Skedar Ruins Outro",
	"Mission Unknown",

	// --- SOUND EFFECTS & AMBIENT WINDS ---
	"Investigation SFX",
	"dD Tower SFX",
	"Defection Intro SFX",
	"Extraction Outro SFX",
	"Infiltration SFX",
	"Deep Sea SFX",
	"AFO SFX",
	"Attack Ship SFX",
	"Escape Outro SFX",
	"A51 Loudspeaker 1",
	"A51 Loudspeaker 2",
	"Crash Site Wind",
	"Ocean Ambience",
	"Wind Ambience",
	"Traffic Ambience",
	"Skedar Wind",

	// --- BETA, DEATH, & UNUSED TUNES ---
	"Deep Sea Beta",
	"Beta Note",
	"Beta Melody",
	"Death (Solo)",
	"Death (Beta)",
	"Death (Multiplayer)"
};

// LOOKUP TABLE (LUT): Maps UI Index directly to native N64 ID
static const s32 g_MusicTrackIDs[] = {
	// --- SYSTEM / MENUS / GENERAL ---
	0,   // None (MUSIC_NONE)
	107, // Title 1 (MUSIC_TITLE1)
	1,   // Title 2 (MUSIC_TITLE2)
	89,  // Main Menu (MUSIC_MAINMENU)
	3,   // Pause Menu (MUSIC_PAUSEMENU)
	72,  // Combat Sim Menu (MUSIC_COMBATSIM_MENU)
	13,  // Carrington Institute (MUSIC_CI)
	88,  // Credits (MUSIC_CREDITS)
	73,  // Mission Success (MUSIC_MISSION_SUCCESS)
	103, // Combat Sim End (MUSIC_COMBATSIM_COMPLETE)
	71,  // Mission Failed (MUSIC_MISSION_FAILED)

	// --- CAMPAIGN LEVELS (STANDARD / X PAIRS) ---
	9,   // Defection (MUSIC_DEFECTION)
	16,  // Defection X (MUSIC_DEFECTION_X)
	18,  // Investigation (MUSIC_INVESTIGATION)
	19,  // Investigation X (MUSIC_INVESTIGATION_X)
	2,   // Extraction (MUSIC_EXTRACTION)
	17,  // Extraction X (MUSIC_EXTRACTION_X)
	12,  // Villa (MUSIC_VILLA)
	39,  // Villa X (MUSIC_VILLA_X)
	14,  // Chicago (MUSIC_CHICAGO)
	40,  // Chicago X (MUSIC_CHICAGO_X)
	15,  // G5 (MUSIC_G5)
	41,  // G5 X (MUSIC_G5_X)
	20,  // Infiltration (MUSIC_INFILTRATION)
	42,  // Infiltration X (MUSIC_INFILTRATION_X)
	22,  // Rescue (MUSIC_RESCUE)
	50,  // Rescue X (MUSIC_RESCUE_X)
	6,   // Escape (MUSIC_ESCAPE)
	51,  // Escape X (MUSIC_ESCAPE_X)
	23,  // Airbase (MUSIC_AIRBASE)
	52,  // Airbase X (MUSIC_AIRBASE_X)
	24,  // Air Force One (MUSIC_AIRFORCEONE)
	53,  // Air Force One X (MUSIC_AIRFORCEONE_X)
	29,  // Crash Site (MUSIC_CRASHSITE)
	30,  // Crash Site X (MUSIC_CRASHSITE_X)
	28,  // Pelagic (MUSIC_PELAGIC)
	54,  // Pelagic X (MUSIC_PELAGIC_X)
	7,   // Deep Sea (MUSIC_DEEPSEA)
	55,  // Deep Sea X (MUSIC_DEEPSEA_X)
	4,   // Defense (MUSIC_DEFENSE)
	36,  // Defense X (MUSIC_DEFENSE_X)
	31,  // Attack Ship (MUSIC_ATTACKSHIP)
	32,  // Attack Ship X (MUSIC_ATTACKSHIP_X)
	33,  // Skedar Ruins (MUSIC_SKEDARRUINS)
	56,  // Skedar Ruins X (MUSIC_SKEDARRUINS_X)
	100, // Skedar Leader (MUSIC_SKEDARRUINS_KING)

	// --- MULTIPLAYER SPECIFIC THEMES ---
	58,  // Dark Combat (MUSIC_DARK_COMBAT)
	59,  // Skedar Mystery (MUSIC_SKEDAR_MYSTERY)
	61,  // CI Operative (MUSIC_CI_OPERATIVE)
	62,  // dataDyne Action (MUSIC_DATADYNE_ACTION)
	63,  // Maian Tears (MUSIC_MAIAN_TEARS)
	64,  // Alien Conflict (MUSIC_ALIEN_CONFLICT)

	// --- INTROS, OUTROS, & CUTSCENES ---
	101, // CI Training (MUSIC_CI_TRAINING)
	108, // CI Intro (MUSIC_CI_INTRO)
	34,  // Defection Intro (MUSIC_DEFECTION_INTRO)
	35,  // Defection Outro (MUSIC_DEFECTION_OUTRO)
	37,  // Investigation Intro (MUSIC_INVESTIGATION_INTRO)
	38,  // Investigation Outro (MUSIC_INVESTIGATION_OUTRO)
	47,  // Chicago Intro (MUSIC_CHICAGO_INTRO)
	43,  // Chicago Outro (MUSIC_CHICAGO_OUTRO)
	45,  // Extraction Intro (MUSIC_EXTRACTION_INTRO)
	44,  // Extraction Outro (MUSIC_EXTRACTION_OUTRO)
	48,  // Villa Intro 1 (MUSIC_VILLA_INTRO1)
	67,  // Villa Intro 2 (MUSIC_VILLA_INTRO2)
	68,  // Villa Intro 3 (MUSIC_VILLA_INTRO3)
	99,  // Villa Outro (MUSIC_VILLA_OUTRO)
	46,  // G5 Intro (MUSIC_G5_INTRO)
	69,  // G5 Outro (MUSIC_G5_OUTRO)
	70,  // G5 Mid-Cutscene (MUSIC_G5_MIDCUTSCENE)
	49,  // Infiltration Intro (MUSIC_INFILTRATION_INTRO)
	83,  // Infiltration Outro (MUSIC_INFILTRATION_OUTRO)
	81,  // Rescue Intro (MUSIC_RESCUE_INTRO)
	66,  // Rescue Outro (MUSIC_RESCUE_OUTRO)
	65,  // Escape Intro (MUSIC_ESCAPE_INTRO)
	80,  // Escape Mid-Cut (MUSIC_ESCAPE_MIDCUTSCENE)
	85,  // Escape Outro Lng (MUSIC_ESCAPE_OUTRO_LONG)
	118, // Escape Outro Sht (MUSIC_ESCAPE_OUTRO_SHORT)
	75,  // Air Base Intro (MUSIC_AIRBASE_INTRO)
	96,  // Air Base Outro (MUSIC_AIRBASE_OUTRO)
	57,  // Air Base Outro Lng (MUSIC_AIRBASE_OUTRO_LONG)
	78,  // AFO Intro (MUSIC_AIRFORCEONE_INTRO)
	91,  // AFO Mid-Cutscene (MUSIC_AIRFORCEONE_MIDCUTSCENE)
	93,  // AFO Outro (MUSIC_AIRFORCEONE_OUTRO)
	74,  // Crash Site Intro (MUSIC_CRASHSITE_INTRO)
	87,  // Crash Site Outro (MUSIC_CRASHSITE_OUTRO)
	84,  // Pelagic Intro (MUSIC_PELAGIC_INTRO)
	92,  // Pelagic Outro (MUSIC_PELAGIC_OUTRO)
	82,  // Deep Sea Intro (MUSIC_DEEPSEA_INTRO)
	77,  // Deep Sea Mid-Cut (MUSIC_DEEPSEA_MIDCUTSCENE)
	90,  // Deep Sea Outro (MUSIC_DEEPSEA_OUTRO)
	86,  // Defense Intro (MUSIC_DEFENSE_INTRO)
	97,  // Defense Outro (MUSIC_DEFENSE_OUTRO)
	76,  // Attack Ship Intro (MUSIC_ATTACKSHIP_INTRO)
	79,  // Attack Ship Outro (MUSIC_ATTACKSHIP_OUTRO)
	94,  // Skedar Ruins Intro (MUSIC_SKEDARRUINS_INTRO)
	98,  // Skedar Ruins Outro (MUSIC_SKEDARRUINS_OUTRO)
	27,  // Mission Unknown (MUSIC_MISSION_UNKNOWN)

	// --- SOUND EFFECTS & AMBIENT WINDS ---
	5,   // Investigation SFX (MUSIC_INVESTIGATION_SFX)
	8,   // dD Tower SFX (MUSIC_DDTOWER_SFX)
	11,  // Defection Intro SFX (MUSIC_DEFECTION_INTRO_SFX)
	26,  // Extraction Outro SFX (MUSIC_EXTRACTION_OUTRO_SFX)
	109, // Infiltration SFX (MUSIC_INFILTRATION_SFX)
	110, // Deep Sea SFX (MUSIC_DEEPSEA_SFX)
	111, // AFO SFX (MUSIC_AIRFORCEONE_SFX)
	112, // Attack Ship SFX (MUSIC_ATTACKSHIP_SFX)
	114, // Escape Outro SFX (MUSIC_ESCAPE_OUTRO_SFX)
	115, // A51 Loudspeaker 1 (MUSIC_A51_LOUDSPEAKER1)
	116, // A51 Loudspeaker 2 (MUSIC_A51_LOUDSPEAKER2)
	102, // Crash Site Wind (MUSIC_CRASHSITE_WIND)
	104, // Ocean Ambience (MUSIC_OCEAN)
	105, // Wind Ambience (MUSIC_WIND)
	106, // Traffic Ambience (MUSIC_TRAFFIC)
	113, // Skedar Wind (MUSIC_SKEDAR_WIND)

	// --- BETA, DEATH, & UNUSED TUNES ---
	60,  // Deep Sea Beta (MUSIC_DEEPSEA_BETA)
	95,  // Beta Note (MUSIC_BETA_NOTE)
	117, // Beta Melody (MUSIC_BETA_MELODY)
	10,  // Death (Solo) (MUSIC_DEATH_SOLO)
	21,  // Death (Beta) (MUSIC_DEATH_BETA)
	25   // Death (Multiplayer) (MUSIC_DEATH_MP)
};

#endif // EXT_AUDIO_TRACKS_H