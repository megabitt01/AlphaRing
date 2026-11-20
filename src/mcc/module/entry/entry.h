#pragma once

#include "common.h"

#include "Offset.h"

class EntrySet;

struct Entry {
public:
    // `feature`: the EntryFeature of the namespace the hook is declared in (entry_feature), or none.
    Entry(EntrySet* set, const AlphaRing::Offset& offset, void* pDetour, AlphaRing::Feature* feature);

    // Hooks it, unless its offset or one of its feature's isn't in the module's build.
    bool update(__int64 hModule);
    void remove();

    const AlphaRing::Offset* m_source;
    AlphaRing::Feature* m_feature;
    __int64 m_offset; // m_source's value, set as it's hooked
    void* m_pOriginal;
    __int64 m_target;
    void* m_pDetour;
};

class EntrySet {
public:
    void append(Entry* entry);
    bool update(__int64 hModule);
    // Unhook while the module is still mapped: MCC loads every game DLL at the menu and
    // reloads them at new addresses, so a hook left behind would later be "restored" into
    // whatever DLL occupies its old address.
    void remove();
    // Called as the set is removed, for state that points into the module (up to MAX_CALLBACK).
    void on_remove(void (*callback)());

private:
    // Halo Reach reached the old limit of 20 with the FOV baseline seam (5
    // hooks); the append assert compiles out in Release, so overflow would be
    // silent. Keep headroom.
    inline static const int MAX_ENTRY = 32;
    int entryCount;
    Entry* entryArray[MAX_ENTRY];
    inline static const int MAX_CALLBACK = 4;
    int callbackCount;
    void (*callbackArray[MAX_CALLBACK])();

};

// Hooks declared in a namespace without an EntryFeature stand alone: each needs only its own offset.
inline constexpr AlphaRing::Feature* entry_feature = nullptr;

// Makes the hooks declared after it in the same namespace one feature: on an MCC build where the offset of any
// of them, or any offset listed here that their detours use, isn't found (src/offsets/Offsets.h), none of them
// are hooked. `name` is for the log.
#define EntryFeature(name, ...) \
    static ::AlphaRing::Feature entry_feature_offsets{name, {__VA_ARGS__}}; \
    static ::AlphaRing::Feature* const entry_feature = &entry_feature_offsets
