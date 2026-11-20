#pragma once

#include "Offset.h"

DefOffset(OFFSET_HALOREACH_PF_ENGINE, 0x34818)
DefOffset(OFFSET_HALOREACH_PF_WORLD, 0x575AC)
DefOffset(OFFSET_HALOREACH_PF_RENDER, 0xC33F8)//0xC3324

DefOffset(OFFSET_HALOREACH_PF_COOP_JOIN, 0x397354)//0x3971C4
DefOffset(OFFSET_HALOREACH_PF_COOP_REJOIN, 0x394A2C)//0x39489C

DefOffset(OFFSET_HALOREACH_PF_ADD_LOCAL_PLAYER, 0x43DC8)

DefOffset(OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT, 0xC30D0)
DefOffset(OFFSET_HALOREACH_PF_SPLITSCREEN_RESOLUTION_RESOURCE, 0x2CA86C)
DefOffset(OFFSET_HALOREACH_PF_DRAW_SPLITSCREEN_BLACK_BARS, 0x2C6D84)
DefOffset(OFFSET_HALOREACH_PF_UPDATE_PLAYER_HUD_VIEW, 0x2D94BC)
DefOffset(OFFSET_HALOREACH_PF_SELECT_HUD_LAYOUT_SUBRECORD, 0x2D91BC)
DefOffset(OFFSET_HALOREACH_PF_HUD_ANCHOR, 0x2D846C)
DefOffset(OFFSET_HALOREACH_PF_PROJECT_HUD_MARKER, 0x2E1430)
DefOffset(OFFSET_HALOREACH_PF_CAMERA_BASIS, 0x2884BC)
DefOffset(OFFSET_HALOREACH_PF_CAMERA_ASPECT_RECT, 0x287F58)
DefOffset(OFFSET_HALOREACH_PF_COMPUTE_VIEWPORT_RECT, 0x287C5C)
DefOffset(OFFSET_HALOREACH_PF_IS_PLAYER_SLOT_VALID, 0x53F98)
DefOffset(OFFSET_HALOREACH_PF_ASSIGN_PLAYER_SLOT, 0x53D7C)
DefOffset(OFFSET_HALOREACH_PF_FIND_PLAYER_SLOT, 0xC3140)
DefOffset(OFFSET_HALOREACH_PF_UPDATE_LOOK_BLEND, 0xC777C)
DefOffset(OFFSET_HALOREACH_PF_GET_CONTROLLER_RECORD, 0x23F70)
DefOffset(OFFSET_HALOREACH_DAT_SHARED_INPUT_FLAG, 0x2DFBE84)
DefOffset(OFFSET_HALOREACH_PF_SET_SHARED_INPUT_FLAG, 0x5C980)
DefOffset(OFFSET_HALOREACH_DAT_TLS_INDEX, 0xC17B18)
DefOffset(OFFSET_HALOREACH_DAT_CURRENT_HUD_PLAYER, 0xD1F71C)
DefOffset(OFFSET_HALOREACH_DAT_CURRENT_HUD_RESOLUTION, 0xD1F710)
DefOffset(OFFSET_HALOREACH_DAT_TAG_SEGMENT_TABLE, 0x4E39F20)
DefOffset(OFFSET_HALOREACH_PF_APPLY_RAW_INPUT, 0xC88C8)
// Native FOV baseline seam (docs/REVERSE_ENGINEERING.md, "Halo Reach split-screen FOV").
// APPLY_RAW_INPUT above is the same function the observer loop calls first
// per slot (it copies the director camera command into the slot record).
DefOffset(OFFSET_HALOREACH_PF_GET_FOV_BASELINE, 0xC8554)
DefOffset(OFFSET_HALOREACH_PF_DIRECTOR_UPDATE, 0xC4458)
DefOffset(OFFSET_HALOREACH_PF_DIRECTOR_SLOT_PREUPDATE, 0xC5040)
DefOffset(OFFSET_HALOREACH_PF_OBSERVER_UPDATE, 0xC7F98)
DefOffset(OFFSET_HALOREACH_PF_UPDATE_PLAYER_FRAME, 0x26C204)
DefOffset(OFFSET_HALOREACH_PF_BUILD_VIEW_MATRICES, 0x28AF8C)
DefOffset(OFFSET_HALOREACH_PF_UPDATE_PLAYER_FRAME_INNER, 0x26C6DC)
DefOffset(OFFSET_HALOREACH_PF_RT_POOL_INIT, 0x266F90)
DefOffset(OFFSET_HALOREACH_PF_RT_POOL_RELEASE, 0x2670FC)
DefOffset(OFFSET_HALOREACH_DAT_RT_SCRATCH_DEPTH, 0x4E38CA8)
DefOffset(OFFSET_HALOREACH_PF_RT_DESC_PATCH, 0x266D60)
DefOffset(OFFSET_HALOREACH_PF_RT_REQUEST, 0x2D4220)
DefOffset(OFFSET_HALOREACH_PF_RT_COMPUTE_SIZE, 0x2663B8)
DefOffset(OFFSET_HALOREACH_PF_RT_CREATE, 0x2669E8)
DefOffset(OFFSET_HALOREACH_PF_UPLOAD_CONSTANT, 0x271200)
DefOffset(OFFSET_HALOREACH_PF_LOADOUT_TEMPLATE_RESOLVE,  0x2EEB94)
DefOffset(OFFSET_HALOREACH_PF_LOADOUT_TEMPLATE_RESOLVE2, 0x2EEC90)
DefOffset(OFFSET_HALOREACH_PF_LOADOUT_BUILD, 0x2C9E00)
// Generic per-window CUI draw (FUN_1802aab24). Per-player call site is
// UpdatePlayerFrame 0x26CEE4; windows 4/5 come from 0x26FCE5 / 0x26FACD.
DefOffset(OFFSET_HALOREACH_PF_CUI_WINDOW_DRAW, 0x2AAB24)

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
DefOffset(OFFSET_HALOREACH_PF_APPLY_RENDER_THROTTLE,       0x270258)
// The selector's own CALL GetSplitscreenPlayerCount, and the production seam:
// on this build the site is E8 6D 2E E5 FF (CALL 0xC30D0), so B8 01 00 00 00
// (MOV EAX, 1) is an exact 5-byte replacement that pins the record index to the
// 1-player entry. Build-specific, and CPatch does not verify the original bytes
// before writing - same as every other entry in the embed patch list.
DefOffset(OFFSET_HALOREACH_PF_RENDER_THROTTLE_COUNT_CALL,  0x27025E)
DefOffset(OFFSET_HALOREACH_DAT_RENDER_THROTTLE_LIVE,       0xCA0240)   // 0x50 bytes
DefOffset(OFFSET_HALOREACH_DAT_RENDER_THROTTLE_TABLE,      0xB43E40)   // 4 x 0x50, index = count-1
DefOffset(OFFSET_HALOREACH_DAT_RENDER_THROTTLE_USE_STATIC, 0x4E389B0)  // byte, !=0 forces STATIC_TABLE
DefOffset(OFFSET_HALOREACH_DAT_SCENARIO_GLOBALS,           0xC1A230)   // ptr; tagref at +0x724
DefOffset(OFFSET_HALOREACH_DAT_ENGINE_GLOBALS,             0xC1A238)   // ptr; tagref at +0x554
DefOffset(OFFSET_HALOREACH_DAT_TAG_INDEX_TABLE,            0xC1A600)   // ptr; 8-byte entries, handle at +4

// MCC quality-tier post-process. The throttle tail is its only caller: it
// rewrites the live block in place as clamp(field * tierMultiplier, lo, hi),
// the multiplier chosen per category from the MCC graphics setting bytes at
// 0x29F5909-0x29F5912. Runs only when the gate pointer is non-null.
// Signature: void(unused, const void* src, void* dst) - src and dst are the
// same buffer at the native call site.
DefOffset(OFFSET_HALOREACH_PF_APPLY_QUALITY_TIER,          0x3AEF8)
DefOffset(OFFSET_HALOREACH_DAT_QUALITY_TIER_GATE,          0xC1A100)

// The MCC graphics setting bytes the tier pass reads: 10 bytes,
// 0x29F5909..0x29F5912. Per category 0 = low/off (and ORs feature-disable bits
// into the live flags word), 2 = high, anything else = x1.0 passthrough.
// Read-only in this DLL - no writer exists here; the MCC host pushes them in.
//   +0 aniso  +1 lighting  +2 effects  +3 shadows  +4 detail/LOD
//   +5 (flags 0x28 only)   +6 water    +7 (unused by the tier pass)
//   +8 (flags 0x400)       +9 (flags 0x800)
DefOffset(OFFSET_HALOREACH_DAT_MCC_QUALITY_SETTINGS,       0x29F5909)
#define OFFSET_HALOREACH_V_MCC_QUALITY_SETTING_COUNT    10

// Every skinned object drawn in a frame (any view, shadows too) gets its bone
// matrices from one pool of 0x35C00 bytes (nodes x 0x30 + 0x48 each), reset
// every frame. The allocator (0x25093C: mov ecx, 0x35C00 ... shl r10d, 0x12)
// returns frame << 18 | byte offset, or -1 once it's full (the last 0x5000 are
// kept for its flagged callers); users address it with lea reg, [pool] and
// `and reg, 0x3FFFF`. haloreach/skinning.cpp moves it to a bigger buffer.
DefOffset(OFFSET_HALOREACH_DAT_SKINNING_POOL,           0xC51C40)
DefOffset(OFFSET_HALOREACH_PF_SKINNING_POOL_ALLOCATE,   0x25093C)

#define OFFSET_HALOREACH_V_ENTRY_PLAYERS 0x3
#define OFFSET_HALOREACH_V_ENTRY_PLAYERS_ACTION 0x23
#define OFFSET_HALOREACH_V_ENTRY_SPLIT_SCREEN 0x2B

// Split-screen layout (mcc/module/patch/SplitscreenConfigStore, haloreach/blackbars.cpp, loadout.cpp)
DefOffset(OFFSET_HALOREACH_PV_SPLITSCREEN_TABLE, 0xB43C40) // c_splitscreen_config::m_config_table, {x0, y0, x1, y1, variant}[players * 4 + slot]
DefOffset(OFFSET_HALOREACH_PV_SCREEN_SIZE, 0xB43A90) // int width, height of the backbuffer the views are laid out on
DefOffset(OFFSET_HALOREACH_PF_FILL_RECT, 0xD3774) // void (short rect[4] {top, left, bottom, right}, unsigned argb)
DefOffset(OFFSET_HALOREACH_PF_RENDER_SETUP_1, 0x274488) // (int, int) - the bar painter calls it with (0, 1) ...
DefOffset(OFFSET_HALOREACH_PF_RENDER_SETUP_2, 0x2743A4) // (int) - ... and this with 0, before drawing

// Split-screen render targets (haloreach/splitscreen_rt.cpp)
DefOffset(OFFSET_HALOREACH_DAT_RT_DESC_TABLE, 0xBB9230) // the pool's target descriptors, 0x58 each
DefOffset(OFFSET_HALOREACH_DAT_RT_STOCK_FRAC_W, 0xA8AE58) // float 0.805208325: RT_COMPUTE_SIZE's two-player width ...
DefOffset(OFFSET_HALOREACH_DAT_RT_STOCK_FRAC_H, 0xA8AD74) // float 0.5: ... and height (a shared constant, read only)

// CUI canvas scale (haloreach/cui_canvas_scale.cpp)
DefOffset(OFFSET_HALOREACH_DAT_CUI_CANVAS_SCALE, 0xB4BBD8) // float x, y: backbuffer size over the 1152x720 canvas
DefOffset(OFFSET_HALOREACH_DAT_CUI_CANVAS_CACHE_W, 0x4E38C8C) // int backbuffer width the scale was worked out for ...
DefOffset(OFFSET_HALOREACH_DAT_CUI_CANVAS_CACHE_H, 0x4E38C94) // int ... and height
DefOffset(OFFSET_HALOREACH_V_CUI_WINDOW_DRAW_RETURN, 0x26CEE9) // where UpdatePlayerFrame's (per-player) call to CUI_WINDOW_DRAW returns

// HUD layout (haloreach/hud_layout_probe.cpp, hud_anchor.cpp)
DefOffset(OFFSET_HALOREACH_V_SELECT_HUD_LAYOUT_RETURN, 0x2D95DF) // where UpdatePlayerHudView's call to SELECT_HUD_LAYOUT_SUBRECORD returns
DefOffset(OFFSET_HALOREACH_DAT_HUD_VIEW_BOUNDS, 0xD1F6D0) // short y0, x0, y1, x1 of the slot whose HUD is laid out
DefOffset(OFFSET_HALOREACH_PV_HUD_CANVAS, 0xD1F6F0) // float width, height of that slot's HUD virtual canvas
DefOffset(OFFSET_HALOREACH_DAT_HUD_BASIS, 0xD1F740) // float4[5], ending where the next begins
DefOffset(OFFSET_HALOREACH_DAT_HUD_SCREEN_SCALE_AND_OFFSET, 0xD1F790) // chud_screen_scale_and_offset: float xInset, yInset, halfW, halfH

// Per-player HUD (mcc/hud; the player is DAT_CURRENT_HUD_PLAYER, the canvas PV_HUD_CANVAS). Where the per-widget
// get_hud_element_transform call (0x2D93A4, widget anchor -> element) returns. UpdatePlayerHudView already asks for the
// motion sensor (element 1) once per view and bakes the answer into the radar centre the dish and blips draw from, so a
// dish widget asking again here would get it twice.
DefOffset(OFFSET_HALOREACH_V_HUD_WIDGET_TRANSFORM_RETURN, 0x2D9443)

// Dev Tools' resolution/FOV table dump (mcc/module/Module.cpp)
DefOffset(OFFSET_HALOREACH_PF_GET_SPLITSCREEN_SLOT_TOKEN, 0x53EC8) // unsigned (int slot)
DefOffset(OFFSET_HALOREACH_PF_RESOLVE_HUD_PROFILE, 0x2C2F60) // int (unsigned token)
DefOffset(OFFSET_HALOREACH_DAT_PLAYER_VIEW_CONTAINER, 0x4E38C68) // the per-player view container*
DefOffset(OFFSET_HALOREACH_DAT_VIEW_CONTEXT, 0x4E389A8) // void*, SELECT_HUD_LAYOUT_SUBRECORD's context
