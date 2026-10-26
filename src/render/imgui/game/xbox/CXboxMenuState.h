#pragma once

#include <string>

struct PlayerColor {
    int colors[3];
};

struct MenuState {
    int playerCount;
    bool useKM;
    int controllerIndex[4];
    int controllerProfile[4];
    int sensitivity[4];
    bool invert[4];
    int teamIndex[4];
    PlayerColor playerColors[4];
    bool usePlayer0Profile; // players 2-4 use player 1's profile ("Use Player 1's Armor")
};
