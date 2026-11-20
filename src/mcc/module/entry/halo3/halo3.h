#pragma once

#include <halo3.h>
#include "offset_halo3.h"

#include "mcc/module/entry/entry.h"

extern EntrySet g_pHalo3EntrySet;
inline EntrySet* Halo3EntrySet() {return &g_pHalo3EntrySet;};

#define Halo3Entry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__);  \
    typedef returnType (*pDetour##_t)(__VA_ARGS__); \
    ::Entry name(Halo3EntrySet(), offset, pDetour, entry_feature); \
    returnType pDetour(__VA_ARGS__)

namespace Halo3::Entry::Skinning {
    // Moves Halo 3's or ODST's per-frame skinning pool to a bigger buffer (skinning.cpp; the games share the
    // engine). CModule calls Enlarge once the module is loaded and hooked, and Unload while it is still mapped,
    // before MCC unloads it. Other titles are ignored.
    void Enlarge(int title, __int64 hModule);
    void Unload(int title);
}
