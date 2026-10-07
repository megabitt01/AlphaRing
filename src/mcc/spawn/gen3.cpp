// Halo 3 and ODST spawn backend (the two share an engine; the differences are addresses and
// the scenario's squad layout).
//
// Objects: object_placement_data_new + object_new, as the engine's own actor placement does.
// Characters: neither game has a retail script function that gives a spawned body an AI, so a
// character is placed through the engine's squad code instead: one spawn point of an empty
// squad in the loaded scenario is pointed at the player and the chosen character for the
// duration of an ai_place of that point, then restored. The squad's team decides whether the
// character fights with or against the players.
//
// Weapons: squads arm their actors only from the scenario's weapon palette (an unset weapon
// means none), so the spawn point names a palette entry for the chosen weapon, or for the one
// the mission usually gives that character; a weapon the palette lacks is lent a palette entry
// while ai_place runs (it arms the actor before returning).

#include "Backend.h"

#include "common.h"

#include "mcc/CGameGlobal.h"
#include "mcc/CGameManager.h"

#include <offset_halo3.h>
#include <offset_halo3odst.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

namespace MCC::Spawn::Gen3 {
    struct Game {
        int id;
        const AlphaRing::Offset &tags_header, &tag_base, &tag_names, &scenario;
        const AlphaRing::Offset &placement_new, &object_new, &post_create, &ai_place, &tag_loaded;
        const AlphaRing::Offset& world; // the simulation tick commands run on (module/entry/halo3/world.cpp, halo3odst)

        // scenario layout (Assembly Halo3MCC / ODSTMCC scnr plugins)
        int squads, squad_size, squad_objective; // objective index, task index follows it
        int zones, zone_size, zone_firing_positions;
        int character_palette, weapon_palette;
        int character_weapons; // the character tag's "weapons properties" block
        // Halo 3 squads place from fire teams' starting locations; ODST squads from "single
        // locations" that carry the character themselves.
        bool single_locations;
        int location_size, location_position;
    };

    static constexpr Game kHalo3 = {
        CGameGlobal::Halo3,
        OFFSET_HALO3_PV_TAGS_HEADER, OFFSET_HALO3_PV_TAG_BASE, OFFSET_HALO3_PV_TAG_NAMES, OFFSET_HALO3_PV_SCENARIO,
        OFFSET_HALO3_PF_OBJECT_PLACEMENT_DATA_NEW, OFFSET_HALO3_PF_OBJECT_NEW, OFFSET_HALO3_PF_OBJECT_POST_CREATE,
        OFFSET_HALO3_PF_AI_PLACE, OFFSET_HALO3_PF_TAG_LOADED,
        OFFSET_HALO3_PF_WORLD,
        0x384, 0x40, 0x2C,
        0x390, 0x40, 0x28,
        0x3A8, 0x120,
        0x174,
        false, 0x88, 0x08,
    };

    static constexpr Game kHalo3ODST = {
        CGameGlobal::Halo3ODST,
        OFFSET_HALO3ODST_PV_TAGS_HEADER, OFFSET_HALO3ODST_PV_TAG_BASE, OFFSET_HALO3ODST_PV_TAG_NAMES,
        OFFSET_HALO3ODST_PV_SCENARIO,
        OFFSET_HALO3ODST_PF_OBJECT_PLACEMENT_DATA_NEW, OFFSET_HALO3ODST_PF_OBJECT_NEW,
        OFFSET_HALO3ODST_PF_OBJECT_POST_CREATE, OFFSET_HALO3ODST_PF_AI_PLACE, OFFSET_HALO3ODST_PF_TAG_LOADED,
        OFFSET_HALO3ODST_PF_WORLD,
        0x3B8, 0x6C, 0x2A,
        0x3C4, 0x3C, 0x24,
        0x3E8, 0x13C,
        0x198,
        true, 0x90, 0x14,
    };

    template <typename R, typename... A>
    static R Call(const Game& g, __int64 rva, A... args) { return EngineCall<R>(g.id, rva, args...); }

    // --- tags -------------------------------------------------------------------------------

    struct TagInstance { short group_index; unsigned short salt; unsigned int address; };
    struct TagGroup { unsigned int tag, parent, grandparent, name; };
    struct TagsHeader { __int64 group_count; TagGroup* groups; __int64 tag_count; TagInstance* instances; };
    struct Block { int count; unsigned int address; int reserved; };

    static char* Address(const Game& g, unsigned int address) {
        return address ? EngineGlobal<char*>(g.id, g.tag_base) + ((__int64)address << 2) : nullptr;
    }
    static char* Element(const Game& g, const char* block, int index, int size) {
        auto b = (const Block*)block;
        return (index >= 0 && index < b->count) ? Address(g, b->address) + (__int64)index * size : nullptr;
    }
    static int Count(const char* block) { return ((const Block*)block)->count; }
    static char* Scenario(const Game& g) { return EngineGlobal<char*>(g.id, g.scenario); }

    static char* TagData(const Game& g, int tag) {
        auto tags = EngineGlobal<TagsHeader*>(g.id, g.tags_header);
        int index = tag & 0xFFFF;
        return (tags && tag != -1 && index < tags->tag_count) ? Address(g, tags->instances[index].address) : nullptr;
    }

    // tag names: int offsets[0x8000], char buffer[0x800000], const char* names[0x8000]
    static const char* TagName(const Game& g, int tag) {
        auto names = EngineGlobal<char*>(g.id, g.tag_names);
        int index = tag & 0xFFFF;
        return (names && index < 0x8000) ? ((const char**)(names + 0x820000))[index] : nullptr;
    }

    // Only tags in the loaded zone set can be created; the rest of the map's tags are listed
    // in the tag table all the same.
    static bool TagLoaded(const Game& g, int tag) { return Call<bool>(g, g.tag_loaded, tag); }

    template <typename F>
    static void ForEachTag(const Game& g, unsigned int group, F&& f) {
        auto tags = EngineGlobal<TagsHeader*>(g.id, g.tags_header);
        if (tags == nullptr) return;
        for (int i = 0; i < tags->tag_count; ++i) {
            auto& instance = tags->instances[i];
            if (instance.group_index < 0 || instance.group_index >= tags->group_count) continue;
            if (tags->groups[instance.group_index].tag != group) continue;
            f(((int)instance.salt << 16) | i);
        }
    }

    // --- game state ---------------------------------------------------------------------------

    // Blam data array: name +0, max +0x20, element size +0x24, count +0x40, data +0x48.
    struct DataArray {
        char name[0x20];
        int max, size, reserved[6], count, reserved2;
        char* data;

        char* At(int index) const {
            index &= 0xFFFF;
            return index < max ? data + (__int64)size * index : nullptr;
        }
        char* Get(int index) const { // null for a free slot (zero salt)
            char* datum = At(index);
            return datum && *(short*)datum ? datum : nullptr;
        }
    };

    static DataArray* ArrayIn(DataArray* const* slot, const char* name) {
        __try {
            DataArray* array = *slot;
            return array && strcmp(array->name, name) == 0 && array->size > 0 && array->data ? array : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    // The game state lives in the module's thread-local block (valid on the game thread,
    // where backends run). Its slot order differs between the two games, so arrays are found
    // by name; the slot is remembered.
    static DataArray* FindArray(const Game& g, const char* name, int& slot) {
        ThreadLocalStorage tls;
        if (!tls.update(MCC::Command::ModuleBase(g.id))) return nullptr;
        auto block = (DataArray**)tls.m_pTLS[tls.m_TlsIndex];

        if (slot >= 0)
            if (auto array = ArrayIn(block + slot, name)) return array;
        for (int i = 0; i < 256; ++i)
            if (auto array = ArrayIn(block + i, name)) return slot = i, array;
        return nullptr;
    }

    struct Arrays { int players = -1, objects = -1, squads = -1; };
    static Arrays& Slots(const Game& g) {
        static Arrays halo3, odst;
        return g.id == CGameGlobal::Halo3 ? halo3 : odst;
    }

    // Local player `player`'s unit position and facing.
    static bool PlayerView(const Game& g, int player, Vector3& origin, Vector3& facing) {
        auto players = FindArray(g, "players", Slots(g).players);
        auto objects = FindArray(g, "object", Slots(g).objects);
        if (players == nullptr || objects == nullptr) return false;
        auto datum = players->Get(player);
        if (datum == nullptr) return false;
        int unit = *(int*)(datum + 0x28);
        auto header = unit == -1 ? nullptr : objects->Get(unit);
        auto object = header ? *(char**)(header + 0x10) : nullptr;
        if (object == nullptr) return false;
        origin = *(Vector3*)(object + 0x50);
        facing = *(Vector3*)(object + 0x5C);
        return true;
    }

    // A squad with living members won't place more actors from its spawn points.
    static bool SquadIsEmpty(const Game& g, int squad) {
        auto squads = FindArray(g, "squad", Slots(g).squads);
        auto datum = squads ? squads->At(squad) : nullptr;
        return datum && *(int*)(datum + 0x10) == 0; // living members
    }

    // --- objects -----------------------------------------------------------------------------

    static int SpawnObject(const Game& g, int tag, int player, Category category) {
        Vector3 origin, facing;
        if (!PlayerView(g, player, origin, facing)) return -1;
        auto place = PlaceAhead(origin, facing, SpawnDistance(category), 0.5f);

        alignas(16) char data[0x180] = {}; // object_placement_data
        Call<void>(g, g.placement_new, data, tag, -1, (void*)nullptr);
        *(Vector3*)(data + 0x1C) = place.position;
        *(Vector3*)(data + 0x28) = place.forward;
        *(Vector3*)(data + 0x34) = place.up;
        // a small toss wakes the object's physics so it settles instead of floating
        if (category != Vehicles)
            *(Vector3*)(data + 0x40) = {place.forward.x, place.forward.y, 1.0f};
        int object = Call<int>(g, g.object_new, data);
        if (object != -1) Call<void>(g, g.post_create, object);
        return object;
    }

    // --- characters ---------------------------------------------------------------------------

    static int CharacterPaletteTag(const Game& g, int index) {
        auto entry = Element(g, Scenario(g) + g.character_palette, index, 0x10);
        return entry ? *(int*)(entry + 0xC) : -1;
    }

    static int* PaletteWeaponTag(const Game& g, int index) {
        auto entry = Element(g, Scenario(g) + g.weapon_palette, index, 0x10);
        return entry ? (int*)(entry + 0xC) : nullptr;
    }
    static int PaletteWeapon(const Game& g, int index) {
        auto tag = PaletteWeaponTag(g, index);
        return tag ? *tag : -1;
    }

    // Character tag -> weapon tag -> how many of the mission's spawn points (Halo 3) or designer
    // cells (ODST) give that character that weapon.
    using WeaponVotes = std::map<int, std::map<int, int>>;

    static WeaponVotes MissionWeapons(const Game& g) {
        WeaponVotes votes;
        auto vote = [&](short character_index, short weapon_index) {
            int character = CharacterPaletteTag(g, character_index), weapon = PaletteWeapon(g, weapon_index);
            if (character != -1 && weapon != -1) ++votes[character][weapon];
        };
        auto scenario = Scenario(g);
        for (int s = 0; s < Count(scenario + g.squads); ++s) {
            auto squad = Element(g, scenario + g.squads, s, g.squad_size);
            if (g.single_locations) {
                for (int p = 0; p < Count(squad + 0x3C); ++p) {
                    auto location = Element(g, squad + 0x3C, p, g.location_size);
                    vote(*(short*)(location + 0x32), *(short*)(location + 0x34));
                }
                for (int c = 0; c < Count(squad + 0x54); ++c) { // cells: character and weapon choices
                    auto cell = Element(g, squad + 0x54, c, 0x84);
                    for (int t = 0; t < Count(cell + 0x14); ++t)
                        for (int w = 0; w < Count(cell + 0x20); ++w)
                            vote(*(short*)(Element(g, cell + 0x14, t, 0x10) + 0xC), *(short*)(Element(g, cell + 0x20, w, 0x10) + 0xC));
                }
                continue;
            }
            for (int f = 0; f < Count(squad + 0x30); ++f) {
                auto fire_team = Element(g, squad + 0x30, f, 0x60);
                for (int p = 0; p < Count(fire_team + 0x54); ++p) { // a location's own choices win over its fire team's
                    auto location = Element(g, fire_team + 0x54, p, g.location_size);
                    short c = *(short*)(location + 0x28), w = *(short*)(location + 0x2A);
                    vote(c != -1 ? c : *(short*)(fire_team + 0x08), w != -1 ? w : *(short*)(fire_team + 0x0A));
                }
            }
        }
        return votes;
    }

    // The weapon the mission usually gives `character`: the loaded one it's given most often.
    // Otherwise the first weapon the character tag knows how to use (weapons properties,
    // inherited from parent characters). The mission's choices are counted once per map, before
    // any spawn: spawn points lent to recent spawns still hold our changes.
    static int UsualWeapon(const Game& g, int character) {
        static struct { unsigned generation = ~0u; WeaponVotes votes; } s_cache[2]; // Halo 3, ODST
        auto& cache = s_cache[g.single_locations];
        if (cache.generation != CGameManager::load_generation())
            cache = {CGameManager::load_generation(), MissionWeapons(g)};

        std::map<int, int> loaded;
        for (auto [weapon, count] : cache.votes[character])
            if (TagLoaded(g, weapon)) loaded[weapon] = count;
        if (int weapon = MostVoted(loaded); weapon != -1) return weapon;

        for (int depth = 0; depth < 8; ++depth) {
            auto data = TagData(g, character);
            if (data == nullptr) return -1;
            auto block = data + g.character_weapons;
            if (Count(block) > 0) {
                for (int i = 0; i < Count(block); ++i) {
                    int weapon = *(int*)(Element(g, block, i, 0xE0) + 0x10); // weapon tag reference
                    if (weapon != -1 && TagLoaded(g, weapon) && !MountedWeapon(TagName(g, weapon))) return weapon;
                }
                return -1;
            }
            character = *(int*)(data + 0x10); // parent character
        }
        return -1;
    }

    struct SpawnPoint {
        int squad, fire_team, point; // fire_team is -1 in ODST
        char *squad_data, *fire_team_data, *location;
        char* cell; // ODST: the designer cell a single location is placed through
    };

    // Squads lent to a spawn and not put back yet (RestoreLater; game thread). Lent again, a squad
    // would have the first spawn's changes saved as its own, and put back over the original.
    static struct { unsigned generation = ~0u; std::vector<const char*> squads; } s_lent;

    static std::vector<const char*>& LentSquads() {
        if (s_lent.generation != CGameManager::load_generation()) s_lent = {CGameManager::load_generation(), {}};
        return s_lent.squads;
    }

    // The spawn point nearest to `position` among empty squads: it lies in a loaded part of
    // the map, and the squad's own members won't mix with the spawned character.
    static bool NearestSpawnPoint(const Game& g, const Vector3& position, SpawnPoint& out) {
        auto& lent = LentSquads();
        auto scenario = Scenario(g);
        float best = 1e30f;
        auto consider = [&](const SpawnPoint& point) {
            float distance = Distance2(*(const Vector3*)(point.location + g.location_position), position);
            if (distance < best) { best = distance; out = point; }
        };

        for (int s = 0; s < Count(scenario + g.squads); ++s) {
            if (!SquadIsEmpty(g, s)) continue;
            auto squad = Element(g, scenario + g.squads, s, g.squad_size);
            if (std::find(lent.begin(), lent.end(), squad) != lent.end()) continue;
            if (g.single_locations) {
                for (int p = 0; p < Count(squad + 0x3C) && p < 0x100; ++p) {
                    auto location = Element(g, squad + 0x3C, p, g.location_size);
                    // the engine only places a single location through its (designer) cell
                    if (auto cell = Element(g, squad + 0x54, *(short*)(location + 0x10), 0x84))
                        consider({s, -1, p, squad, nullptr, location, cell});
                }
                continue;
            }
            for (int f = 0; f < Count(squad + 0x30) && f < 0x100; ++f) {
                auto fire_team = Element(g, squad + 0x30, f, 0x60);
                for (int p = 0; p < Count(fire_team + 0x54) && p < 0x100; ++p)
                    consider({s, f, p, squad, fire_team, Element(g, fire_team + 0x54, p, g.location_size), nullptr});
            }
        }
        return best < 1e30f;
    }

    // The AI zone with a firing position nearest to `position`: squad members move and fight
    // within their zone, so without this they walk off to the borrowed squad's area.
    static short NearestZone(const Game& g, const Vector3& position) {
        auto scenario = Scenario(g);
        short nearest = -1;
        float best = 1e30f;
        for (int z = 0; z < Count(scenario + g.zones); ++z) {
            auto positions = Element(g, scenario + g.zones, z, g.zone_size) + g.zone_firing_positions;
            for (int f = 0; f < Count(positions); ++f) {
                float distance = Distance2(*(const Vector3*)Element(g, positions, f, 0x28), position);
                if (distance < best) { best = distance; nearest = (short)z; }
            }
        }
        return nearest;
    }

    // Borrowed squad data is put back a moment later: the engine finishes placing the actor
    // over the next ticks and reads the spawn point again while doing so.
    struct Borrowed {
        char* address;
        std::vector<char> saved;
    };

    struct PendingRestore {
        std::vector<Borrowed> borrowed;
        int squad, player;
        std::string name;
        const char* squad_data;
    };

    static void RestoreLater(const Game& g, std::shared_ptr<PendingRestore> pending, int ticks) {
        MCC::Command::Schedule(g.id, [&g, pending, ticks] {
            if (ticks > 0) return RestoreLater(g, pending, ticks - 1);
            for (auto& b : pending->borrowed) memcpy(b.address, b.saved.data(), b.saved.size());
            auto& lent = LentSquads();
            lent.erase(std::remove(lent.begin(), lent.end(), pending->squad_data), lent.end());
            if (SquadIsEmpty(g, pending->squad))
                ReportStatus(pending->player, "Couldn't spawn " + pending->name + " here");
        });
    }

    // Halo 3: the fire team and its starting location both carry character and loadout.
    static void PrepareFireTeamPoint(const SpawnPoint& spawn, int palette_index, short weapon, const Placement& place) {
        auto fire_team = spawn.fire_team_data, location = spawn.location;

        *(short*)(fire_team + 0x08) = (short)palette_index;
        for (int offset : {0x0A, 0x0C, 0x10, 0x12, 0x38, 0x42}) *(short*)(fire_team + offset) = -1;
        *(short*)(fire_team + 0x40) = 0; // no activity
        ((Block*)(fire_team + 0x48))->count = 0; // no patrol points

        // spawn on every difficulty, always, visible, without a scripted activity
        *(short*)(location + 0x00) = 0xF;
        *(unsigned*)(location + 0x24) = (*(unsigned*)(location + 0x24) | 0x4u) & ~0x8u;
        *(unsigned*)(location + 0x70) = 0; // activity name
        *(short*)(location + 0x74) = 0;
        *(short*)(location + 0x78) = -1; // point set
        ((Block*)(location + 0x7C))->count = 0;
        *(short*)(location + 0x4A) = 0; // movement mode

        *(Vector3*)(location + 0x08) = place.position;
        *(short*)(location + 0x14) = -1; // no reference frame: world coordinates
        *(float*)(location + 0x18) = place.YawToPlayer();
        *(float*)(location + 0x1C) = 0.0f;
        *(float*)(location + 0x20) = 0.0f;
        *(short*)(location + 0x28) = (short)palette_index;
        for (int offset : {0x2C, 0x30, 0x44, 0x46, 0x48, 0x6C, 0x6E}) *(short*)(location + offset) = -1;
        *(short*)(location + 0x2A) = weapon;
        *(short*)(location + 0x32) = 0; // seat type: none
        *(short*)(location + 0x36) = 0; // swarm count
    }

    // ODST: a single location's own character and loadout win; where they're unset the
    // engine picks from its cell's lists, so the cell's loadout choices are emptied too.
    static void PrepareSingleLocation(const SpawnPoint& spawn, int palette_index, short weapon, const Placement& place) {
        auto cell = spawn.cell, location = spawn.location;

        *(short*)(cell + 0x04) = 0xF; // every difficulty
        for (int block : {0x20, 0x2C, 0x38, 0x78}) ((Block*)(cell + block))->count = 0; // weapons, equipment, points
        for (int offset : {0x46, 0x6C, 0x74}) *(short*)(cell + offset) = -1; // vehicle, command script, point set

        *(short*)(location + 0x00) = 0xF; // every difficulty
        *(unsigned short*)(location + 0x30) = (*(unsigned short*)(location + 0x30) | 0x4) & ~0x8; // always place, visible
        *(Vector3*)(location + 0x14) = place.position;
        *(short*)(location + 0x20) = -1; // no reference frame: world coordinates
        *(float*)(location + 0x24) = place.YawToPlayer();
        *(float*)(location + 0x28) = 0.0f;
        *(float*)(location + 0x2C) = 0.0f;
        *(short*)(location + 0x32) = (short)palette_index;
        for (int offset : {0x36, 0x38, 0x3A, 0x52, 0x54, 0x56, 0x78, 0x80}) *(short*)(location + offset) = -1;
        *(short*)(location + 0x34) = weapon;
        *(short*)(location + 0x3C) = 0; // seat type: none
        *(short*)(location + 0x40) = 0; // swarm count
        *(unsigned*)(location + 0x44) = 0; // actor variant
        *(short*)(location + 0x50) = 0; // movement mode
        *(unsigned*)(location + 0x7C) = 0; // activity name
        ((Block*)(location + 0x84))->count = 0; // no patrol points
    }

    static std::string SpawnCharacter(const Game& g, int palette_index, int player, Team team, int weapon) {
        int character = CharacterPaletteTag(g, palette_index);
        std::string name = character == -1 ? "character" : DisplayName(TagName(g, character));

        Vector3 origin, facing;
        if (!PlayerView(g, player, origin, facing)) return "No player " + std::to_string(player + 1);
        auto place = PlaceAhead(origin, facing, SpawnDistance(Characters), 0.3f);

        SpawnPoint spawn;
        if (!NearestSpawnPoint(g, origin, spawn))
            return "This mission has no free squads to spawn characters with";
        if (weapon == kUsualWeapon) weapon = UsualWeapon(g, character);

        auto pending = std::make_shared<PendingRestore>(PendingRestore{{}, spawn.squad, player, name, spawn.squad_data});
        for (auto [address, size] : {std::pair{spawn.squad_data, g.squad_size}, {spawn.fire_team_data, 0x60},
                                     {spawn.location, g.location_size}, {spawn.cell, 0x84}})
            if (address) {
                pending->borrowed.push_back({address, std::vector<char>(address, address + size)});
                MCC::Command::RecordChange(address, size); // put back at once if ai_place faults
            }

        // A free-roaming squad: no blind/deaf/braindead flags, no scripted objective or task.
        auto squad = spawn.squad_data;
        *(unsigned*)(squad + 0x20) &= ~0xEu;
        *(short*)(squad + 0x24) = EngineTeam(team, 0); // 0: the character's own team
        *(short*)(squad + 0x28) = NearestZone(g, place.position);
        *(short*)(squad + g.squad_objective) = -1;
        *(short*)(squad + g.squad_objective + 2) = -1; // task

        ScopedPoke<int> lent; // held until ai_place has armed the actor
        short weapon_index = WeaponPaletteIndex(weapon, Count(Scenario(g) + g.weapon_palette),
                                                [&](int i) { return PaletteWeaponTag(g, i); }, lent);

        if (g.single_locations) PrepareSingleLocation(spawn, palette_index, weapon_index, place);
        else PrepareFireTeamPoint(spawn, palette_index, weapon_index, place);

        // ai index of one spawn point: (4 << 29) | (squad << 16) | [fire team << 8 |] point
        unsigned int ai = (4u << 29) | ((unsigned)(spawn.squad & 0x1FFF) << 16) | spawn.point;
        if (!g.single_locations) ai |= spawn.fire_team << 8;
        LOG_INFO("Spawn: placing {} through squad {} point {}", name, spawn.squad, spawn.point);
        Call<void>(g, g.ai_place, ai, false);

        for (auto& b : pending->borrowed) MCC::Command::ForgetChange(b.address); // RestoreLater's now
        LentSquads().push_back(spawn.squad_data);
        RestoreLater(g, std::move(pending), 60);
        return SpawnResult(true, weapon_index != -1 ? WithWeapon(name, TagName(g, weapon)) : name);
    }

    // --- backend ---------------------------------------------------------------------------------

    template <const Game& g>
    static void List(Category category, std::vector<Item>& out) {
        if (category == Characters) {
            std::vector<int> seen; // missions list some characters more than once
            for (int i = 0; i < Count(Scenario(g) + g.character_palette); ++i) {
                int tag = CharacterPaletteTag(g, i);
                if (tag == -1 || !TagLoaded(g, tag) || std::find(seen.begin(), seen.end(), tag) != seen.end()) continue;
                seen.push_back(tag);
                out.push_back({i, DisplayName(TagName(g, tag))});
            }
            return;
        }

        static const unsigned int groups[] = {GroupTag("vehi"), GroupTag("weap"), GroupTag("eqip")};
        ForEachTag(g, groups[category], [&](int tag) {
            const char* path = TagName(g, tag);
            if (path == nullptr || !TagLoaded(g, tag)) return;
            // mounted guns are separate vehicle and weapon tags that only work attached, and
            // objects\levels\ holds set pieces (security cameras, holograms)
            if (strstr(path, "\\turrets\\") || strstr(path, "levels\\")) return;
            if (category == Weapons && MountedWeapon(path)) return;
            out.push_back({tag, DisplayName(path)});
        });
    }

    template <const Game& g>
    static std::string Spawn(Category category, int id, int player, Team team, int weapon) {
        if (category == Characters) return SpawnCharacter(g, id, player, team, weapon);
        std::string name = DisplayName(TagName(g, id));
        return SpawnResult(SpawnObject(g, id, player, category) != -1, name);
    }

    static AlphaRing::Feature Offsets(const char* name, const Game& g) {
        return {name, {g.tags_header, g.tag_base, g.tag_names, g.scenario, g.placement_new, g.object_new, g.post_create,
                       g.ai_place, g.tag_loaded, g.world}};
    }

    static const AlphaRing::Feature s_halo3_offsets = Offsets("Halo 3 spawning", kHalo3);
    static const AlphaRing::Feature s_halo3odst_offsets = Offsets("ODST spawning", kHalo3ODST);
    static const Backend s_halo3 = {List<kHalo3>, Spawn<kHalo3>, &s_halo3_offsets};
    static const Backend s_halo3odst = {List<kHalo3ODST>, Spawn<kHalo3ODST>, &s_halo3odst_offsets};
    static const bool s_registered = (RegisterBackend(kHalo3.id, &s_halo3), RegisterBackend(kHalo3ODST.id, &s_halo3odst), true);
}
