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
        int Handle(__int64 module, int local);    // local player `local`'s player handle, -1: none (players.cpp)
        char* Player(__int64 module, int handle); // that player's data, or null
    }

    namespace Splitscreen {
        bool ClassicShown(__int64 module); // Classic graphics are on screen (Back switches them in a mission)
    }
}
