#pragma once

#include <halo4.h>
#include "mcc/module/entry/entry.h"

extern EntrySet g_pHalo4EntrySet;
inline EntrySet* Halo4EntrySet() {return &g_pHalo4EntrySet;};

// The hooks CModule installs for Halo 4. The ones above (engine, render, world) were never switched on
// and their offsets are unchecked, so the split-screen hooks have a set of their own.
extern EntrySet g_Halo4SplitscreenEntrySet;
inline EntrySet* Halo4SplitscreenEntrySet() {return &g_Halo4SplitscreenEntrySet;};

#define Halo4SplitscreenEntry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__); \
    typedef returnType (*pDetour##_t)(__VA_ARGS__); \
    ::Entry name(Halo4SplitscreenEntrySet(), offset, pDetour, entry_feature); \
    returnType pDetour(__VA_ARGS__)

namespace Halo4::Entry {
    // HUD layouts (OFFSET_HALO4_PF_HUD_LAYOUT): full screen 1280x720, two-player 960x360, quarter 640x360
    constexpr int kFullHudLayout = 0x80076, kHalfHudLayout = 0x80077, kQuarterHudLayout = 0x80078;

    namespace HudFit {
        void Refresh(__int64 module); // each user's fit, once a frame (hud_fit.cpp)
        const ::AlphaRing::Feature& Hooks(); // its hooks and offsets, which Left/Right requires
    }
}

#define Halo4Entry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__); \
    typedef returnType (*pDetour##_t)(...); \
    ::Entry name(Halo4EntrySet(), offset, pDetour, entry_feature); \
    returnType pDetour(__VA_ARGS__)
