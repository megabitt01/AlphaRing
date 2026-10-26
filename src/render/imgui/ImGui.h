#pragma once

namespace AlphaRing::Render::ImGui {
    bool Initialize();

    void Render();

    // The Xbox menu (F4) and the advanced menu (F1) are never open together:
    // opening one closes the other. The advanced menu waits for the Xbox
    // menu's close animation, which commits its settings, before it shows.
    void ToggleXboxMenu();
    void ToggleAdvancedMenu(bool showMouse);
}