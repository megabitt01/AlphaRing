#pragma once

// Helpers shared by the per-game spawn backends (implementation detail of mcc/spawn).

#include "Spawn.h"
#include "Command.h"

#include <cmath>
#include <cstring>
#include <string>

namespace MCC::Spawn {
    struct Vector3 { float x, y, z; };

    // Calls a function inside a game module: EngineCall<R>(game, rva, args...).
    template <typename R, typename... A>
    inline R EngineCall(int game, __int64 rva, A... args) {
        return reinterpret_cast<R(__fastcall*)(A...)>(MCC::Command::ModuleBase(game) + rva)(args...);
    }

    // Reads a global variable of a game module.
    template <typename T>
    inline T EngineGlobal(int game, __int64 rva) { return *(T*)(MCC::Command::ModuleBase(game) + rva); }

    // Blam group tags are four characters packed big-endian: 'vehi' -> 0x76656869.
    constexpr unsigned int GroupTag(const char (&s)[5]) {
        return (unsigned)s[0] << 24 | (unsigned)s[1] << 16 | (unsigned)s[2] << 8 | (unsigned)s[3];
    }

    // Where to put a spawned object: `distance` ahead of an origin along its horizontal
    // facing, raised by `height`, facing the same way.
    struct Placement {
        Vector3 position, forward, up;
        // Yaw of a character standing here looking back at the player.
        float YawToPlayer() const { return std::atan2(-forward.y, -forward.x); }
    };

    inline Placement PlaceAhead(const Vector3& origin, const Vector3& facing, float distance, float height) {
        float fx = facing.x, fy = facing.y;
        float length = std::sqrt(fx * fx + fy * fy);
        if (length < 1e-3f) { fx = 1.0f; fy = 0.0f; } else { fx /= length; fy /= length; }
        return {{origin.x + fx * distance, origin.y + fy * distance, origin.z + height}, {fx, fy, 0.0f}, {0.0f, 0.0f, 1.0f}};
    }

    inline float Distance2(const Vector3& a, const Vector3& b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // Weapon tags nobody can carry: mounted on a vehicle or a stand, or built into a character
    // (a Monitor's welder beam).
    inline bool MountedWeapon(const char* path) {
        return path == nullptr || strstr(path, "\\turrets\\") || strstr(path, "\\vehicles\\") ||
               strstr(path, "\\characters\\") || strstr(path, "_integrated");
    }

    // The most common of the weapons voted for (tag -> votes), or -1.
    template <typename Votes>
    inline int MostVoted(const Votes& votes) {
        int best = -1, most = 0;
        for (auto& [weapon, count] : votes)
            if (count > most) { best = weapon; most = count; }
        return best;
    }

    // Writes a value over game data for as long as it's in scope, then puts the old one back
    // (also if the game faults in the meantime: see Command::RecordChange).
    template <typename T>
    struct ScopedPoke {
        T* at = nullptr;
        T saved{};

        ScopedPoke() = default;
        ScopedPoke(const ScopedPoke&) = delete;
        ScopedPoke& operator=(const ScopedPoke&) = delete;
        ~ScopedPoke() {
            if (at == nullptr) return;
            *at = saved;
            MCC::Command::ForgetChange(at);
        }

        void Set(T* where, T value) {
            MCC::Command::RecordChange(where, sizeof(T));
            at = where;
            saved = *where;
            *where = value;
        }
    };

    // Squads arm their actors from the scenario's weapon palette (H2/H3/ODST): the index of the
    // palette entry that places `weapon` - its own entry, or else the last one, lent to `weapon`
    // through `lent` - or -1 for none. `tag_at(i)` is the tag index of entry i of `count`.
    template <typename TagAt>
    short WeaponPaletteIndex(int weapon, int count, TagAt tag_at, ScopedPoke<int>& lent) {
        if (weapon == -1 || count == 0) return -1;
        for (short i = 0; i < count; ++i)
            if (*tag_at(i) == weapon) return i;
        lent.Set(tag_at(count - 1), weapon);
        return (short)(count - 1);
    }

    // Blam team indices: 2 human (the players' side), 3 covenant; `own` keeps the character's.
    inline short EngineTeam(Team team, short own) { return team == TeamAlly ? 2 : team == TeamEnemy ? 3 : own; }

    inline std::string SpawnResult(bool spawned, const std::string& name) {
        return (spawned ? "Spawned " : "Couldn't spawn ") + name;
    }

    // Spawn distances per category: vehicles need room, pickups land at the player's feet.
    inline float SpawnDistance(Category category) {
        switch (category) {
            case Vehicles: return 5.0f;
            case Characters: return 3.0f;
            default: return 1.5f;
        }
    }

    // "objects\characters\brute\ai\brute_captain" -> "Brute Captain": the last part of a tag
    // path, in words. Short vowel-less words are acronyms ("smg" -> "SMG", "br_ammo" -> "BR Ammo").
    inline std::string DisplayName(const char* path) {
        const char* start = path ? path : "";
        if (const char* slash = strrchr(start, '\\')) start = slash + 1;

        std::string name, word;
        auto flush = [&] {
            if (word.empty()) return;
            bool acronym = word.size() <= 4 && word.find_first_of("aeiouy") == std::string::npos;
            for (auto& c : word) c = acronym ? (char)toupper((unsigned char)c) : c;
            word[0] = (char)toupper((unsigned char)word[0]);
            if (word == "Odst") word = "ODST";
            name += (name.empty() ? "" : " ") + word;
            word.clear();
        };
        for (const char* p = start; *p; ++p) {
            if (*p == '_' || *p == ' ') flush();
            else word += *p;
        }
        flush();
        return name.empty() ? "Unnamed" : name;
    }

    // "Marine" armed with objects\weapons\rifle\battle_rifle -> "Marine with Battle Rifle".
    inline std::string WithWeapon(const std::string& name, const char* weapon_path) {
        return name + " with " + DisplayName(weapon_path);
    }
}
