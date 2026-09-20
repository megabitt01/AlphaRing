#pragma once

#include "common.h"

class EntrySet;

struct Entry {
public:
    Entry(EntrySet* set, __int64 offset, void* pDetour);

    bool update(__int64 hModule);

    __int64 m_offset;
    void* m_pOriginal;
    __int64 m_target;
    void* m_pDetour;
};

class EntrySet {
public:
    void append(Entry* entry);
    bool update(__int64 hModule);

private:
    // Halo Reach reached the old limit of 20 with the FOV baseline seam (5
    // hooks); the append assert compiles out in Release, so overflow would be
    // silent. Keep headroom.
    inline static const int MAX_ENTRY = 32;
    int entryCount;
    Entry* entryArray[MAX_ENTRY];

};
