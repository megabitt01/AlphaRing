// Halo CE hot join (mcc/splitscreen/Join.cpp): Halo CE only makes its local players as a mission loads, so a hot-join
// mission starts with all four (MCC is told of four, CGameManagerSplitscreen.cpp) and the game's local player count -
// which its views, HUD, input and cameras follow, live - is held at the player count; the others don't spawn and have
// no body. Halo CE spawns players only at the mission's starting places, which a player joining later could be far
// behind or find taken (the built-in missions have two, where the others may still stand), so a player who joins
// comes in on a spot a teammate walked through lately - or, while there's none, at a starting place and is moved to
// one as soon as there is.
#include "halo1.h"

#include "global/Global.h"
#include "mcc/splitscreen/Splitscreen.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace Halo1::Entry::HotJoin {
    EntryFeature("Halo CE hot join", OFFSET_HALO1_PF_GAME_START, OFFSET_HALO1_PV_LOCAL_PLAYERS,
                 OFFSET_HALO1_PV_PLAYER_COUNT, OFFSET_HALO1_PF_OBJECT_DELETE, OFFSET_HALO1_PF_PLAYER_SET_UNIT,
                 OFFSET_HALO1_PF_OBJECT_GET_ORIGIN, OFFSET_HALO1_PF_OBJECT_SET_POSITION,
                 OFFSET_HALO1_PF_OBJECT_TELEPORTED, OFFSET_HALO1_PV_SABER_SPLIT, OFFSET_HALO1_PV_SABER_SPLIT_STATE,
                 OFFSET_HALO1_PF_SABER_SET_SPLIT);

    constexpr int kSlots = 4;

    std::atomic<bool> s_active = false; // the running mission is a hot-join one
    int s_slots = 0;                    // local players it was started with (read on its first update)

    // Where each player walked lately on foot: every half second (the game ticks 30 times a second), a spot a step
    // from the one before at about the same height - not driving, jumping or falling - and a stride from the last one
    // kept, so a player standing still keeps where they walked before. And for how many more ticks a player is to be
    // brought beside a teammate once they have a body: set while they're out of the mission, so it's there when they
    // join.
    struct Point {
        float x, y, z;
    };
    struct Spawn { // a starting place's first 0x10 bytes
        Point at;
        float facing;
    };
    constexpr int kTrail = 8, kTrailTicks = 15, kPlaceTicks = 300;
    constexpr float kStep = 2.0f, kRise = 0.25f; // world units in half a second
    constexpr float kStride = 0.8f;              // between the spots kept
    constexpr float kClear = 0.6f;               // from every player now (a Spartan is about 0.2 across)
    constexpr float kNear = 10.0f;               // from the teammate now: spots left behind by a teleport, a
                                                 // checkpoint or a drive aren't used
    Point s_trail[kSlots][kTrail];               // newest first
    int s_trail_count[kSlots] = {};
    Point s_sampled[kSlots];                     // the last half-second sample, on foot or not
    bool s_has_sample[kSlots] = {};
    int s_place[kSlots] = {};
    int s_tick = 0;
    // Held back in this mission at some point, until the next one starts. Players and their globals are game state
    // (allocated through 0xACD75C), so going back to a checkpoint from before such a player joined brings back their
    // record without a unit, to spawn at a starting place (0xAD0B63 -> 0xAD4184 -> 0xAD39AC): whenever they have no
    // unit they're to be brought beside a teammate again.
    bool s_held_back[kSlots] = {};

    // players globals: int16 local player count +0xB4, player handle[4] +0xB8, unit[4] +0xC8
    char* Globals(__int64 module) { return *(char**)(module + OFFSET_HALO1_PV_LOCAL_PLAYERS); }
    int Handle(char* globals, int local) { return *(int*)(globals + 0xB8 + 4 * local); }
    int Unit(char* globals, int local) { return *(int*)(globals + 0xC8 + 4 * local); }

    int Joined() { return std::clamp(MCC::Splitscreen::PlayerCount(), 1, kSlots); }

    int LocalIndex(char* globals, int player) {
        for (int j = 0; j < kSlots; ++j)
            if (Handle(globals, j) == player) return j;
        return -1;
    }

    Point Origin(__int64 module, int object) {
        Point at{};
        ((void (*)(int, Point*))(module + OFFSET_HALO1_PF_OBJECT_GET_ORIGIN))(object, &at);
        return at;
    }

    float Distance2(Point a, Point b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    void Sample(__int64 module, int local, int unit) {
        Point at = Origin(module, unit), before = s_sampled[local];
        bool on_foot = s_has_sample[local] && std::abs(at.z - before.z) <= kRise &&
                       Distance2({at.x, at.y, 0}, {before.x, before.y, 0}) <= kStep * kStep;
        s_sampled[local] = at;
        s_has_sample[local] = true;
        if (!on_foot || (s_trail_count[local] > 0 && Distance2(at, s_trail[local][0]) < kStride * kStride)) return;
        std::copy_backward(s_trail[local], s_trail[local] + kTrail - 1, s_trail[local] + kTrail);
        s_trail[local][0] = at;
        s_trail_count[local] = (std::min)(s_trail_count[local] + 1, kTrail);
    }

    void Forget(int local) {
        s_trail_count[local] = 0;
        s_has_sample[local] = false;
    }

    // The newest spot a teammate walked through, near where they are now, that no player is on now, and which way to
    // face there (toward that teammate); false while there's none (nobody has walked anywhere near yet).
    bool Spot(__int64 module, char* globals, int joiner, int joined, Spawn* spawn) {
        Point now[kSlots];
        bool here[kSlots] = {};
        for (int k = 0; k < joined; ++k)
            if (k != joiner && Unit(globals, k) != -1) {
                here[k] = true;
                now[k] = Origin(module, Unit(globals, k));
            }
        for (int t = 0; t < joined; ++t) {
            if (!here[t]) continue;
            for (int n = 0; n < s_trail_count[t]; ++n) {
                Point at = s_trail[t][n];
                if (Distance2(at, now[t]) > kNear * kNear) continue;
                bool clear = true;
                for (int k = 0; k < joined && clear; ++k) clear = !here[k] || Distance2(at, now[k]) >= kClear * kClear;
                if (!clear) continue;
                *spawn = {at, std::atan2(now[t].y - at.y, now[t].x - at.x)};
                return true;
            }
        }
        return false;
    }

    // A joiner who spawned at a starting place, moved to a teammate's spot.
    bool Place(__int64 module, char* globals, int joiner, int joined) {
        Spawn spawn;
        if (!Spot(module, globals, joiner, joined, &spawn)) return false;
        int unit = Unit(globals, joiner);
        ((void (*)(int, Point*, void*, void*))(module + OFFSET_HALO1_PF_OBJECT_SET_POSITION))(unit, &spawn.at, nullptr,
                                                                                         nullptr);
        ((void (*)(int))(module + OFFSET_HALO1_PF_OBJECT_TELEPORTED))(unit);
        return true;
    }

    // Halo CE sets its Anniversary renderer's split screen only while the map loads, from the local player count
    // (0x6762A) - four in a hot-join mission, which left one player split in two. It follows the players in the
    // mission instead, applied as the game applies it: set_split adds or drops the second camera and lays the views
    // out (halo1/anniversary.cpp's detour of it makes and drops the cameras for players 3 and 4).
    void FollowSplit(__int64 module, int joined) {
        char* state = *(char**)(module + OFFSET_HALO1_PV_SABER_SPLIT_STATE);
        if (state == nullptr) return;
        bool split = joined > 1;
        *(bool*)(module + OFFSET_HALO1_PV_SABER_SPLIT) = split;
        if ((state[0x41] != 0) != split) ((void (*)(char*))(module + OFFSET_HALO1_PF_SABER_SET_SPLIT))(state);
    }

    // Each tick: the bodies of the players who aren't in the mission, the game's local player count, and bringing the
    // players who joined to the others.
    void Update(__int64 module) {
        char* globals = Globals(module);
        if (globals == nullptr) return;
        if (s_slots == 0) s_slots = std::clamp((int)*(short*)(globals + 0xB4), 1, kSlots);
        int joined = (std::min)(Joined(), s_slots);
        for (int j = joined; j < s_slots; ++j) {
            s_held_back[j] = true;
            s_place[j] = kPlaceTicks;
            Forget(j);
            int unit = Unit(globals, j);
            if (unit == -1) continue;
            // the game won't delete a player's unit: the player leaves it first
            ((void (*)(int, int))(module + OFFSET_HALO1_PF_PLAYER_SET_UNIT))(Handle(globals, j), -1);
            ((void (*)(int))(module + OFFSET_HALO1_PF_OBJECT_DELETE))(unit);
        }
        *(short*)(globals + 0xB4) = (short)joined;
        *(short*)(module + OFFSET_HALO1_PV_PLAYER_COUNT) = (short)joined;
        FollowSplit(module, joined);

        bool sample = ++s_tick % kTrailTicks == 0;
        for (int j = 0; j < joined; ++j) {
            int unit = Unit(globals, j);
            if (unit == -1) {
                Forget(j); // the next body starts a new trail
                if (s_held_back[j]) s_place[j] = kPlaceTicks;
            } else if (s_place[j] > 0) {
                if (Place(module, globals, j, joined) || --s_place[j] == 0) s_place[j] = 0;
            } else if (sample) {
                Sample(module, j, unit);
            }
        }
    }

    Halo1Entry(entry_update, OFFSET_HALO1_PF_PLAYERS_UPDATE, void, players_update, void* state) {
        if (s_active) Update(entry_update.m_target - entry_update.m_offset);
        ((players_update_t)entry_update.m_pOriginal)(state);
    }

    // A joiner's first body at a teammate's spot: while they spawn, the starting place the game picks is that spot
    // (the game thread's).
    thread_local const Spawn* t_spawn = nullptr;
    thread_local bool t_spot_taken = false; // the spawn going on took that spot
    char s_start[0x34];

    Halo1Entry(entry_choose_start, OFFSET_HALO1_PF_CHOOSE_START_LOCATION, short, choose_start, int player) {
        return t_spawn ? 0 : ((choose_start_t)entry_choose_start.m_pOriginal)(player);
    }

    Halo1Entry(entry_start, OFFSET_HALO1_PF_START_LOCATION, char*, start_location, short index) {
        char* entry = ((start_location_t)entry_start.m_pOriginal)(index);
        if (!t_spawn || entry == nullptr) return entry;
        memcpy(s_start, entry, sizeof s_start);
        memcpy(s_start, t_spawn, sizeof *t_spawn);
        t_spot_taken = true;
        return s_start;
    }

    // The co-op respawn (players_update, 0xAD0546) spawns a waiting player - at a starting place, through the two
    // above - and then moves them beside a teammate, true when it could: moved, they're where Place would bring them.
    Halo1Entry(entry_move_beside, OFFSET_HALO1_PF_PLAYER_MOVE_BESIDE, bool, move_beside, int player, int other,
               void* where) {
        bool moved = ((move_beside_t)entry_move_beside.m_pOriginal)(player, other, where);
        if (moved && s_active) {
            char* globals = Globals(entry_move_beside.m_target - entry_move_beside.m_offset);
            int local = globals ? LocalIndex(globals, player) : -1;
            if (local >= 0) s_place[local] = 0;
        }
        return moved;
    }

    // Players who aren't in the mission don't spawn, and one who joined comes in at a teammate's spot if there is one.
    Halo1Entry(entry_spawn, OFFSET_HALO1_PF_PLAYER_SPAWN, void, player_spawn, int player) {
        auto original = (player_spawn_t)entry_spawn.m_pOriginal;
        if (!s_active) return original(player);
        __int64 module = entry_spawn.m_target - entry_spawn.m_offset;
        char* globals = Globals(module);
        int local = globals ? LocalIndex(globals, player) : -1;
        int joined = Joined();
        if (local >= joined) return;
        Spawn spawn;
        bool at_spot = local >= 0 && s_place[local] > 0 && Spot(module, globals, local, joined, &spawn);
        t_spawn = at_spot ? &spawn : nullptr;
        t_spot_taken = false;
        original(player);
        t_spawn = nullptr;
        if (t_spot_taken && Unit(globals, local) != -1) s_place[local] = 0; // where Place would have moved them
    }

    bool Available() { return entry_feature->Available(); }

    void Reset() {
        s_active = false;
        s_slots = s_tick = 0;
        for (int j = 0; j < kSlots; ++j) {
            s_place[j] = 0;
            s_held_back[j] = false;
            Forget(j);
        }
    }
    const bool s_reset = (Halo1EntrySet()->on_remove(&Reset), true);

    // As a mission starts (halo1/graphics.cpp).
    void Start(bool on) {
        Reset();
        s_active = on && Available();
        if (s_active) LOG_INFO("Halo CE hot join: this mission holds back the players who haven't joined");
    }

    bool Active() { return s_active; }
}
