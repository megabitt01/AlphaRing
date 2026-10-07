// Halo 2 spawn backend.
//
// Objects: object_placement_data_new + object_new. Characters: like Halo 3 (mcc/spawn/gen3.cpp),
// one starting location of an empty squad in the loaded scenario is pointed at the player and
// the chosen character, placed with ai_place, and put back. Its weapon comes from the scenario's
// weapon palette the same way (see gen3.cpp).

#include "Backend.h"

#include "common.h"

#include "mcc/CGameGlobal.h"

#include <offset_halo2.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>

namespace MCC::Spawn::Halo2 {
    static constexpr int kGame = CGameGlobal::Halo2;

    template <typename R, typename... A>
    static R Call(__int64 rva, A... args) { return EngineCall<R>(kGame, rva, args...); }

    template <typename T>
    static T Global(__int64 rva) { return EngineGlobal<T>(kGame, rva); }

    // --- tags -------------------------------------------------------------------------------

    struct TagInstance { unsigned int group, datum, address, size; };
    struct Block { int count; unsigned int address; };

    // Tag block addresses point into the map's tag data, or with the top bit set, the shared map's.
    static char* Address(unsigned int address) {
        if (address == 0xFFFFFFFF) return nullptr;
        if (address & 0x80000000)
            return Global<char*>(OFFSET_HALO2_PV_SHARED_TAG_BASE) + (address & 0x7FFFFFFF);
        return Global<char*>(OFFSET_HALO2_PV_TAG_BASE) + address;
    }
    static char* Element(const char* block, int index, int size) {
        auto b = (const Block*)block;
        return (index >= 0 && index < b->count) ? Address(b->address) + (__int64)index * size : nullptr;
    }
    static int Count(const char* block) { return ((const Block*)block)->count; }
    static char* Scenario() { return Global<char*>(OFFSET_HALO2_PV_SCENARIO); }

    static char* TagData(int tag) {
        auto instances = Global<TagInstance*>(OFFSET_HALO2_PV_TAG_INSTANCES);
        int index = tag & 0xFFFF;
        return (instances && tag != -1 && index < Global<int>(OFFSET_HALO2_PV_TAG_COUNT)) ? Address(instances[index].address) : nullptr;
    }

    static const char* TagName(int tag) {
        int index = tag & 0xFFFF;
        if (tag == -1 || index >= Global<int>(OFFSET_HALO2_PV_TAG_COUNT)) return nullptr;
        return Global<char*>(OFFSET_HALO2_PV_TAG_NAMES) + Global<int*>(OFFSET_HALO2_PV_TAG_NAME_OFFSETS)[index];
    }

    template <typename F>
    static void ForEachTag(unsigned int group, F&& f) {
        auto instances = Global<TagInstance*>(OFFSET_HALO2_PV_TAG_INSTANCES);
        int count = Global<int>(OFFSET_HALO2_PV_TAG_COUNT);
        for (int i = 0; instances && i < count; ++i)
            if (instances[i].group == group) f((int)instances[i].datum);
    }

    // --- game state ---------------------------------------------------------------------------

    // Blam data array: data lives at the array + the offset at +0x48.
    static char* Datum(__int64 array_global, int index, int size) {
        auto array = Global<char*>(array_global);
        if (array == nullptr || (index & 0xFFFF) >= *(int*)(array + 0x20)) return nullptr;
        auto datum = array + *(__int64*)(array + 0x48) + (__int64)size * (index & 0xFFFF);
        return *(short*)datum ? datum : nullptr; // a zero salt marks a free slot
    }

    static char* ObjectData(int object) {
        auto header = Datum(OFFSET_HALO2_PV_OBJECTS, object, 0xC);
        if (header == nullptr) return nullptr;
        auto pool = Global<__int64>(OFFSET_HALO2_PV_OBJECT_MEMORY);
        return (char*)(((pool + 0x57) & ~0xFll) + *(int*)(header + 8));
    }

    static bool PlayerView(int player, Vector3& origin, Vector3& facing) {
        auto datum = Datum(OFFSET_HALO2_PV_PLAYERS, player, 0x224);
        int unit = datum ? *(int*)(datum + 0x2C) : -1;
        auto object = unit == -1 ? nullptr : ObjectData(unit);
        if (object == nullptr) return false;
        Call<void>(OFFSET_HALO2_PF_OBJECT_GET_ORIGIN, unit, &origin);
        facing = *(Vector3*)(object + 0x70);
        return true;
    }

    // --- objects -----------------------------------------------------------------------------

    static int SpawnObject(int tag, int player, Category category) {
        Vector3 origin, facing;
        if (!PlayerView(player, origin, facing)) return -1;
        auto place = PlaceAhead(origin, facing, SpawnDistance(category), 0.5f);

        alignas(16) char data[0x100] = {}; // object_placement_data is 0xC4 bytes
        Call<void>(OFFSET_HALO2_PF_OBJECT_PLACEMENT_DATA_NEW, data, tag, -1, (void*)nullptr);
        *(Vector3*)(data + 0x1C) = place.position;
        *(Vector3*)(data + 0x28) = place.forward;
        *(Vector3*)(data + 0x34) = place.up;
        return Call<int>(OFFSET_HALO2_PF_OBJECT_NEW, data);
    }

    // --- characters ---------------------------------------------------------------------------

    // Scenario layout (Assembly Halo2MCC scnr plugin): squads 0x160 (0x74 each: team 0x24,
    // character 0x36, zone 0x38, starting locations 0x48), starting location 0x64, zones 0x168
    // (0x38 each, firing positions 0x28 of 0x20), weapon palette 0x98 (0x28 each), character
    // palette 0x178 (0x8 each).
    static constexpr int kSquads = 0x160, kSquadSize = 0x74, kSquadLocations = 0x48, kLocationSize = 0x64;
    static constexpr int kZones = 0x168, kZoneSize = 0x38, kZoneFiringPositions = 0x28, kFiringPositionSize = 0x20;
    static constexpr int kWeaponPalette = 0x98, kCharacterPalette = 0x178;

    static int CharacterPaletteTag(int index) {
        auto entry = Element(Scenario() + kCharacterPalette, index, 0x8);
        return entry ? *(int*)(entry + 4) : -1;
    }

    static int* PaletteWeaponTag(int index) {
        auto entry = Element(Scenario() + kWeaponPalette, index, 0x28);
        return entry ? (int*)(entry + 4) : nullptr;
    }
    static int PaletteWeapon(int index) {
        auto tag = PaletteWeaponTag(index);
        return tag ? *tag : -1;
    }

    // The weapon the mission usually gives `character`: the one its squads and their starting
    // locations name most often for it. Otherwise the first carriable weapon of the character
    // tag's weapons properties (character +0xCC, 0xCC each; inherited from parent characters).
    static int UsualWeapon(int character) {
        std::map<int, int> votes;
        auto scenario = Scenario();
        for (int s = 0; s < Count(scenario + kSquads); ++s) {
            auto squad = Element(scenario + kSquads, s, kSquadSize);
            for (int l = 0; l < Count(squad + kSquadLocations); ++l) { // a location's own choices win over its squad's
                auto location = Element(squad + kSquadLocations, l, kLocationSize);
                short c = *(short*)(location + 0x20), w = *(short*)(location + 0x22);
                int weapon = PaletteWeapon(w != -1 ? w : *(short*)(squad + 0x3C));
                if (weapon != -1 && CharacterPaletteTag(c != -1 ? c : *(short*)(squad + 0x36)) == character) ++votes[weapon];
            }
        }
        if (int weapon = MostVoted(votes); weapon != -1) return weapon;

        for (int depth = 0; depth < 8; ++depth) {
            auto data = TagData(character);
            if (data == nullptr) return -1;
            if (Count(data + 0xCC) > 0) {
                for (int i = 0; i < Count(data + 0xCC); ++i) {
                    int weapon = *(int*)(Element(data + 0xCC, i, 0xCC) + 0x8);
                    if (weapon != -1 && !MountedWeapon(TagName(weapon))) return weapon;
                }
                return -1;
            }
            character = *(int*)(data + 0x8); // parent character
        }
        return -1;
    }

    // ai_living_count: the count with no flags and no outputs (it's a thunk doing just that, see offset_halo2.h)
    static int LivingCount(int squad) {
        return Call<int>(OFFSET_HALO2_PF_AI_LIVING_COUNT, squad, 0, (void*)nullptr, (void*)nullptr);
    }

    struct SpawnPoint { int squad, location; char *squad_data, *location_data; };

    // The starting location nearest to `position` among empty squads.
    static bool NearestSpawnPoint(const Vector3& position, SpawnPoint& out) {
        auto scenario = Scenario();
        float best = 1e30f;
        for (int s = 0; s < Count(scenario + kSquads); ++s) {
            auto squad = Element(scenario + kSquads, s, kSquadSize);
            if (Count(squad + kSquadLocations) == 0 || LivingCount(s) != 0) continue;
            for (int l = 0; l < Count(squad + kSquadLocations) && l < 0x8000; ++l) {
                auto location = Element(squad + kSquadLocations, l, kLocationSize);
                float distance = Distance2(*(const Vector3*)(location + 0x04), position);
                if (distance < best) { best = distance; out = {s, l, squad, location}; }
            }
        }
        return best < 1e30f;
    }

    static short NearestZone(const Vector3& position) {
        auto scenario = Scenario();
        short nearest = -1;
        float best = 1e30f;
        for (int z = 0; z < Count(scenario + kZones); ++z) {
            auto positions = Element(scenario + kZones, z, kZoneSize) + kZoneFiringPositions;
            for (int f = 0; f < Count(positions); ++f) {
                float distance = Distance2(*(const Vector3*)Element(positions, f, kFiringPositionSize), position);
                if (distance < best) { best = distance; nearest = (short)z; }
            }
        }
        return nearest;
    }

    static std::string SpawnCharacter(int palette_index, int player, Team team, int weapon) {
        int character = CharacterPaletteTag(palette_index);
        std::string name = character == -1 ? "character" : DisplayName(TagName(character));

        Vector3 origin, facing;
        if (!PlayerView(player, origin, facing)) return "No player " + std::to_string(player + 1);
        auto place = PlaceAhead(origin, facing, SpawnDistance(Characters), 0.3f);

        SpawnPoint spawn;
        if (!NearestSpawnPoint(origin, spawn)) return "This mission has no free squads to spawn characters with";

        // before the borrowed squad changes, since the mission's own choices are counted
        if (weapon == kUsualWeapon) weapon = UsualWeapon(character);

        auto squad = spawn.squad_data, location = spawn.location_data;
        std::array<char, kSquadSize> saved_squad;
        std::array<char, kLocationSize> saved_location;
        memcpy(saved_squad.data(), squad, kSquadSize);
        memcpy(saved_location.data(), location, kLocationSize);
        MCC::Command::RecordChange(squad, kSquadSize); // put back at once if ai_place faults
        MCC::Command::RecordChange(location, kLocationSize);

        *(unsigned*)(squad + 0x20) &= ~0x88u; // no "delay forever", no respawning
        *(short*)(squad + 0x24) = EngineTeam(team, 0); // 0: the character's own team
        *(float*)(squad + 0x28) = 0.0f; // squad delay
        *(short*)(squad + 0x36) = (short)palette_index;
        *(short*)(squad + 0x38) = NearestZone(place.position);
        for (int offset : {0x34, 0x3C, 0x3E, 0x42, 0x70}) *(short*)(squad + offset) = -1; // vehicle, weapons, order, script

        *(Vector3*)(location + 0x04) = place.position;
        *(short*)(location + 0x10) = -1; // no reference frame: world coordinates
        *(float*)(location + 0x14) = place.YawToPlayer();
        *(float*)(location + 0x18) = 0.0f;
        *(unsigned*)(location + 0x1C) = (*(unsigned*)(location + 0x1C) | 0x8u) & ~0x11u; // always place, awake, visible
        *(short*)(location + 0x20) = (short)palette_index;
        for (int offset : {0x22, 0x24, 0x28, 0x3C, 0x60}) *(short*)(location + offset) = -1; // weapons, vehicle, emitter, script
        *(short*)(location + 0x2A) = 0; // seat type
        *(short*)(location + 0x2E) = 0; // swarm count
        *(unsigned*)(location + 0x30) = 0; // actor variant
        *(float*)(location + 0x38) = 0.0f; // initial movement distance
        *(short*)(location + 0x3E) = 0; // movement mode

        ScopedPoke<int> lent; // held until ai_place has armed the actor
        short weapon_index = WeaponPaletteIndex(weapon, Count(Scenario() + kWeaponPalette), PaletteWeaponTag, lent);
        *(short*)(location + 0x22) = weapon_index;

        LOG_INFO("Spawn: placing {} through squad {} location {}", name, spawn.squad, spawn.location);
        Call<void>(OFFSET_HALO2_PF_AI_PLACE, (3u << 30) | ((unsigned)spawn.squad << 16) | (unsigned)spawn.location);

        memcpy(squad, saved_squad.data(), kSquadSize);
        memcpy(location, saved_location.data(), kLocationSize);
        MCC::Command::ForgetChange(squad);
        MCC::Command::ForgetChange(location);

        return SpawnResult(LivingCount(spawn.squad) != 0, weapon_index != -1 ? WithWeapon(name, TagName(weapon)) : name);
    }

    // --- backend ---------------------------------------------------------------------------------

    // Characters only: on an MCC build without one of these, objects still spawn.
    static const AlphaRing::Feature s_characters("Halo 2 character spawning",
                                                 {OFFSET_HALO2_PF_AI_PLACE, OFFSET_HALO2_PF_AI_LIVING_COUNT});

    static void List(Category category, std::vector<Item>& out) {
        if (category == Characters) {
            if (!s_characters.Available()) return;
            std::vector<int> seen; // missions list some characters more than once
            for (int i = 0; i < Count(Scenario() + kCharacterPalette); ++i) {
                int tag = CharacterPaletteTag(i);
                if (tag == -1 || std::find(seen.begin(), seen.end(), tag) != seen.end()) continue;
                seen.push_back(tag);
                out.push_back({i, DisplayName(TagName(tag))});
            }
            return;
        }

        static const unsigned int groups[] = {GroupTag("vehi"), GroupTag("weap"), GroupTag("eqip")};
        ForEachTag(groups[category], [&](int tag) {
            const char* path = TagName(tag);
            if (path == nullptr) return;
            // mounted guns and set pieces (scenarios\objects\...) only work attached
            if (strstr(path, "\\turrets\\") || strncmp(path, "scenarios\\", 10) == 0) return;
            if (category == Weapons && MountedWeapon(path)) return;
            out.push_back({tag, DisplayName(path)});
        });
    }

    static std::string Spawn(Category category, int id, int player, Team team, int weapon) {
        if (category == Characters) {
            if (!s_characters.Available()) return "Characters can't be spawned in this version of Halo 2";
            return SpawnCharacter(id, player, team, weapon);
        }
        std::string name = DisplayName(TagName(id));
        return SpawnResult(SpawnObject(id, player, category) != -1, name);
    }

    // With the console hook's (module/entry/halo2/console.cpp), which runs the commands and goes in only with these.
    static const AlphaRing::Feature s_offsets("Halo 2 spawning", {
        OFFSET_HALO2_PF_HS_COMPILE, OFFSET_HALO2_PV_SHARED_TAG_BASE, OFFSET_HALO2_PV_TAG_BASE, OFFSET_HALO2_PV_SCENARIO,
        OFFSET_HALO2_PV_TAG_INSTANCES, OFFSET_HALO2_PV_TAG_COUNT, OFFSET_HALO2_PV_TAG_NAMES,
        OFFSET_HALO2_PV_TAG_NAME_OFFSETS, OFFSET_HALO2_PV_OBJECTS, OFFSET_HALO2_PV_OBJECT_MEMORY, OFFSET_HALO2_PV_PLAYERS,
        OFFSET_HALO2_PF_OBJECT_GET_ORIGIN, OFFSET_HALO2_PF_OBJECT_PLACEMENT_DATA_NEW, OFFSET_HALO2_PF_OBJECT_NEW});
    const AlphaRing::Feature& Offsets() { return s_offsets; }
    static const Backend s_backend = {List, Spawn, &s_offsets};
    static const bool s_registered = (RegisterBackend(kGame, &s_backend), true);
}
