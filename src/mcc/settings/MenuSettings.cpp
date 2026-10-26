#include "MenuSettings.h"

#include "log/Log.h"
#include "log/MatchLog.h"
#include "global/Global.h"
#include "filesystem/Filesystem.h"
#include "mcc/CGameManager.h"
#include "mcc/CGameGlobal.h"
#include "render/imgui/game/xbox/CXboxColorMapping.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace AlphaRing::MenuSettings {
    static const std::string& FilePath() {
        static std::string path = AlphaRing::Filesystem::ModuleFile("alpha_ring_menu.bin");
        return path;
    }

    // "ARMS" - files from before the menus were united start with playerCount
    // (1-4) instead and hold the menu state alone.
    static constexpr uint32_t k_magic = 0x534D5241;
    // 3 added usePlayer0Profile to the menu state block, 4 the per-player armor,
    // 5 the characters' player names, 6 the players' names, 7 the profiles'
    // built-in index, 8 the per-game armor and the characters' game
    static constexpr uint32_t k_version = 8;

    // Caps on counts read from the file, so a corrupt one can't ask for a huge allocation.
    static constexpr uint32_t k_maxEntries = 1024;
    static constexpr uint32_t k_maxString = 1024;

    static const char* const k_defaultProfileNames[MenuConfig::k_profileCount] = {
        "Default", "Southpaw", "Boxer", "Green Thumb", "Bumper Jumper", "Recon",
    };

    // In file order; append only, so older files keep reading.
    static const struct { const char* name; CGamepadMapping::eButton ControllerProfileMapping::* field; } k_profileActions[k_profileActionCount] = {
        {"Jump",                     &ControllerProfileMapping::jump},
        {"Switch Grenades",          &ControllerProfileMapping::switchGrenades},
        {"Use Equipment",            &ControllerProfileMapping::useEquipment},
        {"Action / Interact",        &ControllerProfileMapping::actionInteract},
        {"Reload Right Weapon",      &ControllerProfileMapping::reloadRightWeapon},
        {"Swap/Reload Left Weapon",  &ControllerProfileMapping::swapReloadLeftWeapon},
        {"Change Weapon",            &ControllerProfileMapping::changeWeapon},
        {"Melee",                    &ControllerProfileMapping::melee},
        {"Toggle Flashlight",        &ControllerProfileMapping::toggleFlashlight},
        {"Throw Grenade",            &ControllerProfileMapping::throwGrenade},
        {"Shoot",                    &ControllerProfileMapping::shoot},
        {"Crouch",                   &ControllerProfileMapping::crouch},
        {"Player Zoom",              &ControllerProfileMapping::playerZoom},
        {"Multiplayer Scoreboard",   &ControllerProfileMapping::multiplayerScoreboard},
    };

    // The CGamepadMapping actions a controller profile sets, mirrors included.
    static const struct { int action; CGamepadMapping::eButton ControllerProfileMapping::* field; } k_profileBindings[] = {
        {0,  &ControllerProfileMapping::jump},
        {1,  &ControllerProfileMapping::switchGrenades},
        {2,  &ControllerProfileMapping::actionInteract},
        {3,  &ControllerProfileMapping::reloadRightWeapon},
        {4,  &ControllerProfileMapping::changeWeapon},
        {13, &ControllerProfileMapping::swapReloadLeftWeapon},
        {5,  &ControllerProfileMapping::melee},
        {6,  &ControllerProfileMapping::toggleFlashlight},
        {7,  &ControllerProfileMapping::throwGrenade},
        {49, &ControllerProfileMapping::throwGrenade},  // Use Left Weapon mirrors Throw Grenade
        {28, &ControllerProfileMapping::throwGrenade},  // Thrust mirrors Throw Grenade
        {24, &ControllerProfileMapping::throwGrenade},  // Vehicle Function 1 mirrors Throw Grenade
        {8,  &ControllerProfileMapping::shoot},
        {9,  &ControllerProfileMapping::crouch},
        {21, &ControllerProfileMapping::crouch},        // Vehicle Function 2 mirrors Crouch
        {10, &ControllerProfileMapping::playerZoom},
        {20, &ControllerProfileMapping::multiplayerScoreboard},
        {23, &ControllerProfileMapping::useEquipment},
        {22, &ControllerProfileMapping::jump},          // Vehicle Function 3 mirrors Jump
    };

    // In file order; append only. Colors are left out: the menu state's
    // per-player colors own them and are reapplied at every game start.
    static const struct { const char* name; int CUserProfile::* field; } k_characterFields[] = {
        {"PlayerModelPermutation",  &CUserProfile::PlayerModelPermutation},
        {"HelmetIndex",             &CUserProfile::HelmetIndex},
        {"LeftShoulderIndex",       &CUserProfile::LeftShoulderIndex},
        {"RightShoulderIndex",      &CUserProfile::RightShoulderIndex},
        {"ChestIndex",              &CUserProfile::ChestIndex},
        {"WristIndex",              &CUserProfile::WristIndex},
        {"UtilityIndex",            &CUserProfile::UtilityIndex},
        {"ArmsIndex",               &CUserProfile::ArmsIndex},
        {"LegsIndex",               &CUserProfile::LegsIndex},
        {"BackpackIndex",           &CUserProfile::BackpackIndex},
        {"SpartanBodyIndex",        &CUserProfile::SpartanBodyIndex},
        {"SpartanArmorEffectIndex", &CUserProfile::SpartanArmorEffectIndex},
        {"KneesIndex",              &CUserProfile::KneesIndex},
        {"VisorColorIndex",         &CUserProfile::VisorColorIndex},
        {"EliteHelmetIndex",        &CUserProfile::EliteHelmetIndex},
        {"EliteLeftShoulderIndex",  &CUserProfile::EliteLeftShoulderIndex},
        {"EliteRightShoulderIndex", &CUserProfile::EliteRightShoulderIndex},
        {"EliteChestIndex",         &CUserProfile::EliteChestIndex},
        {"EliteArmsIndex",          &CUserProfile::EliteArmsIndex},
        {"EliteLegsIndex",          &CUserProfile::EliteLegsIndex},
        {"EliteArmorIndex",         &CUserProfile::EliteArmorIndex},
        {"EliteArmorEffectIndex",   &CUserProfile::EliteArmorEffectIndex},
        {"VoiceIndex",              &CUserProfile::VoiceIndex},
        {"SpartanPose",             &CUserProfile::SpartanPose},
        {"ElitePose",               &CUserProfile::ElitePose},
    };
    // Value 0 of a record is UseEliteModel, the fields above follow it.
    static constexpr int k_characterFieldCount = 1 + (int)std::size(k_characterFields);

    // ---------------------------------------------------------------- armor layout

    // Character::values / Armor() layout: [0] UseEliteModel (no longer set),
    // then k_characterFields, then the four ServiceTag characters.
    static constexpr int k_serviceTagValue = k_characterFieldCount;
    static constexpr int k_serviceTagLength = (int)std::size(CUserProfile{}.ServiceTag);
    static constexpr int k_armorValueCount = k_serviceTagValue + k_serviceTagLength;

    static const char* const k_armorGameNames[ArmorGameCount] = {"Halo 2A", "Halo 3", "Halo Reach", "Halo 4"};
    static const int k_armorItemGames[ArmorGameCount] = {
        CUserProfile::ItemHalo2A, CUserProfile::ItemHalo3, CUserProfile::ItemReach, CUserProfile::ItemHalo4,
    };

    // The ArmorGame an item belongs to, or -1.
    static int ArmorGameOfItem(int item) {
        int game = CUserProfile::CustomizationItemGame(item);
        for (int g = 0; g < ArmorGameCount; ++g)
            if (k_armorItemGames[g] == game) return g;
        return -1;
    }

    // An item field holds an item; the others (UseEliteModel,
    // PlayerModelPermutation) are plain numbers.
    static bool IsItemValue(int v) {
        return v > 0 && v < k_characterFieldCount && k_characterFields[v - 1].field != &CUserProfile::PlayerModelPermutation;
    }

    static std::vector<int> EmptyArmor() {
        std::vector<int> values(k_armorValueCount, -1);
        for (int i = 0; i < k_serviceTagLength; ++i) values[k_serviceTagValue + i] = 0;
        return values;
    }

    static bool HasValues(const std::vector<int>& values) {
        for (int v = 0; v < (int)values.size() && v < k_serviceTagValue; ++v)
            if (values[v] >= 0) return true;
        return (int)values.size() > k_serviceTagValue && values[k_serviceTagValue] != 0;
    }

    // A set from before armor was kept per game, split by the game each item
    // is from. The ServiceTag goes to every game's; the plain numbers to none.
    static std::array<std::vector<int>, ArmorGameCount> SplitByGame(const std::vector<int>& values) {
        std::array<std::vector<int>, ArmorGameCount> split;
        for (auto& armor : split) armor = EmptyArmor();
        for (int v = 0; v < (int)values.size() && v < k_serviceTagValue; ++v) {
            if (!IsItemValue(v) || values[v] < 0) continue;
            int game = ArmorGameOfItem(values[v]);
            if (game >= 0) split[game][v] = values[v];
        }
        if ((int)values.size() >= k_armorValueCount && values[k_serviceTagValue] != 0)
            for (auto& armor : split)
                for (int i = 0; i < k_serviceTagLength; ++i)
                    armor[k_serviceTagValue + i] = values[k_serviceTagValue + i];
        return split;
    }

    static std::recursive_mutex s_mutex;
    static bool s_loaded = false;
    // The file is there but couldn't be opened: saving would replace the
    // player's settings with the defaults loaded instead, so saves are refused.
    static bool s_unreadable = false;
    static MenuState s_state;
    static std::vector<ControllerProfile> s_profiles;
    static std::vector<Character> s_characters;
    static std::vector<int> s_armor[4][ArmorGameCount]; // in Character::values layout; empty = none
    static std::string s_playerNames[4]; // UTF-8; empty = the default "Player N"

    static MenuState DefaultState() {
        MenuState ms{};
        ms.playerCount = 1;
        ms.useKM = false;
        ms.usePlayer0Profile = false;
        for (int i = 0; i < 4; ++i) {
            ms.controllerIndex[i] = i;
            ms.controllerProfile[i] = 0;
            ms.sensitivity[i] = 3;
            ms.invert[i] = false;
            ms.teamIndex[i] = i % 2;
        }
        return ms;
    }

    static void SeedProfiles() {
        s_profiles.clear();
        for (int i = 0; i < MenuConfig::k_profileCount; ++i)
            s_profiles.push_back({k_defaultProfileNames[i], g_menuConfig.controllerProfiles[i], i});
    }

    // ---------------------------------------------------------------- file

    template <typename T> static void Write(std::ostream& os, const T& v) {
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    template <typename T> static bool Read(std::istream& is, T& v) {
        return (bool)is.read(reinterpret_cast<char*>(&v), sizeof(T));
    }

    static void WriteString(std::ostream& os, const std::string& s) {
        Write(os, (uint32_t)s.size());
        os.write(s.data(), (std::streamsize)s.size());
    }

    static bool ReadString(std::istream& is, std::string& s) {
        uint32_t len;
        if (!Read(is, len) || len > k_maxString) return false;
        s.resize(len);
        return len == 0 || (bool)is.read(s.data(), len);
    }

    static bool ReadValues(std::istream& is, std::vector<int>& out) {
        uint32_t count;
        if (!Read(is, count) || count > k_maxEntries) return false;
        out.resize(count);
        for (uint32_t v = 0; v < count; ++v) {
            int32_t value;
            if (!Read(is, value)) return false;
            out[v] = value;
        }
        return true;
    }

    // The layout the Xbox menu has always written, kept as the file's first block.
    static void WriteMenuState(std::ostream& os, const MenuState& ms) {
        Write(os, ms.playerCount);
        Write(os, (uint8_t)(ms.useKM ? 1 : 0));
        Write(os, ms.controllerIndex);
        Write(os, ms.controllerProfile);
        Write(os, ms.sensitivity);
        for (bool invert : ms.invert) Write(os, (uint8_t)(invert ? 1 : 0));
        Write(os, ms.teamIndex);
        Write(os, ms.playerColors);
        Write(os, (uint8_t)(ms.usePlayer0Profile ? 1 : 0));
    }

    // `version` 0 is a file from before the menus were united.
    static bool ReadMenuState(std::istream& is, MenuState& ms, uint32_t version) {
        uint8_t useKM = 0;
        uint8_t invert[4] = {};
        bool ok = Read(is, ms.playerCount) && Read(is, useKM) && Read(is, ms.controllerIndex) &&
                  Read(is, ms.controllerProfile) && Read(is, ms.sensitivity) && Read(is, invert) &&
                  Read(is, ms.teamIndex) && Read(is, ms.playerColors);
        uint8_t usePlayer0Profile = 0;
        if (ok && version >= 3)
            ok = Read(is, usePlayer0Profile);
        ms.usePlayer0Profile = usePlayer0Profile != 0;
        // Unchecked file data: the count indexes the four player profiles and
        // per-player settings, so keep it to 1-4 even for a corrupt file.
        ms.playerCount = std::clamp(ms.playerCount, 1, 4);
        ms.useKM = useKM != 0;
        for (int i = 0; i < 4; ++i) {
            ms.invert[i] = invert[i] != 0;
            ms.controllerIndex[i] = std::clamp(ms.controllerIndex[i], 0, 4); // 4 = no controller
            ms.teamIndex[i] = ms.teamIndex[i] ? 1 : 0;
            for (int& color : ms.playerColors[i].colors)
                color = std::clamp(color, 0, CXboxColorMapping::kColorCount - 1);
        }
        return ok;
    }

    static void Load() {
        s_state = DefaultState();
        s_profiles.clear();
        s_characters.clear();
        for (auto& player : s_armor)
            for (auto& armor : player) armor.clear();
        for (auto& name : s_playerNames) name.clear();

        std::ifstream ifs(FilePath(), std::ios::binary);
        if (!ifs && GetFileAttributesA(FilePath().c_str()) != INVALID_FILE_ATTRIBUTES) {
            s_unreadable = true;
            LOG_ERROR("MenuSettings: can't open {}, so menu settings won't be saved this session", FilePath());
        } else if (ifs) {
            LOG_INFO("MenuSettings: loading {}", FilePath());
        }
        uint32_t magic = 0, version = 0;
        if (ifs && Read(ifs, magic)) {
            if (magic != k_magic) {
                ifs.seekg(0);
                ReadMenuState(ifs, s_state, 0);
            } else if (Read(ifs, version) && ReadMenuState(ifs, s_state, version)) {
                uint32_t count, buttons;
                bool profilesRead = false;
                if (Read(ifs, count) && count <= k_maxEntries) {
                    uint32_t i = 0;
                    for (; i < count; ++i) {
                        ControllerProfile profile{{}, g_menuConfig.controllerProfiles[0]};
                        if (!ReadString(ifs, profile.name) || !Read(ifs, buttons) || buttons > k_maxEntries) break;
                        bool ok = true;
                        for (uint32_t b = 0; b < buttons && ok; ++b) {
                            int8_t button;
                            ok = Read(ifs, button);
                            if (ok && b < (uint32_t)k_profileActionCount && button >= CGamepadMapping::None && button <= CGamepadMapping::Y)
                                ProfileButton(profile.buttons, (int)b) = (CGamepadMapping::eButton)button;
                        }
                        if (!ok) break;
                        if (version >= 7) {
                            int8_t builtin;
                            if (!Read(ifs, builtin)) break;
                            if (builtin >= 0 && builtin < MenuConfig::k_profileCount) profile.builtin = builtin;
                        } else {
                            // older files: a profile still under a built-in name is that profile
                            for (int b = 0; b < MenuConfig::k_profileCount; ++b)
                                if (profile.name == k_defaultProfileNames[b]) profile.builtin = b;
                        }
                        s_profiles.push_back(std::move(profile));
                    }
                    profilesRead = i == count;
                }
                bool charactersRead = false;
                if (profilesRead && Read(ifs, count) && count <= k_maxEntries) {
                    uint32_t i = 0;
                    for (; i < count; ++i) {
                        Character character;
                        if (!ReadString(ifs, character.name) || !ReadValues(ifs, character.values) ||
                            (version >= 5 && !ReadString(ifs, character.playerName))) break;
                        if (version >= 8) {
                            int8_t game;
                            if (!Read(ifs, game)) break;
                            character.game = (game >= 0 && game < ArmorGameCount) ? game : ArmorReach;
                        } else {
                            // one armor set for every game: keep the game most of its items are from
                            auto split = SplitByGame(character.values);
                            int best = ArmorReach, best_count = -1;
                            for (int g = 0; g < ArmorGameCount; ++g) {
                                int count = 0;
                                for (int v = 0; v < k_serviceTagValue && v < (int)split[g].size(); ++v)
                                    count += split[g][v] >= 0;
                                if (count > best_count) { best = g; best_count = count; }
                            }
                            character.game = best;
                            character.values = split[best];
                        }
                        s_characters.push_back(std::move(character));
                    }
                    charactersRead = i == count;
                }
                bool armorRead = false;
                if (version >= 8 && charactersRead && Read(ifs, count) && count <= 4) {
                    uint32_t games = 0;
                    bool ok = Read(ifs, games) && games <= 64;
                    for (uint32_t p = 0; p < count && ok; ++p)
                        for (uint32_t g = 0; g < games && ok; ++g) {
                            std::vector<int> armor;
                            ok = ReadValues(ifs, armor);
                            if (ok && g < (uint32_t)ArmorGameCount) s_armor[p][g] = std::move(armor);
                        }
                    armorRead = ok;
                } else if (version >= 4 && charactersRead && Read(ifs, count) && count <= 4) {
                    // one armor set per player for every game: split it by the game each item is from
                    uint32_t p = 0;
                    for (; p < count; ++p) {
                        std::vector<int> armor;
                        if (!ReadValues(ifs, armor)) break;
                        auto split = SplitByGame(armor);
                        for (int g = 0; g < ArmorGameCount; ++g)
                            if (HasValues(split[g])) s_armor[p][g] = std::move(split[g]);
                    }
                    armorRead = p == count;
                }
                if (version >= 6 && armorRead && Read(ifs, count) && count <= 4) {
                    for (uint32_t p = 0; p < count; ++p)
                        if (!ReadString(ifs, s_playerNames[p])) break;
                }
            }
        }

        // First run, or a file from before the menus were united: the six
        // built-in profiles (with any overrides from alpha_ring_menu.cfg)
        // become the starting custom profiles.
        if (s_profiles.empty())
            SeedProfiles();
        for (int& profile : s_state.controllerProfile)
            if (profile < 0 || profile >= (int)s_profiles.size()) profile = 0;
    }

    static std::string ToUtf8(const wchar_t* text) {
        std::string result;
        int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (size > 1) {
            result.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
        }
        return result;
    }

    // Into a buffer of `size` characters; a name too long keeps what fit.
    static void FromUtf8(const std::string& text, wchar_t* out, int size) {
        if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out, size) == 0)
            out[size - 1] = L'\0';
    }

    static void ApplyPlayerNames() {
        for (int i = 0; i < 4; ++i) {
            auto profile = CGameManager::get_profile(i);
            if (!profile || s_playerNames[i].empty()) continue;
            FromUtf8(s_playerNames[i], profile->name, (int)std::size(profile->name));
        }
    }

    static void EnsureLoaded() {
        std::lock_guard lock(s_mutex);
        if (s_loaded) return;
        s_loaded = true;
        Load();
        ApplyPlayerNames();
    }

    void Initialize() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();

        // The saved player count, override and per-player controls have to be in
        // place before the first match is set up: H2A and Halo 4 take their player
        // list then, and a second player added once the match is running never
        // joins it (it ends at once, the player placing "-1th"). Only memory writes
        // here - teams and colors need the game engine, which doesn't exist yet,
        // and wait for game_setup.
        ApplySplitscreen(true);
        for (int i = 0; i < 4; ++i) {
            ApplyPlayerControls(i);
            ApplyPlayerMapping(i);
        }
    }

    std::recursive_mutex& Mutex() { return s_mutex; }

    MenuState& State() {
        EnsureLoaded();
        return s_state;
    }

    std::vector<ControllerProfile>& Profiles() {
        EnsureLoaded();
        return s_profiles;
    }

    std::vector<Character>& Characters() {
        EnsureLoaded();
        return s_characters;
    }

    bool Save() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        if (s_unreadable)
            return false;

        // Written next to the file and moved over it, so a crash mid-write
        // can't leave a truncated file behind.
        const std::string& path = FilePath();
        std::string tmp = path + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                LOG_WARNING("MenuSettings: can't write {}", tmp);
                return false;
            }
            Write(ofs, k_magic);
            Write(ofs, k_version);
            WriteMenuState(ofs, s_state);

            Write(ofs, (uint32_t)s_profiles.size());
            for (auto& profile : s_profiles) {
                WriteString(ofs, profile.name);
                Write(ofs, (uint32_t)k_profileActionCount);
                for (int b = 0; b < k_profileActionCount; ++b)
                    Write(ofs, (int8_t)ProfileButton(profile.buttons, b));
                Write(ofs, (int8_t)profile.builtin);
            }

            Write(ofs, (uint32_t)s_characters.size());
            for (auto& character : s_characters) {
                WriteString(ofs, character.name);
                Write(ofs, (uint32_t)character.values.size());
                for (int value : character.values)
                    Write(ofs, (int32_t)value);
                WriteString(ofs, character.playerName);
                Write(ofs, (int8_t)character.game);
            }

            Write(ofs, (uint32_t)std::size(s_armor));
            Write(ofs, (uint32_t)ArmorGameCount);
            for (auto& player : s_armor)
                for (auto& armor : player) {
                    Write(ofs, (uint32_t)armor.size());
                    for (int value : armor)
                        Write(ofs, (int32_t)value);
                }

            Write(ofs, (uint32_t)std::size(s_playerNames));
            for (auto& name : s_playerNames)
                WriteString(ofs, name);

            if (!ofs.good()) return false;
        }
        if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            LOG_WARNING("MenuSettings: can't replace {} ({})", path, GetLastError());
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- apply

    void ApplyControllerProfile(int profileIndex, CGamepadMapping& mapping) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (profileIndex < 0 || profileIndex >= (int)profiles.size())
            profileIndex = 0;

        // Also leaves Player Move Forward/Backward/Left/Right (H1A, actions 16-19)
        // unbound — movement is handled by the analog stick, not the D-Pad.
        for (auto& action : mapping.actions)
            action = CGamepadMapping::None;

        const ControllerProfileMapping& p = profiles[profileIndex].buttons;
        for (auto& binding : k_profileBindings)
            mapping.actions[binding.action] = p.*binding.field;
    }

    bool IsProfileAction(int action) {
        return std::any_of(std::begin(k_profileBindings), std::end(k_profileBindings),
                           [action](const auto& binding) { return binding.action == action; });
    }

    void ApplyPlayerControls(int player) {
        std::lock_guard lock(s_mutex);
        auto profile = CGameManager::get_profile(player);
        if (!profile) return;
        auto& ms = State();
        profile->controller_index = ms.controllerIndex[player];
        profile->profile.LookControlsInverted = ms.invert[player];
        profile->profile.MouseLookControlsInverted = ms.invert[player];
        profile->profile.VerticalLookSensitivity = static_cast<unsigned char>(ms.sensitivity[player]);
        profile->profile.HorizontalLookSensitivity = static_cast<unsigned char>(ms.sensitivity[player]);
    }

    void ApplyPlayerMapping(int player) {
        std::lock_guard lock(s_mutex);
        if (auto profile = CGameManager::get_profile(player))
            ApplyControllerProfile(State().controllerProfile[player], profile->mapping);
    }

    void ApplyPlayerTeam(int player) {
        std::lock_guard lock(s_mutex);
        if (player >= State().playerCount) {
            MATCH_LOG("change_team player={} skipped: not in the match (player_count={})", player, State().playerCount);
            return;
        }
        auto engine = GameEngine();
        auto xuid = CGameManager::get_xuid(player);
        MATCH_LOG("change_team player={} xuid={:#x} team={}{}", player, (unsigned long long)xuid,
                  State().teamIndex[player], (xuid && engine) ? "" : " (skipped: no xuid/engine)");
        if (xuid && engine)
            engine->change_team(xuid, State().teamIndex[player]);
    }

    void ApplyPlayerColors(int player) {
        std::lock_guard lock(s_mutex);
        auto profile = CGameManager::get_profile(player);
        if (!profile) return;
        auto* gg = GameGlobal();
        int game = gg ? static_cast<int>(gg->current_game) : static_cast<int>(CGameGlobal::Halo3);
        const auto& colors = State().playerColors[player].colors;
        profile->profile.PlayerModelPrimaryColorIndex   = CXboxColorMapping::GetColorIndex(colors[0], game);
        profile->profile.PlayerModelSecondaryColorIndex = CXboxColorMapping::GetColorIndex(colors[1], game);
        profile->profile.PlayerModelTertiaryColorIndex  = CXboxColorMapping::GetColorIndex(colors[2], game);
        profile->profile.PlayerModelPrimaryColor        = CXboxColorMapping::GetColorItemIndex(colors[0], game);
        profile->profile.PlayerModelSecondaryColor      = CXboxColorMapping::GetColorItemIndex(colors[1], game);
        profile->profile.PlayerModelTertiaryColor       = CXboxColorMapping::GetColorItemIndex(colors[2], game);
    }

    void ApplySplitscreen(bool atGameStart) {
        std::lock_guard lock(s_mutex);
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        if (!p_setting) return;
        auto& ms = State();
        if (atGameStart)
            p_setting->b_override = ms.playerCount > 1;
        else if (ms.playerCount > 1)
            p_setting->b_override = true;
        p_setting->player_count = ms.playerCount;
        p_setting->b_player0_use_km = ms.useKM;
        p_setting->b_use_player0_profile = ms.usePlayer0Profile;
        MATCH_LOG("ApplySplitscreen(atGameStart={}): override={} player_count={} p1_km={} use_p1_profile={}",
                  atGameStart, p_setting->b_override, p_setting->player_count, p_setting->b_player0_use_km,
                  p_setting->b_use_player0_profile);
    }

    static bool TeamChangeLeavesMatch() {
        auto* gg = GameGlobal();
        return gg && (gg->current_game == CGameGlobal::Halo4 || gg->current_game == CGameGlobal::GroundHog);
    }

    void ApplyAll(bool atGameStart, const int* previousTeams) {
        std::lock_guard lock(s_mutex);
        for (int i = 0; i < 4; ++i) {
            ApplyPlayerControls(i);
            ApplyPlayerMapping(i);
        }
        ApplySplitscreen(atGameStart);
        bool picked_only = TeamChangeLeavesMatch();
        for (int i = 0; i < 4; ++i) {
            if (!picked_only || (previousTeams && previousTeams[i] != State().teamIndex[i]))
                ApplyPlayerTeam(i);
            else
                MATCH_LOG("change_team player={} held back: Halo 4/H2A take it once the match is running", i);
            ApplyPlayerColors(i);
            if (auto profile = CGameManager::get_profile(i))
                ApplyArmor(i, profile->profile);
        }
    }

    // ---------------------------------------------------------------- profiles

    const char* ProfileActionName(int action) {
        return (action >= 0 && action < k_profileActionCount) ? k_profileActions[action].name : "";
    }

    CGamepadMapping::eButton& ProfileButton(ControllerProfileMapping& buttons, int action) {
        return buttons.*(k_profileActions[std::clamp(action, 0, k_profileActionCount - 1)].field);
    }

    int AddProfile(const std::string& name, const ControllerProfileMapping& buttons) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        profiles.push_back({name, buttons});
        return (int)profiles.size() - 1;
    }

    void DeleteProfile(int index) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (index < 0 || index >= (int)profiles.size() || profiles.size() <= 1)
            return;
        profiles.erase(profiles.begin() + index);
        for (int i = 0; i < 4; ++i) {
            int& selected = State().controllerProfile[i];
            if (selected == index) {
                selected = 0;
                ApplyPlayerMapping(i);
            } else if (selected > index) {
                --selected;
            }
        }
    }

    void ResetProfile(int index) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (index < 0 || index >= (int)profiles.size()) return;
        int builtin = profiles[index].builtin;
        profiles[index].buttons = g_menuConfig.controllerProfiles[builtin >= 0 ? builtin : 0];
        ReapplyProfile(index);
    }

    void ReapplyProfile(int index) {
        std::lock_guard lock(s_mutex);
        for (int i = 0; i < 4; ++i)
            if (State().controllerProfile[i] == index)
                ApplyPlayerMapping(i);
    }

    // ---------------------------------------------------------------- characters

    void SaveAll() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        for (int i = 0; i < 4; ++i) {
            auto profile = CGameManager::get_profile(i);
            if (!profile) continue;
            std::string name = ToUtf8(profile->name);
            if (name.size() > k_maxString) name.resize(k_maxString);
            s_playerNames[i] = std::move(name);
        }
        Save();
    }

    std::string CharacterName(const wchar_t* playerName) {
        std::string name = ToUtf8(playerName);
        if (name.size() > k_maxString - 16) name.resize(k_maxString - 16);
        return name + "'s Character";
    }

    const char* ArmorGameName(int game) {
        return (game >= 0 && game < ArmorGameCount) ? k_armorGameNames[game] : "";
    }

    int ArmorItemGame(int game) {
        return (game >= 0 && game < ArmorGameCount) ? k_armorItemGames[game] : CUserProfile::ItemOther;
    }

    int CurrentArmorGame() {
        auto* gg = GameGlobal();
        if (!gg) return -1;
        switch (gg->current_game) {
            case CGameGlobal::GroundHog: return ArmorHalo2A;
            case CGameGlobal::Halo3:     return ArmorHalo3;
            case CGameGlobal::HaloReach: return ArmorReach;
            case CGameGlobal::Halo4:     return ArmorHalo4;
            default:                     return -1;
        }
    }

    std::vector<int>& Armor(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        auto& armor = s_armor[std::clamp(player, 0, 3)][std::clamp(game, 0, ArmorGameCount - 1)];
        if ((int)armor.size() < k_armorValueCount) {
            auto full = EmptyArmor();
            std::copy(armor.begin(), armor.end(), full.begin());
            armor = std::move(full);
        }
        return armor;
    }

    int ArmorValueIndex(int CUserProfile::* field) {
        for (int i = 0; i < (int)std::size(k_characterFields); ++i)
            if (k_characterFields[i].field == field) return i + 1;
        return -1;
    }

    int ArmorServiceTagIndex() { return k_serviceTagValue; }

    bool HasArmor(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        return player >= 0 && player < 4 && game >= 0 && game < ArmorGameCount && HasValues(s_armor[player][game]);
    }

    bool HasArmor(int player) {
        return HasArmor(player, CurrentArmorGame());
    }

    void ClearArmor(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        if (player >= 0 && player < 4 && game >= 0 && game < ArmorGameCount)
            s_armor[player][game].clear();
    }

    void ApplyArmor(int player, CUserProfile& profile) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        int game = CurrentArmorGame();
        if (player < 0 || player >= 4 || game < 0)
            return;
        const auto& values = s_armor[player][game];
        for (int v = 1; v < (int)values.size() && v < k_serviceTagValue; ++v) {
            if (values[v] < 0) continue;
            // never an item from another game, whatever the file holds
            if (IsItemValue(v) && ArmorGameOfItem(values[v]) != game) continue;
            profile.*(k_characterFields[v - 1].field) = values[v];
        }
        if ((int)values.size() >= k_armorValueCount && values[k_serviceTagValue] != 0)
            for (int i = 0; i < k_serviceTagLength; ++i)
                profile.ServiceTag[i] = (wchar_t)values[k_serviceTagValue + i];
    }

    int SaveCharacter(const std::string& name, int game, const std::vector<int>& armor, const wchar_t* playerName) {
        std::lock_guard lock(s_mutex);
        Character character{name, armor, ToUtf8(playerName), game};

        auto& characters = Characters();
        for (int i = 0; i < (int)characters.size(); ++i) {
            if (characters[i].name == name && characters[i].game == game) {
                characters[i] = std::move(character);
                return i;
            }
        }
        characters.push_back(std::move(character));
        return (int)characters.size() - 1;
    }

    void LoadCharacter(int index, int player, wchar_t* playerName, int playerNameSize) {
        std::lock_guard lock(s_mutex);
        auto& characters = Characters();
        if (index < 0 || index >= (int)characters.size() || player < 0 || player >= 4)
            return;
        const auto& character = characters[index];
        auto& armor = Armor(player, character.game);
        armor = EmptyArmor();
        std::copy_n(character.values.begin(), (std::min)(character.values.size(), armor.size()), armor.begin());
        if (!character.playerName.empty() && playerNameSize > 0)
            FromUtf8(character.playerName, playerName, playerNameSize);
    }

    void DeleteCharacter(int index) {
        std::lock_guard lock(s_mutex);
        auto& characters = Characters();
        if (index >= 0 && index < (int)characters.size())
            characters.erase(characters.begin() + index);
    }
}
