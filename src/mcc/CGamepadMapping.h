#pragma once

#include <array>

struct CGamepadMapping {
    enum eButton : __int8 {
        LeftTrigger, RightTrigger,
        DpadUp, DpadDown, DpadLeft, DpadRight,
        Start, Back,
        LeftThumb, RightThumb,
        LeftShoulder, RightShoulder,
        A, B, X, Y,
        None = -1  // Unbound - no button assigned
    };

    eButton actions[66];

    // profileActionsHidden: leaves out the actions a controller profile sets
    // (and their mirrors) and the Reset button, for when a profile editor
    // shows those instead.
    void ImGuiContext(bool profileActionsHidden = false);
    void ResetToDefaults();

    static const std::array<const char*, 17>* ButtonNames();
    static const std::array<const char*, 66>* ActionNames();

    // The button (an eButton) held on a controller right now, or -1 for none.
    static int DetectPressedButton(int controllerIndex);
};