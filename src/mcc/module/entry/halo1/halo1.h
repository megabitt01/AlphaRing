#pragma once

#include <halo1.h>

#include "mcc/module/entry/entry.h"

extern EntrySet g_pHalo1EntrySet;
inline EntrySet* Halo1EntrySet() {return &g_pHalo1EntrySet;};

#define Halo1Entry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__); \
    typedef returnType (*pDetour##_t)(...); \
    ::Entry name(Halo1EntrySet(), offset, pDetour, entry_feature); \
    returnType pDetour(__VA_ARGS__)

namespace Halo1::Entry {
    namespace Splitscreen {
        const ::AlphaRing::Feature& Hooks(); // its hooks and offsets, which Anniversary graphics with 3-4 players require
        bool ClassicShown(__int64 module);   // Classic graphics are on screen (Back switches them in a mission)
    }

    namespace Anniversary {
        bool Quad(__int64 module); // 3-4 players drawn in quarters, Anniversary graphics (anniversary.cpp)
    }
}
