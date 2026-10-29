#pragma once

#include "Offset.h"

DefOffset(OFFSET_HALO2_PF_LOAD, 0x4F980)
DefOffset(OFFSET_HALO2_PF_GAME, 0x67A220)
DefOffset(OFFSET_HALO2_PF_COPY_GAME_OPTIONS, 0x39CE0) // copy_game_options(game_options)

DefOffset(OFFSET_HALO2_PF_ADD_LOCAL_PLAYER, 0x69D2C0)//0x69D290

DefOffset(OFFSET_HALO2_PV_PLAYERS, 0xE80A28)//0xE7FA28
DefOffset(OFFSET_HALO2_PV_RESPAWN, 0xE80A20)//0xE7FA20

DefOffset(OFFSET_HALO2_PF_PLAYER_VALID, 0x6A6C80)//0x6A6C30
DefOffset(OFFSET_HALO2_PF_PLAYER_COUNT1, 0x8940CA)//0x893FDA
DefOffset(OFFSET_HALO2_PF_PLAYER_COUNT2, 0x894127)//0x894037

// The views' hand-over splits the screen only for exactly two local players (cmp eax, 2 / jne);
// splitscreen_patch4 makes it any count but one (cmp eax, 1 / je)
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

// Left/Right split screen (mcc/splitscreen/LeftRight)
DefOffset(OFFSET_HALO2_PF_SPLIT_GRID, 0x7E09D0) // void (int views, int mode, short grid[2] columns, rows) - mode 1 stacks, others side by side
DefOffset(OFFSET_HALO2_PF_SPLIT_CELL, 0x7E0BD0) // void (int view, int views, int mode, short grid[2], short cell[2], short span[2])
DefOffset(OFFSET_HALO2_PF_SPLIT_DIVIDERS, 0x831D90) // void () - paints the black bands between the views, by the frame's mode
DefOffset(OFFSET_HALO2_PV_SPLIT_MODE, 0x165C168) // int - the frame's grid mode, which only the dividers read
DefOffset(OFFSET_HALO2_PV_SPLIT_VIEWS, 0x165C16C) // int - the frame's views
DefOffset(OFFSET_HALO2_PV_ANNIVERSARY, 0xE21280) // int, nonzero while the Anniversary renderer is up
DefOffset(OFFSET_HALO2_PV_CLASSIC_SHOWN, 0x1E8CE73) // bool, the player switched to Classic graphics
