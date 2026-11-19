#pragma once

#include "Offset.h"

DefOffset(OFFSET_HALO1_PF_RENDER, 0x1F05B0)
DefOffset(OFFSET_HALO1_PF_GAME_START, 0x939D0) // engine game_start(self, manager, game_options)

DefOffset(OFFSET_HALO1_PF_4PLAYERS, 0x67492)
DefOffset(OFFSET_HALO1_PF_PAUSE, 0x427978)
DefOffset(OFFSET_HALO1_PF_IDK, 0x85428)

DefOffset(OFFSET_HALO1_PV_PLAYER_COUNT, 0x1B7B910)

// Left/Right split screen (mcc/splitscreen/LeftRight)
DefOffset(OFFSET_HALO1_PF_SPLIT_GRID, 0xAC4108) // void (int views, int* columns, int* rows) - the window grid, rows first
DefOffset(OFFSET_HALO1_PF_SPLIT_WINDOW, 0xAC4154) // void (int view, int views, short rect[4], short copy[4]) - a view's window
DefOffset(OFFSET_HALO1_PF_SPLIT_DIVIDERS, 0xB32238) // void () - paints the black bands between the views
DefOffset(OFFSET_HALO1_PF_SPLIT_VIEWS, 0xB321FC) // int () - views on screen
DefOffset(OFFSET_HALO1_PV_WINDOW_BOUNDS, 0x29E05B4) // int16 top, left, bottom, right of the window being drawn
DefOffset(OFFSET_HALO1_PV_SCREEN_BOUNDS, 0x1B7D3DC) // int16 top, left, bottom, right the windows are laid out on
DefOffset(OFFSET_HALO1_SCOPE_GRID_SET, 0xAC73DC) // in the scope effects' texture transforms: the view's grid place is set
DefOffset(OFFSET_HALO1_PF_FILL_RECT, 0xAC63F4) // void (short rect[4] {top, left, bottom, right}, unsigned argb)
DefOffset(OFFSET_HALO1_PV_ANNIVERSARY_SHOWN, 0x1B7AA84) // int, nonzero while Anniversary graphics are on screen (Back switches it; 0x745B0 is bool () "Classic")

// Anniversary graphics (Saber3D) with 3-4 players (module/entry/halo1/anniversary.cpp)
DefOffset(OFFSET_HALO1_PV_LOCAL_PLAYERS, 0x2EA2D90) // players globals*, int16 local player count at +0xB4
DefOffset(OFFSET_HALO1_PV_RENDER_WINDOWS, 0x2D9CADC) // the game's view windows, 0xAC each: int16 view, rect at +0x84
DefOffset(OFFSET_HALO1_PV_RENDER_CAMERAS, 0x2D9BDD4) // the players' cameras, 0x2A8 each: field of view at +0x38
DefOffset(OFFSET_HALO1_PV_WINDOW_FIELDS_OF_VIEW, 0x1B7D6E8) // float per view, from view -1
DefOffset(OFFSET_HALO1_PF_RENDER_WINDOW_CAMERA, 0xAC450C) // void (window*, camera*) - the window's camera
DefOffset(OFFSET_HALO1_PF_SABER_HAND_OVER_VIEWS, 0x89B70) // void () - each tick: views 0 and 1 to the renderer
DefOffset(OFFSET_HALO1_PF_SABER_HAND_OVER_VIEW, 0xB29268) // void (int view) - a view's camera to the renderer's
DefOffset(OFFSET_HALO1_PV_SABER_CAMERAS, 0x2B17B90) // camera*[], int count at +8
DefOffset(OFFSET_HALO1_PF_SABER_ADD_CAMERA, 0x4684C0) // int ()
DefOffset(OFFSET_HALO1_PF_SABER_DELETE_CAMERA, 0x2B1EA0) // void (camera*)
DefOffset(OFFSET_HALO1_PF_SABER_CAMERA_SET_FOV, 0x11B020) // void (camera*, float degrees)
DefOffset(OFFSET_HALO1_PV_SABER_SPLIT, 0x2E3B821) // bool, the game's: split screen
DefOffset(OFFSET_HALO1_PF_SABER_SET_SPLIT, 0x4150F0) // void (state*) - adds or drops the second camera as the above changes
DefOffset(OFFSET_HALO1_PF_SABER_SPLIT_LAYOUT, 0x415200) // void (state*, bool split) - the cameras' viewports
DefOffset(OFFSET_HALO1_PV_SABER_SPLIT_STATE, 0x2E3CCA8) // state*, split screen at +0x41
DefOffset(OFFSET_HALO1_PV_SABER_DEVICE, 0x2E3BDD8) // device*, settings* at +0x118: pixel aspect +0x1C, width +0x20, height +0x24
DefOffset(OFFSET_HALO1_PV_SABER_CONTEXT, 0x2E3BDE0) // context*, vt+0x190 copies a region
DefOffset(OFFSET_HALO1_PV_SABER_RENDERER, 0x1C33E30) // renderer*, cameras changed at +0x92
DefOffset(OFFSET_HALO1_PV_SABER_SCENE, 0x1BEA9E0) // scene*, the frame's camera list at +0xB0
DefOffset(OFFSET_HALO1_PF_SABER_PREPARE_FRAME, 0x455170) // void (scene renderer*)
DefOffset(OFFSET_HALO1_PF_SABER_BUILD_CAMERA_LIST, 0x4547E0) // (-, list*, bool split, -)
DefOffset(OFFSET_HALO1_PF_SABER_LIST_CAMERA, 0x2EA720) // int (list*, camera*, flags, split index, 12 more) - appends a copy
DefOffset(OFFSET_HALO1_PV_SABER_ZOOMED_FOV, 0x1944B70) // float, a view below this field of view is zoomed
DefOffset(OFFSET_HALO1_PF_SABER_RENDER_FRAME, 0x455A10)
DefOffset(OFFSET_HALO1_PF_SABER_COMPOSITE, 0x45E2B0) // void (int list index) - copies the view's image to its half
DefOffset(OFFSET_HALO1_PV_SABER_VIEW_IMAGES, 0x2D62890) // texture*[2], current index at 0x1B7B11C
DefOffset(OFFSET_HALO1_PV_SABER_VIEW_IMAGE_INDEX, 0x1B7B11C)
DefOffset(OFFSET_HALO1_PV_SABER_SCREEN_IMAGE, 0x2E3D0D0) // texture*, the frame's full-screen image
DefOffset(OFFSET_HALO1_PF_SABER_TEXTURE_CREATE_CHILD, 0x1F9D20) // texture* (parent*, name, width, height, -, flags)
// void (manager*, int index) - the texture manager deletes its texture `index` (texture*[] at +0x1B8): its split
// children (+0xA8, +0xB0) first, then the texture itself; a render target set's release (0x20B000) gets here
DefOffset(OFFSET_HALO1_PF_SABER_DELETE_TEXTURE, 0x1F6BF0)
DefOffset(OFFSET_HALO1_PF_SABER_CLASSIC_HUD, 0x740B0) // void () - the classic HUD over the image: views 0 and 1, then view -1
DefOffset(OFFSET_HALO1_PF_HUD_VIEW, 0xB29438) // void (int view) - the HUD's view (window, drawing player)
DefOffset(OFFSET_HALO1_PF_HUD_VIEW_DRAW, 0xBABD38) // void (int view, state*) - the view's HUD, state 0x29E07C0
DefOffset(OFFSET_HALO1_PV_HUD_VIEW_DRAW_STATE, 0x29E07C0)
DefOffset(OFFSET_HALO1_PF_HUD_VIEW_DRAW_2, 0xB31D88) // void () - then these two
DefOffset(OFFSET_HALO1_PF_HUD_VIEW_DRAW_3, 0xAC8844) // void () - skipped while 0x2EA0218 and 0x2E3B450 are set
DefOffset(OFFSET_HALO1_PV_HUD_VIEW_DRAW_3_SKIP_1, 0x2EA0218) // int
DefOffset(OFFSET_HALO1_PV_HUD_VIEW_DRAW_3_SKIP_2, 0x2E3B450) // bool
DefOffset(OFFSET_HALO1_PF_SABER_VIEW_OVERLAY, 0x4512D0) // void (-, camera*, int split index) - a view's screen effects
DefOffset(OFFSET_HALO1_SABER_VIEW_OVERLAY_RECT, 0x4513AC) // in it, its rect is set: left r11d, top r9d, right r10d, bottom eax
DefOffset(OFFSET_HALO1_PF_SABER_SYNC, 0x89F00) // void () - each frame: the game's state to the renderer
DefOffset(OFFSET_HALO1_PF_SABER_SYNC_OBJECT, 0x7D350) // void (record*) - an object's, record {int16 index, +8 body**, +0x10 second**}
DefOffset(OFFSET_HALO1_PV_RENDER_OBJECTS, 0x29E0900) // data array*, 0x7C each: object at +0x2C
DefOffset(OFFSET_HALO1_PV_DIRECTORS, 0x2D9B960) // 0xF8 each: +0xC, update at +0x10
DefOffset(OFFSET_HALO1_PF_FIRST_PERSON_DIRECTOR, 0xC5263C) // the first-person director's update
DefOffset(OFFSET_HALO1_PF_LOCAL_PLAYER_UNIT_FLAG, 0xAB16A4) // int (int local) - the local player's unit has flag 0x1D4 & 2
DefOffset(OFFSET_HALO1_PF_FIRST_PERSON_PREPARE, 0xB27510) // void () - for the HUD's view, then
DefOffset(OFFSET_HALO1_PF_FIRST_PERSON_UPDATE, 0xB275B8) // void () - its first-person weapon, for the renderer while
DefOffset(OFFSET_HALO1_PV_FIRST_PERSON_FOR_RENDERER, 0x2EA32F8) // int
DefOffset(OFFSET_HALO1_PV_FIRST_PERSON_WEAPONS, 0x1B7AA88) // int[4] per view: the object whose nodes are at 0x1C384A0, 0xD00 per view
DefOffset(OFFSET_HALO1_PV_FIRST_PERSON_ARMS, 0x1B7AA98) // int[4], nodes at 0x1C350A0
DefOffset(OFFSET_HALO1_PF_FIRST_PERSON_SYNC, 0x7AC60) // void (int object, int slot) - a view's first-person weapon or arms (the nodes of the view it's in above) to the renderer's model for (slot, object), made on first use
DefOffset(OFFSET_HALO1_FIRST_PERSON_SYNC_SLOT_LIMIT, 0x7AE64) // in it: cmp r15d, 1 / ja - a slot past 1 gets no nodes
DefOffset(OFFSET_HALO1_PV_FIRST_PERSON_MODELS, 0x2B050E8) // the renderer's first-person models: entry*, int count at +8; entry 0x20: int slot, int object, bytes +0x1C/+0x1E hidden in view 0/1 (wanted), +0x1D/+0x1F (applied)
DefOffset(OFFSET_HALO1_PF_OBJECT_HIDDEN_VIEWS, 0xAB2890) // int (int object) - views (bits 0, 1) the object is hidden in
DefOffset(OFFSET_HALO1_PF_DIRECTOR_CAMERA_MODE, 0xB14EA4) // short (int local) - 0 in first person
DefOffset(OFFSET_HALO1_PV_HIDDEN_OBJECT_CAMERA, 0x1B87A20) // bool on, int16 mode at +2 (2 hides), int object at +0x34
