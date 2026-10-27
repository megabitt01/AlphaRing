#pragma once

#include <halo2.h>
#include "offset_halo2.h"

#include "mcc/module/entry/entry.h"

extern EntrySet g_pHalo2EntrySet;
inline EntrySet* Halo2EntrySet() {return &g_pHalo2EntrySet;};

#define Halo2Entry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__);  \
    typedef returnType (*pDetour##_t)(__VA_ARGS__); \
    ::Entry name(Halo2EntrySet(), offset, pDetour, entry_feature); \
    returnType pDetour(__VA_ARGS__)

namespace Halo2::Entry {
    namespace Players {
        const ::AlphaRing::Feature& Hooks();      // its hook and offsets, which hot join requires (players.cpp)
        int Handle(__int64 module, int local);    // local player `local`'s player handle, -1: none
        char* Player(__int64 module, int handle); // that player's data, or null
    }

    namespace HotJoin {
        void Start(bool on); // as a mission starts (graphics.cpp): whether it's a hot-join one (hotjoin.cpp)
        // Each tick (players.cpp), before the game's: in a hot-join mission, the game's local player count held at the
        // players who joined, and that number; 4 otherwise.
        int Hold(__int64 module);
        // Local player `local` was held back in this hot-join mission at some point (with the players who haven't
        // joined; past the count, the caller tells them apart).
        bool JoinedLate(int local);
        // After the game's tick: in a hot-join mission, "every player is dead" and "a player is", over the ones who
        // joined.
        void Account(__int64 module);
    }

    namespace Splitscreen {
        bool ClassicShown(__int64 module); // Classic graphics are on screen (Back switches them in a mission)
    }
}
