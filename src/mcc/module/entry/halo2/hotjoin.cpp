// Halo 2 hot join (mcc/splitscreen/Join.cpp), as Halo CE's (halo1/hotjoin.cpp): Halo 2 only makes its local players as
// a mission loads, so a hot-join mission starts with all four (MCC is told of four, CGameManagerSplitscreen.cpp) and
// the game's local player count - which its views, HUD and input follow - is held at the player count; the others
// don't spawn. A player who joins comes in beside a teammate through the co-op respawn (players.cpp), where the game
// gives co-op respawns (not on Legendary or with the Iron skull: there, at a starting place).
//
// The players held back keep waiting for a starting place, as players do before their first spawn: a player without a
// unit who isn't waiting for one counts as a dead teammate (0x6A16F0 sets players globals +5), which keeps every
// checkpoint from being saved (game_safe_to_save, 0x7005C0, at 0x70067E). Waiting so, though, they count as alive in
// the game's "every player is dead" (+4), so that's worked out over the players who joined (Account).
#include "halo2.h"

#include "global/Global.h"
#include "mcc/splitscreen/Splitscreen.h"

#include <algorithm>
#include <atomic>
#include <iterator>

namespace Halo2::Entry::HotJoin {
    // players.cpp holds the count each tick, from its players_update hook
    EntryFeature("Halo 2 hot join", OFFSET_HALO2_PF_COPY_GAME_OPTIONS, OFFSET_HALO2_PV_RESPAWN);
    const bool s_requires_players = (entry_feature->Requires(Players::Hooks()), true);

    constexpr int kSlots = 4;

    std::atomic<bool> s_active = false; // the running mission is a hot-join one
    // Held back in it at some point, until the next mission starts (game thread). A checkpoint saved before such a
    // player joined brings back, as it's restored, their record waiting for a starting place without a unit (players
    // and their globals are game state, allocated through 0x15A0AF0's allocator): they're still sent to the co-op
    // respawn, as whenever they have no unit (players.cpp).
    bool s_held_back[kSlots] = {};
    // The players it lets into the mission, taken once a tick, as players_update starts (Hold, game thread): one who
    // joins in the middle of a tick waits for the next, so that their record is sent to the co-op respawn before they
    // can spawn - spawning first, they would take the mission's starting place (0x6A3B93).
    std::atomic<int> s_admitted = kSlots;

    // players globals: int16 local player count +8, player handle[4] +0xC (both kept by 0x69E4A0, which maps a player
    // to a local index; Players::Handle reads the handles), bytes +4 every player dead and +5 one is (0x6A16F0)
    char* Globals(__int64 module) { return *(char**)(module + OFFSET_HALO2_PV_RESPAWN); }

    int Joined() { return std::clamp(MCC::Splitscreen::PlayerCount(), 1, kSlots); }

    int LocalIndex(__int64 module, int player) {
        for (int j = 0; j < kSlots; ++j)
            if (Players::Handle(module, j) == player) return j;
        return -1;
    }

    // A local player's record (unit +0x2C, flags +6), or null.
    char* Record(__int64 module, int local) {
        int handle = Players::Handle(module, local);
        return handle != -1 ? Players::Player(module, handle) : nullptr;
    }

    bool HasUnit(const char* record) { return *(const int*)(record + 0x2C) != -1; }

    // The count the game's local players are held at: the ones who joined, once they're all mapped; until then 0, and
    // the game keeps its own count. The game counts players as it maps them (in no set order over a fresh mission's
    // first ticks) and unmaps them, so a count past the ones mapped would run on past four.
    int Held(__int64 module, int joined) {
        for (int j = 0; j < joined; ++j)
            if (Players::Handle(module, j) == -1) return 0;
        return joined;
    }

    int Hold(__int64 module) {
        if (!s_active) return kSlots;
        int joined = s_admitted = Joined();
        for (int j = joined; j < kSlots; ++j) s_held_back[j] = true;
        if (char* globals = Globals(module))
            if (int held = Held(module, joined)) *(short*)(globals + 8) = (short)held;
        return joined;
    }

    bool JoinedLate(int local) { return s_active && local >= 0 && local < kSlots && s_held_back[local]; }

    void Account(__int64 module) {
        char* globals = s_active ? Globals(module) : nullptr;
        int joined = s_admitted;
        if (globals == nullptr || !Held(module, joined)) return; // until they're all mapped, the game's own
        bool all_dead = true, dead = false;
        for (int j = 0; j < joined; ++j) {
            char* record = Record(module, j);
            if (record == nullptr) return;
            if (HasUnit(record) || (*(unsigned short*)(record + 6) & 8)) all_dead = false; // or waiting to spawn
            else dead = true;
        }
        globals[4] = all_dead;
        globals[5] = dead;
    }

    // Players who aren't in the mission don't spawn (a starting place, or the co-op respawn beside a teammate).
    Halo2Entry(entry_spawn, OFFSET_HALO2_PF_PLAYER_SPAWN, bool, player_spawn, int player) {
        if (s_active) {
            int local = LocalIndex(entry_spawn.m_target - entry_spawn.m_offset, player); // -1 without the globals
            if (local >= s_admitted) return false;
        }
        return ((player_spawn_t)entry_spawn.m_pOriginal)(player);
    }

    bool Available() { return entry_feature->Available(); }

    void Reset() {
        s_active = false;
        s_admitted = kSlots;
        std::fill(std::begin(s_held_back), std::end(s_held_back), false);
    }
    const bool s_reset = (Halo2EntrySet()->on_remove(&Reset), true);

    // As a mission starts (halo2/graphics.cpp).
    void Start(bool on) {
        Reset();
        s_admitted = Joined();
        s_active = on && Available();
        if (s_active) LOG_INFO("Halo 2 hot join: this mission holds back the players who haven't joined");
    }

    bool Active() { return s_active; }
}
