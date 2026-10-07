#include "Spawn.h"

#include "Command.h"

#include "common.h"

#include "global/Global.h"
#include "input/MenuConfig.h"
#include "mcc/mcc.h"
#include "mcc/CGameGlobal.h"
#include "mcc/CGameManager.h"
#include "mcc/module/Module.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace MCC::Spawn {
    static const char* kCategoryNames[kCategoryCount] = {"Vehicles", "Weapons", "Equipment", "Characters"};
    static constexpr int kGames = MCC::Module::MODULE_MCC; // one slot per game module

    static const Backend* s_backends[kGames]; // constant-initialized, so safe to fill from static initializers

    static const Backend* BackendOf(int game) {
        auto backend = (unsigned)game < kGames ? s_backends[game] : nullptr;
        return backend && backend->offsets->Available() ? backend : nullptr;
    }

    void RegisterBackend(int game, const Backend* backend) {
        if ((unsigned)game < kGames) s_backends[game] = backend;
    }

    int LocalPlayerCount() {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        return p_setting->b_override ? std::clamp(CGameManager::active_player_count(), 1, kMaxPlayers) : 1;
    }

    // Filled on the game thread, read by the menus on the render and input threads.
    static struct {
        std::mutex mutex;
        int game = -1;
        unsigned generation = 0; // CGameManager::load_generation() the request was made for
        bool requested = false;
        std::vector<Item> items[kCategoryCount];
        std::string status[kMaxPlayers];
    } s_catalog;

    static bool ContainsNoCase(const std::string& text, const char* part) {
        auto end = part + strlen(part);
        return std::search(text.begin(), text.end(), part, end, [](char a, char b) {
            return tolower((unsigned char)a) == tolower((unsigned char)b);
        }) != text.end();
    }

    // Several tags can share a display name (e.g. two "Smg"); number the repeats.
    static void MakeNamesUnique(std::vector<Item>& items) {
        std::map<std::string, int> seen;
        for (auto& item : items)
            if (int n = ++seen[item.name]; n > 1) item.name += " " + std::to_string(n);
    }

    static void CmdList(int game, const Command::Args&) {
        auto backend = BackendOf(game);
        if (backend == nullptr) return;

        std::vector<Item> items[kCategoryCount];
        for (int c = 0; c < kCategoryCount; ++c) {
            backend->list((Category)c, items[c]);
            MakeNamesUnique(items[c]);
        }

        std::lock_guard<std::mutex> lock(s_catalog.mutex);
        s_catalog.game = game;
        for (int c = 0; c < kCategoryCount; ++c)
            s_catalog.items[c] = std::move(items[c]);
        LOG_INFO("Spawn: catalog for game {}: {} vehicles, {} weapons, {} equipment, {} characters", game,
                 s_catalog.items[Vehicles].size(), s_catalog.items[Weapons].size(),
                 s_catalog.items[Equipment].size(), s_catalog.items[Characters].size());
    }

    // "@ar spawn <category> <id> <player> <team> <weapon>"
    static void CmdSpawn(int game, const Command::Args& args) {
        auto backend = BackendOf(game);
        if (backend == nullptr || args.size() < 6) return;

        auto category = (Category)std::atoi(args[1].c_str());
        int player = std::atoi(args[3].c_str());
        if (category < 0 || category >= kCategoryCount || player < 0 || player >= kMaxPlayers) return;

        ReportStatus(player, backend->spawn(category, std::atoi(args[2].c_str()), player,
                                            (Team)std::atoi(args[4].c_str()), std::atoi(args[5].c_str())));
    }

    void ReportStatus(int player, const std::string& status) {
        LOG_INFO("Spawn: P{} {}", player + 1, status);
        std::lock_guard<std::mutex> lock(s_catalog.mutex);
        if (player >= 0 && player < kMaxPlayers) s_catalog.status[player] = status;
    }

    static const bool s_registered = [] {
        Command::Register("spawn_list", CmdList);
        Command::Register("spawn", CmdSpawn);
        return true;
    }();

    int Catalog::CurrentGame() {
        auto p_global = GameGlobal();
        if (p_global == nullptr || !MCC::IsInGame()) return -1;
        int game = p_global->current_game;
        return BackendOf(game) ? game : -1;
    }

    // Drop the catalog whenever another game or map loads, then ask for a new one.
    void Catalog::Refresh(int game, bool force) {
        auto generation = CGameManager::load_generation();

        {
            std::lock_guard<std::mutex> lock(s_catalog.mutex);
            bool new_map = generation != s_catalog.generation;
            if (force || new_map || (game >= 0 && game != s_catalog.game && s_catalog.game >= 0)) {
                for (auto& items : s_catalog.items) items.clear();
                s_catalog.game = -1;
                s_catalog.generation = generation;
                s_catalog.requested = false;
            }
            if (new_map)
                for (auto& status : s_catalog.status) status.clear();
            if (game < 0 || s_catalog.requested) return;
            s_catalog.requested = true;
        }

        // outside the lock: some games run posted commands right away
        if (!Command::Post("spawn_list")) {
            std::lock_guard<std::mutex> lock(s_catalog.mutex);
            s_catalog.requested = false;
        }
    }

    void Catalog::Read(int game, Category category, const std::function<void(const std::vector<Item>*)>& f) {
        std::lock_guard<std::mutex> lock(s_catalog.mutex);
        f(game >= 0 && s_catalog.game == game ? &s_catalog.items[category] : nullptr);
    }

    int Catalog::Size(int game, Category category) {
        std::lock_guard<std::mutex> lock(s_catalog.mutex);
        return game >= 0 && s_catalog.game == game ? (int)s_catalog.items[category].size() : 0;
    }

    bool Catalog::Get(int game, Category category, int index, Item& item) {
        std::lock_guard<std::mutex> lock(s_catalog.mutex);
        auto& items = s_catalog.items[category];
        if (game < 0 || s_catalog.game != game || index < 0 || index >= (int)items.size()) return false;
        item = items[index];
        return true;
    }

    std::string Catalog::Status(int player) {
        std::lock_guard<std::mutex> lock(s_catalog.mutex);
        return (player >= 0 && player < kMaxPlayers) ? s_catalog.status[player] : std::string();
    }

    void Catalog::Spawn(Category category, const Item& item, int player, Team team, int weapon_index) {
        int weapon = kUsualWeapon;
        {
            std::lock_guard<std::mutex> lock(s_catalog.mutex);
            auto& weapons = s_catalog.items[Weapons];
            if (category == Characters && weapon_index >= 0 && weapon_index < (int)weapons.size())
                weapon = weapons[weapon_index].id;
            s_catalog.status[player] = "Spawning " + item.name + "...";
        }
        Command::Post("spawn %d %d %d %d %d", category, item.id, player, team, weapon);
    }

    std::string Catalog::WeaponName(int game, int weapon_index) {
        Item weapon;
        return Get(game, Weapons, weapon_index, weapon) ? weapon.name : "Their usual weapon";
    }

    void ImGuiContext() {
        static bool show, was_shown;
        static int player, team;
        static int category;
        static int weapon = -1; // index in the Weapons list; -1: the character's usual weapon
        static char filter[64];

        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Spawn", nullptr, &show);
            ImGui::EndMainMenuBar();
        }

        if (!show) { was_shown = false; return; }

        int game = Catalog::CurrentGame();
        // what can spawn changes as players move through the mission's zone sets
        bool refresh = !was_shown;
        was_shown = true;

        // nothing of it is saved, its place in imgui.ini included
        ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Spawn", &show, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            return;
        }

        if (!MCC::IsInGame()) {
            ImGui::TextWrapped("Start a campaign mission, then open this menu to spawn vehicles, weapons and characters.");
        } else if (game < 0) {
            ImGui::TextWrapped("Spawning isn't available in this game (supported: Halo CE, Halo 2, Halo 3, ODST).");
        } else {
            if (g_menuConfig.spawnMenuMask)
                ImGui::TextDisabled("Tip: in game, each player can press %s for their own spawn menu.",
                                    g_menuConfig.spawnMenuMask == XINPUT_GAMEPAD_DPAD_DOWN ? "D-pad Down" : "the spawn button");

            int player_count = LocalPlayerCount();
            if (player >= player_count) player = 0;

            ImGui::Text("In front of:");
            for (int i = 0; i < player_count; ++i) {
                char label[16];
                snprintf(label, sizeof(label), "Player %d", i + 1);
                ImGui::SameLine();
                ImGui::RadioButton(label, &player, i);
            }

            ImGui::SetNextItemWidth(260);
            ImGui::InputTextWithHint("##filter", "Search", filter, sizeof(filter));
            ImGui::SameLine();
            refresh |= ImGui::Button("Refresh list");

            if (ImGui::BeginTabBar("categories")) {
                for (int c = 0; c < kCategoryCount; ++c) {
                    if (ImGui::BeginTabItem(kCategoryNames[c])) {
                        category = c;
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            if (category == Characters) {
                ImGui::Text("Side:");
                ImGui::SameLine(); ImGui::RadioButton("Their own", &team, TeamDefault);
                ImGui::SameLine(); ImGui::RadioButton("Ally", &team, TeamAlly);
                ImGui::SameLine(); ImGui::RadioButton("Enemy", &team, TeamEnemy);

                if (weapon >= Catalog::Size(game, Weapons)) weapon = -1;
                ImGui::SetNextItemWidth(260);
                if (ImGui::BeginCombo("Weapon", Catalog::WeaponName(game, weapon).c_str())) {
                    if (ImGui::Selectable("Their usual weapon", weapon < 0)) weapon = -1;
                    Catalog::Read(game, Weapons, [&](const std::vector<Item>* items) {
                        for (int i = 0; items && i < (int)items->size(); ++i)
                            if (ImGui::Selectable((*items)[i].name.c_str(), i == weapon)) weapon = i;
                    });
                    ImGui::EndCombo();
                }
            }

            ImGui::BeginChild("items", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
            int picked = -1;
            Catalog::Read(game, (Category)category, [&](const std::vector<Item>* items) {
                if (items == nullptr) { ImGui::TextDisabled("Loading..."); return; }
                if (items->empty()) ImGui::TextDisabled("Nothing of this kind is loaded here.");
                for (int i = 0; i < (int)items->size(); ++i) {
                    auto& name = (*items)[i].name;
                    if (filter[0] && !ContainsNoCase(name, filter)) continue;
                    if (ImGui::Selectable(name.c_str())) picked = i;
                }
            });
            ImGui::EndChild();
            // spawn outside the catalog lock: posting can run the command right away
            Item item;
            if (picked >= 0 && Catalog::Get(game, (Category)category, picked, item))
                Catalog::Spawn((Category)category, item, player, (Team)team, weapon);

            ImGui::TextDisabled("%s", Catalog::Status(player).c_str());
        }
        ImGui::End();

        Catalog::Refresh(game, refresh);
    }
}
