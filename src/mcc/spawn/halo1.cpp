// Halo CE spawn backend. Mirrors what the retail cheat_spawn_warthog / cheat_all_chars
// script functions do: object_placement_data_new + object_new for any object tag, and for
// characters actor_customize_unit (weapons) + ai_attach_free (a free, encounterless actor).

#include "Backend.h"

#include "common.h"

#include "mcc/CGameGlobal.h"

#include <offset_halo1.h>

namespace MCC::Spawn::Halo1 {
    static constexpr int kGame = CGameGlobal::Halo1;

    template <typename R, typename... A>
    static R Call(__int64 rva, A... args) { return EngineCall<R>(kGame, rva, args...); }

    struct TagIterator {
        unsigned __int64 reserved0 = 0;
        unsigned short reserved1 = 0;
        int current = -1;
        unsigned int group = 0;
        unsigned int reserved2[3] = {};
    };

    template <typename F>
    static void ForEachTag(unsigned int group, F&& f) {
        TagIterator it;
        it.group = group;
        for (int tag; (tag = Call<int>(OFFSET_HALO1_PF_TAG_ITERATOR_NEXT, &it)) != -1;) f(tag);
    }

    static const char* TagName(int tag) { return Call<const char*>(OFFSET_HALO1_PF_TAG_GET_NAME, tag); }
    static char* TagData(int tag) { return Call<char*>(OFFSET_HALO1_PF_TAG_GET, tag); }

    // actor_variant: +0x14 'unit' tag reference, whose tag index sits 12 bytes in.
    static int ActorVariantUnit(int actor_variant) { return *(int*)(TagData(actor_variant) + 0x20); }

    // The unit of the player local player `player` controls. Players globals keep the engine's player mapping
    // (as Halo 3's): the player datum per input user at +0x4, -1 for none (set at 0xAE43FC, with the unit at +0x14).
    static int PlayerUnit(int player) {
        constexpr int kPlayerSlots = 16;
        auto globals = EngineGlobal<char*>(kGame, OFFSET_HALO1_PV_LOCAL_PLAYERS);
        auto array = EngineGlobal<char*>(kGame, OFFSET_HALO1_PV_PLAYERS);
        if (globals == nullptr || array == nullptr || player < 0 || player >= kMaxPlayers) return -1;
        int handle = *(int*)(globals + 0x4 + 4 * player);
        if (handle == -1 || (handle & 0xFFFF) >= kPlayerSlots) return -1;
        auto element = array + *(int*)(array + 0x34) + (handle & 0xFFFF) * 0xC20;
        if (*(short*)element == 0) return -1; // free slot
        return *(int*)(element + 0x64);
    }

    // Object data by object type: bipeds and vehicles are units.
    enum : unsigned { kUnitTypes = 1 << 0 | 1 << 1, kWeaponType = 1 << 2 };
    static char* ObjectData(int object, unsigned types) { return Call<char*>(OFFSET_HALO1_PF_OBJECT_TRY_AND_GET, object, types); }

    // Whether `unit` carries a weapon made from tag `weapon` (unit +0x2D8: four weapon slots).
    static bool Carries(int unit, int weapon) {
        auto data = ObjectData(unit, kUnitTypes);
        if (data == nullptr) return false;
        for (int i = 0; i < 4; ++i) {
            auto held = ObjectData(((int*)(data + 0x2D8))[i], kWeaponType);
            if (held != nullptr && *(int*)held == weapon) return true; // object +0: its tag
        }
        return false;
    }

    // Returns the new object index, or -1. TeamDefault keeps the object's own team.
    static int SpawnObject(int tag, int player, Category category, Team team) {
        int unit = PlayerUnit(player);
        auto body = ObjectData(unit, kUnitTypes);
        if (body == nullptr) return -1;

        // unit +0x204: where the player looks (desired facing). The body turns only once the look
        // strays far enough, so its forward can be up to a right angle off.
        Vector3 origin;
        Call<void>(OFFSET_HALO1_PF_OBJECT_GET_ORIGIN, unit, &origin);
        auto place = PlaceAhead(origin, *(Vector3*)(body + 0x204), SpawnDistance(category), 0.8f);

        alignas(16) char data[0x100] = {}; // object_placement_data is 0x8C bytes
        Call<void>(OFFSET_HALO1_PF_OBJECT_PLACEMENT_DATA_NEW, data, tag, -1);
        *(Vector3*)(data + 0x1C) = place.position;
        *(Vector3*)(data + 0x38) = place.forward;
        *(Vector3*)(data + 0x44) = place.up;
        // Allies join the player's own team (object +0x74): the human team is friendly only where
        // the mission's script allies it with the player, which The Maw's doesn't.
        short side = team == TeamAlly ? *(short*)(body + 0x74) : EngineTeam(team, -1);
        if (side >= 0) *(short*)(data + 0x18) = side;
        return Call<int>(OFFSET_HALO1_PF_OBJECT_NEW, data);
    }

    static void List(Category category, std::vector<Item>& out) {
        static const unsigned int groups[kCategoryCount] = {
            GroupTag("vehi"), GroupTag("weap"), GroupTag("eqip"), GroupTag("actv")};

        ForEachTag(groups[category], [&](int tag) {
            const char* path = TagName(tag);
            if (path == nullptr) return;
            // vehicle turrets are weapon tags nobody can carry
            if (category == Weapons && strncmp(path, "vehicles\\", 9) == 0) return;
            if (category == Characters && ActorVariantUnit(tag) == -1) return;
            out.push_back({tag, DisplayName(path)});
        });
    }

    static std::string Spawn(Category category, int tag, int player, Team team, int weapon) {
        std::string name = DisplayName(TagName(tag));

        if (category != Characters)
            return SpawnResult(SpawnObject(tag, player, category, TeamDefault) != -1, name);

        int unit = SpawnObject(ActorVariantUnit(tag), player, category, team);
        if (unit == -1) return SpawnResult(false, name);

        // actor_customize_unit arms the unit with the actor variant's weapon (+0x64, tag index
        // 12 bytes in); a chosen weapon stands in for it during the call. The game refuses a
        // weapon the character has no animations for (an Elite with a human weapon), and the
        // actor then keeps its usual one.
        bool chosen = weapon != kUsualWeapon;
        {
            ScopedPoke<int> variant_weapon;
            if (chosen) variant_weapon.Set((int*)(TagData(tag) + 0x70), weapon);
            Call<void>(OFFSET_HALO1_PF_ACTOR_CUSTOMIZE_UNIT, tag, unit);
        }
        bool refused = chosen && !Carries(unit, weapon);

        Call<void>(OFFSET_HALO1_PF_AI_ATTACH_FREE, unit, tag);
        if (refused) return "Can't use " + DisplayName(TagName(weapon)) + ": " + name; // the menu cuts long lines
        return SpawnResult(true, chosen ? WithWeapon(name, TagName(weapon)) : name);
    }

    // With the console hook's (module/entry/halo1/console.cpp), which runs the commands and goes in only with these.
    static const AlphaRing::Feature s_offsets("Halo CE spawning", {
        OFFSET_HALO1_PF_HS_COMPILE, OFFSET_HALO1_PF_TAG_ITERATOR_NEXT, OFFSET_HALO1_PF_TAG_GET_NAME,
        OFFSET_HALO1_PF_TAG_GET, OFFSET_HALO1_PV_PLAYERS, OFFSET_HALO1_PV_LOCAL_PLAYERS, OFFSET_HALO1_PF_OBJECT_TRY_AND_GET,
        OFFSET_HALO1_PF_OBJECT_GET_ORIGIN, OFFSET_HALO1_PF_OBJECT_PLACEMENT_DATA_NEW, OFFSET_HALO1_PF_OBJECT_NEW,
        OFFSET_HALO1_PF_ACTOR_CUSTOMIZE_UNIT, OFFSET_HALO1_PF_AI_ATTACH_FREE});
    const AlphaRing::Feature& Offsets() { return s_offsets; }
    static const Backend s_backend = {List, Spawn, &s_offsets};
    static const bool s_registered = (RegisterBackend(kGame, &s_backend), true);
}
