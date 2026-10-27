#pragma once

#include "Offset.h"

DefOffset(OFFSET_HALO2_PF_LOAD, 0x4F980)
DefOffset(OFFSET_HALO2_PF_GAME, 0x67A220)
DefOffset(OFFSET_HALO2_PF_COPY_GAME_OPTIONS, 0x39CE0) // copy_game_options(game_options)
DefOffset(OFFSET_HALO2_PF_HS_COMPILE, 0x6E17C0) // compile a console script: (const char* text, bool)

DefOffset(OFFSET_HALO2_PF_ADD_LOCAL_PLAYER, 0x69D2C0)//0x69D290

DefOffset(OFFSET_HALO2_PV_PLAYERS, 0xE80A28)//0xE7FA28
DefOffset(OFFSET_HALO2_PV_RESPAWN, 0xE80A20)//0xE7FA20

DefOffset(OFFSET_HALO2_PF_PLAYER_VALID, 0x6A6C80)//0x6A6C30
DefOffset(OFFSET_HALO2_PF_PLAYER_COUNT1, 0x8940CA)//0x893FDA
DefOffset(OFFSET_HALO2_PF_PLAYER_COUNT2, 0x894127)//0x894037

// The views' hand-over splits the screen only for exactly two local players (cmp eax, 2 / jne, where the call to the
// local player count returns); splitscreen_patch4 makes it any count but one (cmp eax, 1 / je)
DefOffset(OFFSET_HALO2_TWO_PLAYERS_TEST_HAND_OVER, 0x5153E)

// In the main loop's quit handling (0x679CD0): call 0x6A6320 (did this game come from a Halo 2 lobby?), then
// test al, al / je - true loads the main-menu map to go back to that lobby, false quits to MCC.
DefOffset(OFFSET_HALO2_PF_QUIT_TO_LOBBY_TEST, 0x679EE0)

// void (void*) - each tick: in a campaign, a player flagged +6 & 8 spawns at a starting location; the others waiting
// come back through the co-op respawn (0x6A1320: player_spawn, then moved beside a teammate out of combat)
DefOffset(OFFSET_HALO2_PF_PLAYERS_UPDATE, 0x6A3910)
DefOffset(OFFSET_HALO2_PF_COOP_CAMPAIGN, 0x6A6320) // bool () - a co-op campaign: session [0xE80A78] mode +8 == 1, byte +0x2C8
DefOffset(OFFSET_HALO2_PF_COOP_RESPAWN_ALLOWED, 0x6A5D40) // bool () - not with the Iron skull (11), not on Legendary
DefOffset(OFFSET_HALO2_PV_SCENARIO, 0xE6F768) // the loaded scenario*: int starting locations +0x100
DefOffset(OFFSET_HALO2_PF_PLAYER_SPAWN, 0x69E580) // bool (int player) - a new unit at the best starting location (hot join)

// spawning (mcc/spawn)
DefOffset(OFFSET_HALO2_PV_TAG_INSTANCES, 0x15E4B30) // {u32 group, u32 datum, u32 address, u32 size}[tag count]
DefOffset(OFFSET_HALO2_PV_TAG_COUNT, 0x15E4B60)
DefOffset(OFFSET_HALO2_PV_TAG_NAME_OFFSETS, 0x15E4B68) // int32[tag count] into the name buffer
DefOffset(OFFSET_HALO2_PV_TAG_NAMES, 0x15E4B78)
DefOffset(OFFSET_HALO2_PV_TAG_BASE, 0xE80AB0) // tag block address -> map tag data + address
DefOffset(OFFSET_HALO2_PV_SHARED_TAG_BASE, 0xE80AC0) // addresses with the top bit set are in the shared map
// OFFSET_HALO2_PV_PLAYERS (above): data array (data at +*(+0x48)): 0x224 each, unit +0x2C
DefOffset(OFFSET_HALO2_PV_OBJECTS, 0x18B7398) // object headers: data array of 0xC, object data offset +8
DefOffset(OFFSET_HALO2_PV_OBJECT_MEMORY, 0x18B7360) // object data = ((pool + 0x57) & ~0xF) + offset
DefOffset(OFFSET_HALO2_PF_OBJECT_GET_ORIGIN, 0x8D6780) // (int object, real_point3d*)
DefOffset(OFFSET_HALO2_PF_OBJECT_PLACEMENT_DATA_NEW, 0x8D8880) // (data* [0xC4], int tag, int owner, void*)
DefOffset(OFFSET_HALO2_PF_OBJECT_NEW, 0x8D79D0) // int (data*)
DefOffset(OFFSET_HALO2_PF_AI_PLACE, 0x618890) // (int ai_index); starting location = (3 << 30) | (squad << 16) | location
// int (int ai_index, short, int*, float*) - living actors; a squad's ai index is its index. ai_living_count (0x619B10)
// is a 13-byte thunk to it with 0, null, null, too generic to find again by pattern.
DefOffset(OFFSET_HALO2_PF_AI_LIVING_COUNT, 0x6198E0)

// Left/Right split screen (mcc/splitscreen/LeftRight)
DefOffset(OFFSET_HALO2_PF_SPLIT_GRID, 0x7E09D0) // void (int views, int mode, short grid[2] columns, rows) - mode 1 stacks, others side by side
DefOffset(OFFSET_HALO2_PF_SPLIT_CELL, 0x7E0BD0) // void (int view, int views, int mode, short grid[2], short cell[2], short span[2])
DefOffset(OFFSET_HALO2_PF_SPLIT_DIVIDERS, 0x831D90) // void () - paints the black bands between the views, by the frame's mode
DefOffset(OFFSET_HALO2_PV_SPLIT_MODE, 0x165C168) // int - the frame's grid mode, which only the dividers read
DefOffset(OFFSET_HALO2_PV_SPLIT_VIEWS, 0x165C16C) // int - the frame's views
DefOffset(OFFSET_HALO2_PV_ANNIVERSARY, 0xE21280) // int, nonzero while the Anniversary renderer is up
DefOffset(OFFSET_HALO2_PV_CLASSIC_SHOWN, 0x1E8CE73) // bool, the player switched to Classic graphics

// Anniversary graphics (Saber3D) with 3-4 players (module/entry/halo2/anniversary.cpp)
DefOffset(OFFSET_HALO2_PF_CINEMATIC, 0x6F4A20) // bool () - one view (cinematics)
DefOffset(OFFSET_HALO2_PF_LOCAL_PLAYER_COUNT, 0x6A4380) // int () - a 12-byte leaf
// returns from its other "== 2" (split screen) tests (the views' hand-over's is OFFSET_HALO2_TWO_PLAYERS_TEST_HAND_OVER,
// above): the object sync, a screen effect, and three first-person effect callbacks
DefOffset(OFFSET_HALO2_TWO_PLAYERS_TEST_OBJECT_SYNC, 0x6A00C)
DefOffset(OFFSET_HALO2_TWO_PLAYERS_TEST_SCREEN_EFFECT, 0x6F40C)
DefOffset(OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_1, 0x64004)
DefOffset(OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_2, 0x64F4E)
DefOffset(OFFSET_HALO2_TWO_PLAYERS_TEST_FIRST_PERSON_3, 0x65559)
DefOffset(OFFSET_HALO2_PF_SABER_HAND_OVER_VIEW, 0x5F510) // void (int view, bool split) - observer camera(view) -> cams[view]
DefOffset(OFFSET_HALO2_PV_SABER_CAMERAS, 0x1E91210) // container*: camera*[] at +0x100, int count +0x108
DefOffset(OFFSET_HALO2_PF_SABER_CAMERA_SET_FOV, 0xBC560) // void (camera*, float vertical degrees) - also the projection
DefOffset(OFFSET_HALO2_PV_SABER_DEVICE, 0x1E8D418) // device*, settings* at +0x118: out w +0x10, h +0x14, pixel aspect +0x1C
DefOffset(OFFSET_HALO2_PV_SABER_CONTEXT, 0x1E8D420) // context*, ID3D11DeviceContext* at +0xD58
DefOffset(OFFSET_HALO2_PV_SABER_SCENE, 0x1A250F8) // scene*, the renderer's camera list at +0x140
DefOffset(OFFSET_HALO2_PF_SABER_BUILD_CAMERA_LIST, 0x2DDCB0) // (renderer*, list*, bool split, -)
DefOffset(OFFSET_HALO2_PF_SABER_LIST_CAMERA, 0x1C7740) // int (list*, camera*, flags, split index, 6 more) - appends a copy
DefOffset(OFFSET_HALO2_PF_SABER_RENDER_FRAME, 0x2DEC00) // void ()
DefOffset(OFFSET_HALO2_PF_SABER_COMPOSITE, 0x1D2A00) // void (texture*, bool split, int index) - a view's image to its half
DefOffset(OFFSET_HALO2_SABER_COMPOSITE_DRAW, 0x1D2C33) // in it, the quad's vertices are at rsp+0xC0 (x, y, z, -, u, v; 0x18 each)
DefOffset(OFFSET_HALO2_PF_SABER_AFTER_CAMERAS, 0x2E3F70) // void () - after the views, before the UI and HUD
DefOffset(OFFSET_HALO2_PF_SABER_HDR_PASS, 0x210BF0) // void (-, -, -, -, -, float, int hdr view, bool, ...) - a view's exposure adaptation, bloom and tonemapping
DefOffset(OFFSET_HALO2_PV_SABER_HDR_VIEWS, 0x1AB8600) // 0x4C per HDR view (4): settings, +0x3C measured luminance, +0x40 exposure, +0x44 adapted luminance, int +0x48 frames
DefOffset(OFFSET_HALO2_PV_SABER_HDR_READ_BACKS, 0x1AB8758) // texture*[4][2] - per HDR view, the luminance read back on alternate frames
// texture* (set*, name, ...9 in all) - adds a render target to a set: count at +0xC, entries at +0x10 (0x30 each:
// texture* +0, flags +8). A split target also gets "_split0" (flags 0x20000000, full width, half height - both views
// are drawn into it in turn) and, for per-view history, "_split1" (0x40000000). A texture's vtable +0xA0 is
// bool (texture*, width, height, mips, format, depth, samples, data) - no-op if unchanged, else remade; fields
// width +0x20, height +0x22, depth +0x24, format +0x26, samples +0x28 (int16), mips +0x2A (byte).
DefOffset(OFFSET_HALO2_PF_SABER_ADD_RENDER_TARGET, 0xF7750)
// void (manager*, int index) - the texture manager deletes its texture `index` (texture*[] at +0x220), children
// first, whatever the texture's reference count (+0x78); a render-target set's teardown (0xF8750) gets here
DefOffset(OFFSET_HALO2_PF_SABER_DELETE_TEXTURE, 0x1A5990)
DefOffset(OFFSET_HALO2_PF_PLAYER_VISIBLE_OBJECTS, 0x608A0) // void (int local) - the window's visible objects, as view 0's (local 0) or view 1's
DefOffset(OFFSET_HALO2_PF_OWN_UNIT_IN_FIRST_PERSON, 0x5FE80) // bool (int object, int local) - the local player's unit, in first person
DefOffset(OFFSET_HALO2_PF_FIRST_PERSON_SYNC, 0x6D0D0) // void () - players 1-2's first-person models, each hidden in the other's view
DefOffset(OFFSET_HALO2_PF_FIRST_PERSON_BUILD, 0x81BFB0) // void (int local)
DefOffset(OFFSET_HALO2_PV_FIRST_PERSON_MODELS, 0x1AB11C0) // entries*, int count at +8; 0x28 each: +8 object**, +0x1C owner, +0x20 active
DefOffset(OFFSET_HALO2_PV_MODEL_LEVELS, 0x1DC36F0) // unsigned char* detail levels, int count at +8 - the current one is the last
DefOffset(OFFSET_HALO2_PF_SABER_OBJECT_SHOW, 0x69C30) // void (object*)
DefOffset(OFFSET_HALO2_PF_SABER_OBJECT_HIDE, 0x69CA0) // void (object*)
// Per-player HUD (mcc/hud). With "HUD anchor: Centered" on a screen wider than 16:9, 0x954DD0 insets the whole-screen
// HUD frame to a centred 16:9 box, which 0x7E0A40 splits among the players.
DefOffset(OFFSET_HALO2_PF_HUD_ASPECT_LOCK, 0x954DF2) // je (74 3C) that skips the centred 16:9 HUD frame when the profile's aspect lock is off
DefOffset(OFFSET_HALO2_PV_HUD_ASPECT_LOCK, 0x197EE40) // bool: the profiles' LockMaxAspectRatio ("HUD anchor: Centered"), the last loaded wins
DefOffset(OFFSET_HALO2_PV_HUD_DRAWING_PLAYER, 0x165C398) // int local player whose HUD is being drawn
DefOffset(OFFSET_HALO2_PV_HUD_VIEW_BOUNDS, 0x165C298) // int16 top, left, bottom, right of that player's HUD (window bounds)
DefOffset(OFFSET_HALO2_PV_HUD_UNIT_SCALE, 0xE14F28) // float: pixels per HUD offset unit (times the element's scale)
