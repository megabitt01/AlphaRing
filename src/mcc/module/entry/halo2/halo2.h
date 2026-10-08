#pragma once

#include <halo2.h>
#include "offset_halo2.h"

#include "mcc/module/entry/entry.h"

extern EntrySet g_pHalo2EntrySet;
inline EntrySet* Halo2EntrySet() {return &g_pHalo2EntrySet;};

#define Halo2Entry(name, offset, returnType, pDetour, ...) \
    returnType pDetour(__VA_ARGS__);  \
    typedef returnType (*pDetour##_t)(__VA_ARGS__); \
    ::Entry name(Halo2EntrySet(), offset, pDetour); \
    returnType pDetour(__VA_ARGS__)
