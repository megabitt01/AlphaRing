#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Offset.h"

// Spawning: vehicles, weapons, equipment and AI characters in front of a player, in the campaigns of
// Halo CE, Halo 2, Halo 3 and ODST. Each supported game implements a Backend; its functions run on the
// game thread (mcc/spawn/Command.h).
namespace MCC::Spawn {
    enum Category : int { Vehicles, Weapons, Equipment, Characters, kCategoryCount };
    enum Team : int { TeamDefault, TeamAlly, TeamEnemy, kTeamCount };
    constexpr int kMaxPlayers = 4;

    // Local players in the running game (1 without split-screen).
    int LocalPlayerCount();

    struct Item {
        int id;           // backend-defined (a tag index or a palette index)
        std::string name; // display name
    };

    // A character's weapon: a tag from the Weapons list, or this for the one it usually carries.
    constexpr int kUsualWeapon = -1;

    struct Backend {
        // Fill `out` with what the loaded map can spawn in `category`.
        void (*list)(Category category, std::vector<Item>& out);
        // Spawn `id` in front of local player `player` (0-3), a character armed with `weapon`;
        // returns a short status message.
        std::string (*spawn)(Category category, int id, int player, Team team, int weapon);
        // The game offsets it uses, the hook that runs its commands included: on an MCC build
        // without one of them the game has no backend, and no spawning is offered.
        const AlphaRing::Feature* offsets;
    };

    void RegisterBackend(int game, const Backend* backend);

    // Replace `player`'s status line (for results known only after spawn returns).
    void ReportStatus(int player, const std::string& status);

    // The Spawn window (overlay menu bar).
    void ImGuiContext();
}

// Shared by the Spawn window and the players' menus (implementation detail of mcc/spawn).
namespace MCC::Spawn::Catalog {
    // The game whose catalog applies right now, or -1 outside a mission / without a backend.
    int CurrentGame();
    // Ask the game thread for a new catalog of `game` (automatic when the map changes).
    void Refresh(int game, bool force);
    // Calls `f` with `game`'s items of `category` under the catalog lock, or nullptr while
    // the catalog is loading.
    void Read(int game, Category category, const std::function<void(const std::vector<Item>*)>& f);
    int Size(int game, Category category);
    bool Get(int game, Category category, int index, Item& item);
    std::string Status(int player);
    // Spawns `item` in front of `player`; a character carries the Weapons item at
    // `weapon_index`, or its usual weapon when there's none.
    void Spawn(Category category, const Item& item, int player, Team team, int weapon_index);
    // Name of the weapon a character will carry for `weapon_index`.
    std::string WeaponName(int game, int weapon_index);
}
