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
