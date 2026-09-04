#include "MenuConfig.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <string>

MenuConfig g_menuConfig;

static const std::map<std::string, WORD> k_buttonMap = {
    {"DPAD_UP",        XINPUT_GAMEPAD_DPAD_UP},
    {"DPAD_DOWN",      XINPUT_GAMEPAD_DPAD_DOWN},
    {"DPAD_LEFT",      XINPUT_GAMEPAD_DPAD_LEFT},
    {"DPAD_RIGHT",     XINPUT_GAMEPAD_DPAD_RIGHT},
    {"START",          XINPUT_GAMEPAD_START},
    {"BACK",           XINPUT_GAMEPAD_BACK},
    {"LEFT_THUMB",     XINPUT_GAMEPAD_LEFT_THUMB},
    {"RIGHT_THUMB",    XINPUT_GAMEPAD_RIGHT_THUMB},
    {"LEFT_SHOULDER",  XINPUT_GAMEPAD_LEFT_SHOULDER},
    {"RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {"A",              XINPUT_GAMEPAD_A},
    {"B",              XINPUT_GAMEPAD_B},
    {"X",              XINPUT_GAMEPAD_X},
    {"Y",              XINPUT_GAMEPAD_Y},
};

static const std::map<std::string, CGamepadMapping::eButton> k_gamepadButtonMap = {
    {"DPAD_UP",        CGamepadMapping::DpadUp},
    {"DPAD_DOWN",      CGamepadMapping::DpadDown},
    {"DPAD_LEFT",      CGamepadMapping::DpadLeft},
    {"DPAD_RIGHT",     CGamepadMapping::DpadRight},
    {"START",          CGamepadMapping::Start},
    {"BACK",           CGamepadMapping::Back},
    {"LEFT_THUMB",     CGamepadMapping::LeftThumb},
    {"RIGHT_THUMB",    CGamepadMapping::RightThumb},
    {"LEFT_SHOULDER",  CGamepadMapping::LeftShoulder},
    {"RIGHT_SHOULDER", CGamepadMapping::RightShoulder},
    {"LEFT_TRIGGER",   CGamepadMapping::LeftTrigger},
    {"RIGHT_TRIGGER",  CGamepadMapping::RightTrigger},
    {"A",              CGamepadMapping::A},
    {"B",              CGamepadMapping::B},
    {"X",              CGamepadMapping::X},
    {"Y",              CGamepadMapping::Y},
};

// Maps a controller-profile cfg key prefix ("d_", "s_", ...) to the profile
// index used by the "Controller Profile" subpage in CXboxMenu.hpp.
static const std::map<std::string, int> k_profilePrefixMap = {
    {"d_", 0}, // default
    {"s_", 1}, // southpaw
    {"b_", 2}, // boxer
    {"g_", 3}, // green thumb
    {"j_", 4}, // bumper jumper
    {"r_", 5}, // recon
};

// Maps a controller-profile cfg key suffix to the field it binds.
static const std::map<std::string, CGamepadMapping::eButton ControllerProfileMapping::*> k_profileActionMap = {
    {"jump",                     &ControllerProfileMapping::jump},
    {"switch_grenades",          &ControllerProfileMapping::switchGrenades},
    {"use_equipment",            &ControllerProfileMapping::useEquipment},
    {"action_interact",          &ControllerProfileMapping::actionInteract},
    {"reload_right_weapon",      &ControllerProfileMapping::reloadRightWeapon},
    {"swap_reload_left_weapon",  &ControllerProfileMapping::swapReloadLeftWeapon},
    {"change_weapon",            &ControllerProfileMapping::changeWeapon},
    {"melee",                    &ControllerProfileMapping::melee},
    {"toggle_flashlight",        &ControllerProfileMapping::toggleFlashlight},
    {"throw_grenade",            &ControllerProfileMapping::throwGrenade},
    {"shoot",                    &ControllerProfileMapping::shoot},
    {"crouch",                   &ControllerProfileMapping::crouch},
    {"player_zoom",              &ControllerProfileMapping::playerZoom},
    {"multiplayer_scoreboard",   &ControllerProfileMapping::multiplayerScoreboard},
};

static const std::map<std::string, int> k_keyMap = {
    {"F1",     VK_F1},  {"F2",  VK_F2},  {"F3",  VK_F3},  {"F4",  VK_F4},
    {"F5",     VK_F5},  {"F6",  VK_F6},  {"F7",  VK_F7},  {"F8",  VK_F8},
    {"F9",     VK_F9},  {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
    {"ENTER",  VK_RETURN},
    {"ESCAPE", VK_ESCAPE},
    {"SPACE",  VK_SPACE},
    {"TAB",    VK_TAB},
};

std::string MenuConfig::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

WORD MenuConfig::parseButton(const std::string& raw) {
    std::string name = trim(raw);
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    auto it = k_buttonMap.find(name);
    return (it != k_buttonMap.end()) ? it->second : 0;
}

CGamepadMapping::eButton MenuConfig::parseGamepadButton(const std::string& raw) {
    std::string name = trim(raw);
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    auto it = k_gamepadButtonMap.find(name);
    return (it != k_gamepadButtonMap.end()) ? it->second : CGamepadMapping::None;
}

int MenuConfig::parseKey(const std::string& raw) {
    std::string name = trim(raw);
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);

    auto it = k_keyMap.find(name);
    if (it != k_keyMap.end()) return it->second;

    // Single letter A-Z
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
        return static_cast<int>(name[0]);

    return VK_F4;
}

void MenuConfig::writeDefault(const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# AlphaRing Xbox Menu Configuration\n"
           "#\n"
           "# Available Xbox controller button IDs:\n"
           "#   DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT\n"
           "#   START, BACK\n"
           "#   LEFT_THUMB, RIGHT_THUMB\n"
           "#   LEFT_SHOULDER, RIGHT_SHOULDER\n"
           "#   A, B, X, Y\n"
           "#\n"
           "# Use + to combine buttons for a combo (e.g., START+BACK)\n"
           "# Keyboard key IDs: F1-F12, ENTER, ESCAPE, SPACE, TAB, A-Z\n"
           "# Note: open_menu_controller and open_debug_controller must not use the same combo.\n"
           "\n"
           "open_menu_controller=START+LEFT_THUMB\n"
           "open_debug_controller=START+RIGHT_THUMB\n"
           "open_menu_keyboard=F4\n"
           "open_debug_keyboard=F1\n"
           "\n"
           "# Controller Profile Mappings\n"
           "#\n"
           "# Available Xbox controller button IDs:\n"
           "#   DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT\n"
           "#   START, BACK\n"
           "#   LEFT_THUMB, RIGHT_THUMB\n"
           "#   LEFT_SHOULDER, RIGHT_SHOULDER\n"
           "#   LEFT_TRIGGER, RIGHT_TRIGGER\n"
           "#   A, B, X, Y\n"
           "#\n"
           "# Select a profile for a player from the Controller Profile menu\n"
           "# (Default, Southpaw, Boxer, Green Thumb, Bumper Jumper, Recon).\n"
           "\n"
           "# default\n"
           "d_jump = A\n"
           "d_switch_grenades = B\n"
           "d_use_equipment = LEFT_SHOULDER\n"
           "d_action_interact = X\n"
           "d_reload_right_weapon = X\n"
           "d_swap_reload_left_weapon = X\n"
           "d_change_weapon = Y\n"
           "d_melee = RIGHT_SHOULDER\n"
           "d_toggle_flashlight = DPAD_LEFT\n"
           "d_throw_grenade = LEFT_TRIGGER\n"
           "d_shoot = RIGHT_TRIGGER\n"
           "d_crouch = LEFT_THUMB\n"
           "d_player_zoom = RIGHT_THUMB\n"
           "d_multiplayer_scoreboard = BACK\n"
           "\n"
           "# southpaw\n"
           "s_jump = A\n"
           "s_switch_grenades = B\n"
           "s_use_equipment = RIGHT_SHOULDER\n"
           "s_action_interact = X\n"
           "s_reload_right_weapon = X\n"
           "s_swap_reload_left_weapon = X\n"
           "s_change_weapon = Y\n"
           "s_melee = LEFT_SHOULDER\n"
           "s_toggle_flashlight = DPAD_LEFT\n"
           "s_throw_grenade = RIGHT_TRIGGER\n"
           "s_shoot = LEFT_TRIGGER\n"
           "s_crouch = RIGHT_THUMB\n"
           "s_player_zoom = LEFT_THUMB\n"
           "s_multiplayer_scoreboard = BACK\n"
           "\n"
           "# boxer\n"
           "b_jump = A\n"
           "b_switch_grenades = B\n"
           "b_use_equipment = LEFT_SHOULDER\n"
           "b_action_interact = X\n"
           "b_reload_right_weapon = X\n"
           "b_swap_reload_left_weapon = X\n"
           "b_change_weapon = Y\n"
           "b_melee = LEFT_TRIGGER\n"
           "b_toggle_flashlight = DPAD_LEFT\n"
           "b_throw_grenade = LEFT_SHOULDER\n"
           "b_shoot = RIGHT_TRIGGER\n"
           "b_crouch = LEFT_THUMB\n"
           "b_player_zoom = RIGHT_THUMB\n"
           "b_multiplayer_scoreboard = BACK\n"
           "\n"
           "# green thumb\n"
           "g_jump = A\n"
           "g_switch_grenades = B\n"
           "g_use_equipment = LEFT_SHOULDER\n"
           "g_action_interact = X\n"
           "g_reload_right_weapon = X\n"
           "g_swap_reload_left_weapon = X\n"
           "g_change_weapon = Y\n"
           "g_melee = RIGHT_THUMB\n"
           "g_toggle_flashlight = DPAD_LEFT\n"
           "g_throw_grenade = LEFT_TRIGGER\n"
           "g_shoot = RIGHT_TRIGGER\n"
           "g_crouch = LEFT_THUMB\n"
           "g_player_zoom = RIGHT_SHOULDER\n"
           "g_multiplayer_scoreboard = BACK\n"
           "\n"
           "# bumper jumper\n"
           "j_jump = LEFT_SHOULDER\n"
           "j_switch_grenades = A\n"
           "j_use_equipment = X\n"
           "j_action_interact = B\n"
           "j_reload_right_weapon = B\n"
           "j_swap_reload_left_weapon = B\n"
           "j_change_weapon = Y\n"
           "j_melee = RIGHT_SHOULDER\n"
           "j_toggle_flashlight = DPAD_LEFT\n"
           "j_throw_grenade = LEFT_TRIGGER\n"
           "j_shoot = RIGHT_TRIGGER\n"
           "j_crouch = LEFT_THUMB\n"
           "j_player_zoom = RIGHT_THUMB\n"
           "j_multiplayer_scoreboard = BACK\n"
           "\n"
           "# recon\n"
           "r_jump = A\n"
           "r_switch_grenades = X\n"
           "r_use_equipment = LEFT_SHOULDER\n"
           "r_action_interact = RIGHT_SHOULDER\n"
           "r_reload_right_weapon = RIGHT_SHOULDER\n"
           "r_swap_reload_left_weapon = RIGHT_SHOULDER\n"
           "r_change_weapon = Y\n"
           "r_melee = B\n"
           "r_toggle_flashlight = DPAD_LEFT\n"
           "r_throw_grenade = LEFT_TRIGGER\n"
           "r_shoot = RIGHT_TRIGGER\n"
           "r_crouch = LEFT_THUMB\n"
           "r_player_zoom = RIGHT_THUMB\n"
           "r_multiplayer_scoreboard = BACK\n";
}

MenuConfig MenuConfig::load() {
    MenuConfig cfg;
    std::ifstream ifs(k_configPath);

    if (!ifs.is_open()) {
        writeDefault(k_configPath);
        return cfg;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        auto parseCombo = [&](const std::string& v) -> WORD {
            WORD mask = 0;
            std::istringstream ss(v);
            std::string token;
            while (std::getline(ss, token, '+'))
                mask |= parseButton(token);
            return mask;
        };

        if (key == "open_menu_controller") {
            WORD mask = parseCombo(val);
            if (mask != 0) cfg.controllerComboMask = mask;
        }
        else if (key == "open_debug_controller") {
            WORD mask = parseCombo(val);
            if (mask != 0) cfg.debugComboMask = mask;
        }
        else if (key == "open_menu_keyboard") {
            int vk = parseKey(val);
            if (vk != 0) cfg.keyboardVKey = vk;
        }
        else if (key == "open_debug_keyboard") {
            int vk = parseKey(val);
            if (vk != 0) cfg.debugKeyboardVKey = vk;
        }
        else if (key.size() > 2 && key[1] == '_') {
            // Controller profile binding: "<prefix>_<action> = BUTTON" (e.g. d_jump = A)
            auto profileIt = k_profilePrefixMap.find(key.substr(0, 2));
            auto actionIt  = k_profileActionMap.find(key.substr(2));
            if (profileIt != k_profilePrefixMap.end() && actionIt != k_profileActionMap.end()) {
                CGamepadMapping::eButton btn = parseGamepadButton(val);
                if (btn != CGamepadMapping::None)
                    cfg.controllerProfiles[profileIt->second].*(actionIt->second) = btn;
            }
        }
    }

    return cfg;
}

void MenuConfig::ApplyControllerProfile(int profileIndex, CGamepadMapping& mapping) const {
    if (profileIndex < 0 || profileIndex >= k_profileCount)
        profileIndex = 0;

    // Also leaves Player Move Forward/Backward/Left/Right (H1A, actions 16-19)
    // unbound — movement is handled by the analog stick, not the D-Pad.
    for (auto& action : mapping.actions)
        action = CGamepadMapping::None;

    const ControllerProfileMapping& p = controllerProfiles[profileIndex];
    mapping.actions[0]  = p.jump;
    mapping.actions[1]  = p.switchGrenades;
    mapping.actions[2]  = p.actionInteract;
    mapping.actions[3]  = p.reloadRightWeapon;
    mapping.actions[4]  = p.changeWeapon;
    mapping.actions[13] = p.swapReloadLeftWeapon;
    mapping.actions[5]  = p.melee;
    mapping.actions[6]  = p.toggleFlashlight;
    mapping.actions[7]  = p.throwGrenade;
    mapping.actions[49] = p.throwGrenade;  // Use Left Weapon mirrors Throw Grenade
    mapping.actions[28] = p.throwGrenade;  // Thrust mirrors Throw Grenade
    mapping.actions[24] = p.throwGrenade;  // Vehicle Function 1 mirrors Throw Grenade
    mapping.actions[8]  = p.shoot;
    mapping.actions[9]  = p.crouch;
    mapping.actions[21] = p.crouch;        // Vehicle Function 2 mirrors Crouch
    mapping.actions[10] = p.playerZoom;
    mapping.actions[20] = p.multiplayerScoreboard;
    mapping.actions[23] = p.useEquipment;
    mapping.actions[22] = p.jump;          // Vehicle Function 3 mirrors Jump
}
