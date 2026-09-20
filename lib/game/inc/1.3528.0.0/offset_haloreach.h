#pragma once

#define OFFSET_HALOREACH_PF_ENGINE 0x34818
#define OFFSET_HALOREACH_PF_WORLD 0x575AC
#define OFFSET_HALOREACH_PF_RENDER 0xC33F8//0xC3324

#define OFFSET_HALOREACH_PF_COOP_JOIN 0x397354//0x3971C4
#define OFFSET_HALOREACH_PF_COOP_REJOIN 0x394A2C//0x39489C

#define OFFSET_HALOREACH_PF_ADD_LOCAL_PLAYER 0x43DC8

#define OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT 0xC30D0
#define OFFSET_HALOREACH_PF_SPLITSCREEN_RESOLUTION_RESOURCE 0x2CA86C
#define OFFSET_HALOREACH_PF_DRAW_SPLITSCREEN_BLACK_BARS 0x2C6D84
#define OFFSET_HALOREACH_PF_UPDATE_PLAYER_HUD_VIEW 0x2D94BC
#define OFFSET_HALOREACH_PF_SELECT_HUD_LAYOUT_SUBRECORD 0x2D91BC
#define OFFSET_HALOREACH_PF_HUD_ANCHOR 0x2D846C
#define OFFSET_HALOREACH_PF_PROJECT_HUD_MARKER 0x2E1430
#define OFFSET_HALOREACH_PF_CAMERA_BASIS 0x2884BC
#define OFFSET_HALOREACH_PF_CAMERA_ASPECT_RECT 0x287F58
#define OFFSET_HALOREACH_PF_COMPUTE_VIEWPORT_RECT 0x287C5C
#define OFFSET_HALOREACH_PF_IS_PLAYER_SLOT_VALID 0x53F98
#define OFFSET_HALOREACH_PF_ASSIGN_PLAYER_SLOT 0x53D7C
#define OFFSET_HALOREACH_PF_FIND_PLAYER_SLOT 0xC3140
#define OFFSET_HALOREACH_PF_UPDATE_LOOK_BLEND 0xC777C
#define OFFSET_HALOREACH_PF_GET_CONTROLLER_RECORD 0x23F70
#define OFFSET_HALOREACH_DAT_SHARED_INPUT_FLAG 0x2DFBE84
#define OFFSET_HALOREACH_PF_SET_SHARED_INPUT_FLAG 0x5C980
#define OFFSET_HALOREACH_DAT_TLS_INDEX 0xC17B18
#define OFFSET_HALOREACH_DAT_CURRENT_HUD_PLAYER 0xD1F71C
#define OFFSET_HALOREACH_DAT_CURRENT_HUD_RESOLUTION 0xD1F710
#define OFFSET_HALOREACH_DAT_TAG_SEGMENT_TABLE 0x4E39F20
#define OFFSET_HALOREACH_PF_APPLY_RAW_INPUT 0xC88C8
// Native FOV baseline seam (docs/REVERSE_ENGINEERING.md, "Halo Reach split-screen FOV").
// APPLY_RAW_INPUT above is the same function the observer loop calls first
// per slot (it copies the director camera command into the slot record).
#define OFFSET_HALOREACH_PF_GET_FOV_BASELINE 0xC8554
#define OFFSET_HALOREACH_PF_DIRECTOR_UPDATE 0xC4458
#define OFFSET_HALOREACH_PF_DIRECTOR_SLOT_PREUPDATE 0xC5040
#define OFFSET_HALOREACH_PF_OBSERVER_UPDATE 0xC7F98
#define OFFSET_HALOREACH_PF_UPDATE_PLAYER_FRAME 0x26C204
#define OFFSET_HALOREACH_PF_BUILD_VIEW_MATRICES 0x28AF8C
#define OFFSET_HALOREACH_PF_UPDATE_PLAYER_FRAME_INNER 0x26C6DC
#define OFFSET_HALOREACH_PF_RT_POOL_INIT 0x266F90
#define OFFSET_HALOREACH_PF_RT_POOL_RELEASE 0x2670FC
#define OFFSET_HALOREACH_DAT_RT_SCRATCH_DEPTH 0x4E38CA8
#define OFFSET_HALOREACH_PF_RT_DESC_PATCH 0x266D60
#define OFFSET_HALOREACH_PF_RT_REQUEST 0x2D4220
#define OFFSET_HALOREACH_PF_RT_COMPUTE_SIZE 0x2663B8
#define OFFSET_HALOREACH_PF_RT_CREATE 0x2669E8
#define OFFSET_HALOREACH_PF_UPLOAD_CONSTANT 0x271200
#define OFFSET_HALOREACH_PF_LOADOUT_TEMPLATE_RESOLVE  0x2EEB94
#define OFFSET_HALOREACH_PF_LOADOUT_TEMPLATE_RESOLVE2 0x2EEC90
#define OFFSET_HALOREACH_PF_LOADOUT_BUILD 0x2C9E00
// Generic per-window CUI draw (FUN_1802aab24). Per-player call site is
// UpdatePlayerFrame 0x26CEE4; windows 4/5 come from 0x26FCE5 / 0x26FACD.
#define OFFSET_HALOREACH_PF_CUI_WINDOW_DRAW 0x2AAB24

// Split-screen render-quality throttle (FUN_180270258). Called once per frame
// from UpdateAllPlayerViews at 0xC358E. Selects one 0x50-byte record by
// GetSplitscreenPlayerCount() - index (count-1) - and copies it to the live
// block, which 0x270349 is the only writer of in the whole DLL.
//
// Source preference inside the selector:
//   USE_STATIC != 0                          -> STATIC_TABLE
//   [SCENARIO_GLOBALS]+0x724 is a valid tagref -> that tag's block
//   else [ENGINE_GLOBALS]+0x554 -> body +0x64 -> that tag's block
//   any of those missing, or block shorter than the player count -> STATIC_TABLE
#define OFFSET_HALOREACH_PF_APPLY_RENDER_THROTTLE       0x270258
// The selector's own CALL GetSplitscreenPlayerCount, and the production seam:
// on this build the site is E8 6D 2E E5 FF (CALL 0xC30D0), so B8 01 00 00 00
// (MOV EAX, 1) is an exact 5-byte replacement that pins the record index to the
// 1-player entry. Build-specific, and CPatch does not verify the original bytes
// before writing - same as every other entry in the embed patch list.
#define OFFSET_HALOREACH_PF_RENDER_THROTTLE_COUNT_CALL  0x27025E
#define OFFSET_HALOREACH_DAT_RENDER_THROTTLE_LIVE       0xCA0240   // 0x50 bytes
#define OFFSET_HALOREACH_DAT_RENDER_THROTTLE_TABLE      0xB43E40   // 4 x 0x50, index = count-1
#define OFFSET_HALOREACH_DAT_RENDER_THROTTLE_USE_STATIC 0x4E389B0  // byte, !=0 forces STATIC_TABLE
#define OFFSET_HALOREACH_DAT_SCENARIO_GLOBALS           0xC1A230   // ptr; tagref at +0x724
#define OFFSET_HALOREACH_DAT_ENGINE_GLOBALS             0xC1A238   // ptr; tagref at +0x554
#define OFFSET_HALOREACH_DAT_TAG_INDEX_TABLE            0xC1A600   // ptr; 8-byte entries, handle at +4

// MCC quality-tier post-process. The throttle tail is its only caller: it
// rewrites the live block in place as clamp(field * tierMultiplier, lo, hi),
// the multiplier chosen per category from the MCC graphics setting bytes at
// 0x29F5909-0x29F5912. Runs only when the gate pointer is non-null.
// Signature: void(unused, const void* src, void* dst) - src and dst are the
// same buffer at the native call site.
#define OFFSET_HALOREACH_PF_APPLY_QUALITY_TIER          0x3AEF8
#define OFFSET_HALOREACH_DAT_QUALITY_TIER_GATE          0xC1A100

// The MCC graphics setting bytes the tier pass reads: 10 bytes,
// 0x29F5909..0x29F5912. Per category 0 = low/off (and ORs feature-disable bits
// into the live flags word), 2 = high, anything else = x1.0 passthrough.
// Read-only in this DLL - no writer exists here; the MCC host pushes them in.
//   +0 aniso  +1 lighting  +2 effects  +3 shadows  +4 detail/LOD
//   +5 (flags 0x28 only)   +6 water    +7 (unused by the tier pass)
//   +8 (flags 0x400)       +9 (flags 0x800)
#define OFFSET_HALOREACH_DAT_MCC_QUALITY_SETTINGS       0x29F5909
#define OFFSET_HALOREACH_V_MCC_QUALITY_SETTING_COUNT    10

#define OFFSET_HALOREACH_V_ENTRY_PLAYERS 0x3
#define OFFSET_HALOREACH_V_ENTRY_PLAYERS_ACTION 0x23
#define OFFSET_HALOREACH_V_ENTRY_SPLIT_SCREEN 0x2B
