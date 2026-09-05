#include "haloreach.h"

#include "common.h"

namespace HaloReach::Entry::Loadout {
    typedef unsigned __int64 (*GetSplitscreenPlayerCount_t)();

    // The game's own resolution->resource-ID lookup only recognizes resolution
    // values 0, 2 and 3 (each maps to a registered loadout layout template); any
    // other value, including 1, falls through to a "not found" default and the
    // loadout screen never gets built. resolution=1 is otherwise the cleanest
    // splitscreen mode (no HUD/crosshair artifacts), so we reuse resolution=3's
    // already-working template ID for it instead of patching the game's own
    // (space-constrained) lookup function directly.
    HaloReachEntry(entry, OFFSET_HALOREACH_PF_SPLITSCREEN_RESOLUTION_RESOURCE, int, detour, int slot_index) {
        __int64 hModule = entry.m_target - entry.m_offset;

        if (slot_index >= 0 && slot_index < 4) {
            auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
            int player_count = (int)GetSplitscreenPlayerCount();
            int* p_resolution = (int*)(hModule + 0xB43C40 + 0x10 + (size_t)(slot_index + player_count * 4) * 20);

            if (*p_resolution == 1) return 0x80046;
        }

        return ((detour_t)entry.m_pOriginal)(slot_index);
    }
}
