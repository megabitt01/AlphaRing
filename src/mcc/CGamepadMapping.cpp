#include "CGamepadMapping.h"
#include "CGameEngine.h"

static std::array<const char*, 17> button_names {
        "Left Trigger","Right Trigger",
        "Dpad Up","Dpad Down","Dpad Left","Dpad Right",
        "Start","Back",
        "Left Thumb","Right Thumb",
        "Left Shoulder","Right Shoulder",
        "A","B","X","Y",
        "None"  // Unbound
};

static std::array<const char*, 66> action_names {
        "Jump",
        "Switch Grenades 1",
        "Action",
        "Reload Right Weapon",
        "Change Weapon",
        "Melee",
        "Toggle Flashlight",
        "Throw Grenade",
        "Use Right Weapon",
        "Crouch",
        "Player Zoom",
        nullptr,
        nullptr,
        "Swap/Reload Left Weapon",
        "Sprint",
        "Banshee Bomb",
        "Player Move Forward", // H1A
        "Player Move Backward", // H1A
        "Player Move Left", // H1A
        "Player Move Right", // H1A
        "Multiplayer Scoreboard",
        "Vehicle Function 2",
        "Vehicle Function 3",
        "Use Equipment",
        "Vehicle Function 1",
        "Editor Ascend",
        "Editor Descend",
        "Drop/Pickup",
        "Thrust",
        "Precision Mode",
        "Delete",
        "Object Options",
        "Tools",
        "Play/Edit",
        "Editor Zoom",
        "Rotate",
        "TOGGLE PANEL",
        "TOGGLE INTERFACE",
        "TOGGLE FIRST/THIRD PERSON",
        "CAMERA RESET",
        "JUMP FORWARD",
        "JUMP BACK",
        "PAUSE/RESUME PLAYBACK",
        "FAST FORWARD",
        "TOGGLE FREECAM",
        "BOOST",
        "THEATER PANNING",
        "THEATER ASCEND",
        "THEATER DESCEND",
        "Use Left Weapon",
        "Theater Zoom",
        "Toggle Rotation Axes",
        "Duplicate",
        "Lock",
        "Reset",
        "Select Next Grenades 2",
        "Select Previous Grenades 2",
        "Special Action",
        "Open Loadouts Menu",
        "Toggle Display Waypoint Markers",
        "Toggle Display Waypoint Markers Alternative",
        "Push to Talk", // not sure
        "Vehicle Ascend",
        "Vehicle Descend",
        "Select Previous Grenades",
        "Select Next Grenades",
};

const std::array<const char *, 17>* CGamepadMapping::ButtonNames() {return &button_names;}

const std::array<const char *, 66>* CGamepadMapping::ActionNames() {return &action_names;}

void CGamepadMapping::ResetToDefaults() {
    // Set all to None (unbound) first
    for (int i = 0; i < 66; i++) {
        actions[i] = None;
    }

    // Standard Xbox Halo controls
    actions[0]  = A;             // Jump
    actions[1]  = LeftShoulder;  // Switch Grenades
    actions[2]  = X;             // Action/Interact
    actions[3]  = RightShoulder; // Reload Right Weapon
    actions[4]  = Y;             // Change Weapon
    actions[13] = actions[3];    // Swap/Reload Left Weapon (defaults to Reload Right Weapon)
    actions[5]  = B;             // Melee
    actions[6]  = DpadUp;        // Toggle Flashlight
    actions[7]  = LeftTrigger;   // Throw Grenade
    actions[49] = actions[7];    // Use Left Weapon
    actions[28] = actions[7];    // Thrust
    actions[24] = actions[7];    // Vehicle Function 1
    actions[8]  = RightTrigger;  // Use Right Weapon (Shoot)
    actions[9]  = LeftThumb;     // Crouch
    actions[21] = actions[9];    // Vehicle Function 2
    actions[10] = RightThumb;    // Player Zoom
    actions[20] = Back;          // Multiplayer Scoreboard
    actions[22] = actions[0];    // Vehicle Function 3 (mirrors Jump)

    // Player Move Forward/Backward/Left/Right (H1A) stay unbound — movement is
    // handled by the analog stick, not the D-Pad.
    actions[16] = None;
    actions[17] = None;
    actions[18] = None;
    actions[19] = None;
}

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>
#include "input/Input.h"

// Detect which button is currently pressed on a controller
static int DetectPressedButton(int controllerIndex) {
    XINPUT_STATE state;
    if (!AlphaRing::Input::GetXInputGetState(controllerIndex, &state))
        return -1;

    // Check triggers first
    if (state.Gamepad.bLeftTrigger > 30) return CGamepadMapping::LeftTrigger;
    if (state.Gamepad.bRightTrigger > 30) return CGamepadMapping::RightTrigger;

    // Check buttons
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) return CGamepadMapping::DpadUp;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) return CGamepadMapping::DpadDown;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) return CGamepadMapping::DpadLeft;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) return CGamepadMapping::DpadRight;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) return CGamepadMapping::Start;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) return CGamepadMapping::Back;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) return CGamepadMapping::LeftThumb;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) return CGamepadMapping::RightThumb;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) return CGamepadMapping::LeftShoulder;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) return CGamepadMapping::RightShoulder;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) return CGamepadMapping::A;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) return CGamepadMapping::B;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) return CGamepadMapping::X;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) return CGamepadMapping::Y;

    return -1;
}

void CGamepadMapping::ImGuiContext() {
    char buffer[64];
    bool result = false;
    static int binding_action = -1;  // Which action is being bound (-1 = none)
    static int binding_controller = 0;  // Which controller to listen to

    // Controller selector for binding
    ImGui::PushItemWidth(150);
    ImGui::Combo("Bind Controller", &binding_controller, "Controller 1\0Controller 2\0Controller 3\0Controller 4\0");
    ImGui::PopItemWidth();
    ImGui::Separator();

    // Custom Profile Management
    static std::vector<std::string> profile_names;
    static int selected_profile = -1;
    static char new_profile_name[64] = "";
    static bool show_save_input = false;
    static bool needs_refresh = true;

    // Refresh profile list when needed
    if (needs_refresh) {
        needs_refresh = false;
        // Reset selection if out of bounds
        if (selected_profile >= (int)profile_names.size()) {
            selected_profile = profile_names.empty() ? -1 : 0;
        }
    }

    ImGui::Text("Custom Profiles:");

    // Profile dropdown
    ImGui::PushItemWidth(200);
    const char* preview = (selected_profile >= 0 && selected_profile < (int)profile_names.size())
        ? profile_names[selected_profile].c_str()
        : "-- Select Profile --";

    if (ImGui::BeginCombo("##CustomProfile", preview)) {
        for (int i = 0; i < (int)profile_names.size(); ++i) {
            bool is_selected = (selected_profile == i);
            if (ImGui::Selectable(profile_names[i].c_str(), is_selected)) {
                selected_profile = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Load") && selected_profile >= 0 && selected_profile < (int)profile_names.size()) {
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete") && selected_profile >= 0 && selected_profile < (int)profile_names.size()) {
        needs_refresh = true;
        selected_profile = -1;
    }

    // Save new profile section
    if (!show_save_input) {
        if (ImGui::Button("Save as New Profile...")) {
            show_save_input = true;
            new_profile_name[0] = '\0';
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset to Defaults")) {
            ResetToDefaults();
            result = true;
        }
    }

    ImGui::Separator();

    // Check for button press if binding is active
    if (binding_action >= 0) {
        int pressed = DetectPressedButton(binding_controller);
        if (pressed >= 0) {
            actions[binding_action] = static_cast<CGamepadMapping::eButton>(pressed);
            if (binding_action == 7) actions[49] = actions[7]; // use left weapon
            if (binding_action == 7) actions[28] = actions[7]; // thrust
            if (binding_action == 7) actions[24] = actions[7]; // vehicle function 1
            if (binding_action == 9) actions[21] = actions[9]; // vehicle function 2
            if (binding_action == 0) actions[22] = actions[0]; // vehicle function 3
            binding_action = -1;
            result = true;
        }
    }

    for (int i = 0; i < action_names.size(); ++i) {
        auto name = action_names.at(i);
        if (name == nullptr) {
            snprintf(buffer, sizeof(buffer), "Action %d", i);
            name = buffer;
        }

        bool is_binding = (binding_action == i);

        if (is_binding) {
            // Show binding prompt
            ImGui::Text("%s:", name);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press button...");
            ImGui::SameLine();
            snprintf(buffer, sizeof(buffer), "Cancel##%d", i);
            if (ImGui::Button(buffer)) {
                binding_action = -1;
            }
        } else {
            // Show current binding with dropdown and bind button
            ImGui::PushItemWidth(150);
            // Convert None (-1) to index 16 for dropdown display
            int value = (actions[i] == CGamepadMapping::None) ? 16 : static_cast<int>(actions[i]);
            if (ImGui::Combo(name, &value, button_names.data(), button_names.size())) {
                // Convert index 16 back to None (-1) for storage
                actions[i] = (value == 16) ? CGamepadMapping::None : static_cast<CGamepadMapping::eButton>(value);
                if (i == 7) actions[49] = actions[7]; // Use Left Weapon mirrors Throw Grenade
                if (i == 7) actions[28] = actions[7]; // Thrust mirrors Throw Grenade
                if (i == 7) actions[24] = actions[7]; // Vehicle Function 1 mirrors Throw Grenade
                if (i == 9) actions[21] = actions[9]; // Vehicle Function 2 mirrors Crouch
                if (i == 0) actions[22] = actions[0]; // Vehicle Function 3 mirrors Jump
                result = true;
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            snprintf(buffer, sizeof(buffer), "Bind##%d", i);
            if (ImGui::Button(buffer) && binding_action < 0) {
                binding_action = i;
            }
        }
    }

    if (result) {
        auto p_engine = GameEngine();
        if (p_engine) p_engine->load_setting();
    }
}
