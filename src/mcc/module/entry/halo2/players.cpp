#include "halo2.h"

// In a campaign each local player first spawns at one of the mission's starting locations, and the built-in missions
// have two: players 3 and 4 waited until players 1 and 2 walked off theirs, and didn't come in while they stood
// there. Local players past the mission's starting locations take the co-op respawn instead, which brings them in
// beside a teammate (once that teammate is out of combat, as for any co-op respawn) - only where the game gives co-op
// respawns. On Legendary or with the Iron skull it doesn't, and counts a co-op player waiting for one as dead, which
// reverts to the checkpoint: there they keep waiting for a starting location, as before.
namespace Halo2::Entry::Players {
    // players globals: player handle per local player, int[4] at +0xC (-1: none)
    int Handle(__int64 module, int local) {
        char* globals = *(char**)(module + OFFSET_HALO2_PV_RESPAWN);
        return globals ? *(int*)(globals + 0xC + 4 * local) : -1;
    }

    // players: data array (data at +*(+0x48)), 0x224 each: int16 flags +6 (8: spawns at a starting location), unit +0x2C
    char* Player(__int64 module, int handle) {
        char* players = *(char**)(module + OFFSET_HALO2_PV_PLAYERS);
        __int64 data = players ? *(__int64*)(players + 0x48) : 0;
        return data ? players + data + (handle & 0xFFFF) * 0x224 : nullptr;
    }

    // The game's own test before a waiting player's co-op respawn (players_update, 0x6A3BA2)
    bool CoopRespawn(__int64 module) {
        return ((bool (*)())(module + OFFSET_HALO2_PF_COOP_CAMPAIGN))() &&
               ((bool (*)())(module + OFFSET_HALO2_PF_COOP_RESPAWN_ALLOWED))();
    }

    int StartingLocations(__int64 module) {
        char* scenario = *(char**)(module + OFFSET_HALO2_PV_SCENARIO);
        return scenario ? *(int*)(scenario + 0x100) : 0;
    }

    Halo2Entry(entry, OFFSET_HALO2_PF_PLAYERS_UPDATE, void, detour, void* state) {
        __int64 module = entry.m_target - entry.m_offset;
        int places = CoopRespawn(module) ? StartingLocations(module) : 0;
        for (int local = places; places > 0 && local < 4; ++local) {
            int handle = Handle(module, local);
            char* player = handle != -1 ? Player(module, handle) : nullptr;
            if (player && *(int*)(player + 0x2C) == -1) // still waiting for a unit
                *(unsigned short*)(player + 6) &= ~8;
        }
        ((detour_t)entry.m_pOriginal)(state);
    }
}
