#pragma once

#include <haloreach.h>

#include "mcc/module/entry/entry.h"

extern EntrySet g_pHaloReachEntrySet;
inline EntrySet* HaloReachEntrySet() {return &g_pHaloReachEntrySet;};

#define HaloReachEntry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__); \
    typedef returnType (*pDetour##_t)(...); \
    ::Entry name(HaloReachEntrySet(), offset, pDetour); \
    returnType pDetour(__VA_ARGS__)

namespace HaloReach::Entry::SplitscreenRt {
    // Layout generation the render-target pool was last built against
    // (stamped by the RT_CREATE detour), and an explicit stamp for the render
    // hook to record after it has requested a rebuild.
    unsigned BuiltLayoutGeneration();
    void MarkLayoutGenerationBuilt(unsigned generation);
}
