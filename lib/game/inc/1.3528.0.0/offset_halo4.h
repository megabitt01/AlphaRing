#pragma once

#include "Offset.h"


DefOffset(OFFSET_HALO4_PF_ENGINE, 0x616A8)
DefOffset(OFFSET_HALO4_PF_WORLD, 0x9A51C)//0x9A508
DefOffset(OFFSET_HALO4_PF_RENDER, 0x12259C)//0x12251C

DefOffset(OFFSET_HALO4_PF_COOP_JOIN, 0x56671C)//0x566794
DefOffset(OFFSET_HALO4_PF_COOP_REJOIN, 0x4E311B)//0x4E301B
DefOffset(OFFSET_HALO4_PF_COOP_PLAYER_LIMIT, 0x3F5D7)

DefOffset(OFFSET_HALO4_PF_ADD_LOCAL_PLAYER, 0x7D948)

#define OFFSET_HALO4_V_ENTRY_PLAYERS 0x1
#define OFFSET_HALO4_V_ENTRY_PLAYERS_ACTION 0x22

// c_splitscreen_config::m_config_table; the black-bar patches rewrite entries 8, 9 and 12 (0xE84E50, 0xE84E64, 0xE84EA0)
DefOffset(OFFSET_HALO4_PV_SPLITSCREEN_TABLE, 0xE84DB0)//0xE84DA0

// Left/Right split screen and each view's title-safe box (mcc/splitscreen/LeftRight); see offset_halo3.h for the signatures
DefOffset(OFFSET_HALO4_PV_SCREEN_SIZE, 0xE84608)
DefOffset(OFFSET_HALO4_PF_SPLITSCREEN_PLAYER_COUNT, 0x122188)
DefOffset(OFFSET_HALO4_PF_DRAW_SPLITSCREEN_BARS, 0x3C66D4)
DefOffset(OFFSET_HALO4_PF_FILL_RECT, 0x12D694)
DefOffset(OFFSET_HALO4_PF_RENDER_SETUP_1, 0x34D224) // (int, int) - the bar painter calls it with (0, 1) ...
DefOffset(OFFSET_HALO4_PF_RENDER_SETUP_2, 0x34D14C) // (int) - ... and this with 0, before drawing
DefOffset(OFFSET_HALO4_PF_RT_VARIANT_SIZE, 0x37E8B4) // (descriptor*, int* w, int* h, int* w2, int* h2, int* face, int index)
DefOffset(OFFSET_HALO4_PF_RT_POOL_RELEASE, 0x37F610)
DefOffset(OFFSET_HALO4_PF_RT_POOL_INIT, 0x37F3A8)
DefOffset(OFFSET_HALO4_PF_HUD_LAYOUT, 0x3BD78C) // int (int user) - the HUD layout by the view's table variant: 0x80076 full, 0x80077 half, 0x80078 quarter
DefOffset(OFFSET_HALO4_PF_COMPUTE_VIEWPORT_RECT, 0x38EE74) // (int slot, int players, short view[4], short box[4]) - a view and its title-safe box, as Reach's
DefOffset(OFFSET_HALO4_VIEWMODEL_ASPECT_DIVIDE, 0x34ED96) // divss xmm5, xmm0 - the first-person camera's field of view, times the screen's aspect, over the view's (in 0x34EC44)

// Halo 4's HUD in a Left/Right half (module/entry/halo4/hud_fit.cpp)
DefOffset(OFFSET_HALO4_PF_HUD_UPDATE, 0x3BCF88) // (hud*, int user) - updates a user's HUD screens
DefOffset(OFFSET_HALO4_PF_HUD_RENDER, 0x3F7A7C) // (?, int user, ?, ?) - draws a user's HUD
DefOffset(OFFSET_HALO4_PF_UI_WIDE, 0x3DBB8C) // bool () - the screen is wider than 16:9
DefOffset(OFFSET_HALO4_PF_UI_ASPECT, 0x3DBBEC) // float (bool inverse) - the screen's aspect over 16:9's (or its inverse)
DefOffset(OFFSET_HALO4_PF_UI_EXTRA_WIDTH, 0x3DBC40) // float () - UI units either side of a 1280-wide layout on a wider screen
DefOffset(OFFSET_HALO4_PF_UI_EXTRA_HEIGHT, 0x3DBC98) // float () - ... either side of a 720-high layout on a narrower one
DefOffset(OFFSET_HALO4_PF_UI_ANCHOR, 0x3FD3E8) // (element*, context*) - places an element anchored to the screen's edges
DefOffset(OFFSET_HALO4_PF_UI_FIT_RANGE, 0x4224F0) // (widget*, context*) - fits a widget's extent to the screen's shape
DefOffset(OFFSET_HALO4_PF_UI_USER_INDEX, 0x3A7BDC) // int (int user id) - the local user index of a UI context's user (+0x28)
DefOffset(OFFSET_HALO4_PF_UI_SCREEN_RENDER, 0x3D6184) // (screen*, context*) - draws a UI screen, translated by the extra width/height
DefOffset(OFFSET_HALO4_VT_HUD_RETICLE_GROUP, 0xD6C2D0) // the vtable of the widget group holding the crosshair
DefOffset(OFFSET_HALO4_VT_UI_GROUP, 0xD7DE70) // the vtable of a UI screen's top-level widget groups
