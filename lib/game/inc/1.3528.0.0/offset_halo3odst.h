#pragma once

#include "Offset.h"

DefOffset(OFFSET_HALO3ODST_PF_ENGINE, 0xC0520)//0xC0424
DefOffset(OFFSET_HALO3ODST_PF_WORLD, 0x109F78)//0x109E70
DefOffset(OFFSET_HALO3ODST_PF_RENDER, 0x1B4708)//0x1B4608

DefOffset(OFFSET_HALO3ODST_PF_COOP_JOIN, 0x1258C)

DefOffset(OFFSET_HALO3ODST_PF_ADD_LOCAL_PLAYER, 0xFBFE4)//0xFBEE4

#define OFFSET_HALO3ODST_DAT_SKINNING_POOL 0x46AC274
#define OFFSET_HALO3ODST_PF_SKINNING_POOL_ALLOCATE 0x2CCFBC

#define OFFSET_HALO3ODST_V_ENTRY_PLAYERS 0x2
#define OFFSET_HALO3ODST_V_ENTRY_PLAYERS_ACTION 0xB

// c_splitscreen_config::m_config_table; the black-bar patches rewrite entries 8, 9 and 12 (0x8F1FB0, 0x8F1FC4, 0x8F2000)
DefOffset(OFFSET_HALO3ODST_PV_SPLITSCREEN_TABLE, 0x8F1F10)//0x8F1F20

// Left/Right split screen and each view's title-safe box (mcc/splitscreen/LeftRight); see offset_halo3.h for the signatures
DefOffset(OFFSET_HALO3ODST_PV_SCREEN_SIZE, 0x8F0278)
DefOffset(OFFSET_HALO3ODST_PF_SPLITSCREEN_PLAYER_COUNT, 0x313AF0)
DefOffset(OFFSET_HALO3ODST_PF_DRAW_SPLITSCREEN_BARS, 0x303C54)
DefOffset(OFFSET_HALO3ODST_PF_FILL_RECT, 0x1BA498)
DefOffset(OFFSET_HALO3ODST_PF_RT_CREATE, 0x2A2C64) // (target*, int sizes[], descriptor*, int variant, int)
DefOffset(OFFSET_HALO3ODST_PF_RT_POOL_RELEASE, 0x2A3400)
DefOffset(OFFSET_HALO3ODST_PF_RT_POOL_INIT, 0x2A30F0)
DefOffset(OFFSET_HALO3ODST_PF_HUD_RESOLUTION, 0x32DD0C)
DefOffset(OFFSET_HALO3ODST_PF_HUD_LAYOUT, 0x3284B4) // record* - +0x94 int canvas width, height; 0x110 bytes
DefOffset(OFFSET_HALO3ODST_PF_VIEW_SETUP, 0x2ABAC8) // void (view*, int slot, int players, int, int, void*) - as Halo 3's
DefOffset(OFFSET_HALO3ODST_PF_TITLE_SAFE, 0x29F634) // void (short rect[4]) - the screen's title-safe box, 5% in from each edge
DefOffset(OFFSET_HALO3ODST_VIEW_SETUP_TITLE_SAFE_RETURN, 0x2ABB14) // where the view setup's call to it returns
