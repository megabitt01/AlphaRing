#pragma once

#include <Windows.h>
#include <Xinput.h>
#include <string>

#include "mcc/CGamepadMapping.h"

// One button binding per Halo action, used to build a CGamepadMapping for a
// controller profile (default/southpaw/boxer/green thumb/bumper jumper/recon).
struct ControllerProfileMapping {
    CGamepadMapping::eButton jump;
    CGamepadMapping::eButton switchGrenades;
    CGamepadMapping::eButton useEquipment;
    CGamepadMapping::eButton actionInteract;
    CGamepadMapping::eButton reloadRightWeapon;
    CGamepadMapping::eButton swapReloadLeftWeapon;
    CGamepadMapping::eButton changeWeapon;
    CGamepadMapping::eButton melee;
    CGamepadMapping::eButton toggleFlashlight;
    CGamepadMapping::eButton throwGrenade;
    CGamepadMapping::eButton shoot;
    CGamepadMapping::eButton crouch;
    CGamepadMapping::eButton playerZoom;
    CGamepadMapping::eButton multiplayerScoreboard;
};

struct MenuConfig {
    WORD controllerComboMask = XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_LEFT_THUMB;
    WORD debugComboMask      = XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_RIGHT_THUMB;
    int  keyboardVKey        = VK_F4;
    int  debugKeyboardVKey   = VK_F1;

    // Indexed to match the "Controller Profile" subpage order in CXboxMenu.hpp:
    // 0 = Default, 1 = Southpaw, 2 = Boxer, 3 = Green Thumb, 4 = Bumper Jumper, 5 = Recon
    static constexpr int k_profileCount = 6;
    ControllerProfileMapping controllerProfiles[k_profileCount] = {
        // Default
        {CGamepadMapping::A, CGamepadMapping::B, CGamepadMapping::LeftShoulder, CGamepadMapping::X,
         CGamepadMapping::X, CGamepadMapping::X, CGamepadMapping::Y, CGamepadMapping::RightShoulder, CGamepadMapping::DpadLeft,
         CGamepadMapping::LeftTrigger, CGamepadMapping::RightTrigger, CGamepadMapping::LeftThumb,
         CGamepadMapping::RightThumb, CGamepadMapping::Back},
        // Southpaw
        {CGamepadMapping::A, CGamepadMapping::B, CGamepadMapping::RightShoulder, CGamepadMapping::X,
         CGamepadMapping::X, CGamepadMapping::X, CGamepadMapping::Y, CGamepadMapping::LeftShoulder, CGamepadMapping::DpadLeft,
         CGamepadMapping::RightTrigger, CGamepadMapping::LeftTrigger, CGamepadMapping::RightThumb,
         CGamepadMapping::LeftThumb, CGamepadMapping::Back},
        // Boxer
        {CGamepadMapping::A, CGamepadMapping::B, CGamepadMapping::LeftShoulder, CGamepadMapping::X,
         CGamepadMapping::X, CGamepadMapping::X, CGamepadMapping::Y, CGamepadMapping::LeftTrigger, CGamepadMapping::DpadLeft,
         CGamepadMapping::LeftShoulder, CGamepadMapping::RightTrigger, CGamepadMapping::LeftThumb,
         CGamepadMapping::RightThumb, CGamepadMapping::Back},
        // Green Thumb
        {CGamepadMapping::A, CGamepadMapping::B, CGamepadMapping::LeftShoulder, CGamepadMapping::X,
         CGamepadMapping::X, CGamepadMapping::X, CGamepadMapping::Y, CGamepadMapping::RightThumb, CGamepadMapping::DpadLeft,
         CGamepadMapping::LeftTrigger, CGamepadMapping::RightTrigger, CGamepadMapping::LeftThumb,
         CGamepadMapping::RightShoulder, CGamepadMapping::Back},
        // Bumper Jumper
        {CGamepadMapping::LeftShoulder, CGamepadMapping::A, CGamepadMapping::X, CGamepadMapping::B,
         CGamepadMapping::B, CGamepadMapping::B, CGamepadMapping::Y, CGamepadMapping::RightShoulder, CGamepadMapping::DpadLeft,
         CGamepadMapping::LeftTrigger, CGamepadMapping::RightTrigger, CGamepadMapping::LeftThumb,
         CGamepadMapping::RightThumb, CGamepadMapping::Back},
        // Recon
        {CGamepadMapping::A, CGamepadMapping::X, CGamepadMapping::LeftShoulder, CGamepadMapping::RightShoulder,
         CGamepadMapping::RightShoulder, CGamepadMapping::RightShoulder, CGamepadMapping::Y, CGamepadMapping::B, CGamepadMapping::DpadLeft,
         CGamepadMapping::LeftTrigger, CGamepadMapping::RightTrigger, CGamepadMapping::LeftThumb,
         CGamepadMapping::RightThumb, CGamepadMapping::Back},
    };

    static constexpr const char* k_configPath = "./alpha_ring_menu.cfg";

    static MenuConfig load();

    // Binds the given profile's buttons onto `mapping`'s Halo actions, clearing
    // everything else. `profileIndex` is clamped to [0, k_profileCount).
    void ApplyControllerProfile(int profileIndex, CGamepadMapping& mapping) const;

private:
    static void        writeDefault(const std::string& path);
    static WORD        parseButton(const std::string& name);
    static int         parseKey(const std::string& name);
    static CGamepadMapping::eButton parseGamepadButton(const std::string& name);
    static std::string trim(const std::string& s);
};

extern MenuConfig g_menuConfig;
