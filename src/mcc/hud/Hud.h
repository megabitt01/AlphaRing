#pragma once

// Per-player HUD customization: move, scale or hide HUD elements and recolour the HUD, separately
// for each local player.
//
// Halo CE, 2, 3, ODST and Reach ask MCC's host (CGameManager) for a per-element override right before
// placing each HUD element (see CGameManagerHud.cpp); Halo 3, ODST and Reach also pass every HUD colour,
// with the drawing player, through the host. Answering those calls for the player being drawn is
// the whole trick: the games apply the override to that element and put it back after drawing.
// Halo CE applies host offsets wrongly (both to X), so it gets its offsets from a placement hook
// instead (module/entry/halo1/hud.cpp).
//
// The settings are kept in memory for the session; nothing is saved yet.
namespace MCC::Hud {
    enum Element : int { MotionSensor, Shield, Weapon, Grenades, Crosshair, Equipment, Messages, kElementCount };

    struct ElementLayout {
        float x = 0.0f, y = 0.0f; // offset, as a fraction of the HUD canvas width/height
        float scale = 1.0f;
        bool hidden = false;
    };

    // Screen shapes (width / height) a player's HUD can be laid out in, centered in their view:
    // the game's own layout, the view's full width (its edges), or a box of that shape.
    constexpr float kFullWidth = 100.0f;
    constexpr float kAreas[] = {0.0f, kFullWidth, 21.0f / 9.0f, 16.0f / 9.0f, 4.0f / 3.0f};
    constexpr int kAreaCount = sizeof(kAreas) / sizeof(*kAreas);

    struct PlayerHud {
        float scale = 1.0f;      // every element
        int area = 0;            // index in kAreas
        ElementLayout elements[kElementCount];
        bool recolor = false;
        float hue = 200.0f;      // degrees
        float strength = 1.0f;   // 0 = game colours, 1 = fully the chosen hue
    };

    constexpr int kPlayers = 4;

    // A player's settings, as the games' HUD drawing reads them: a copy, and a whole new set put in
    // place at once (the HUD window publishes its edits so). What a profile would save and load. A
    // default PlayerHud changes nothing.
    PlayerHud Settings(int player);
    void SetSettings(int player, const PlayerHud& settings);

    // Host callbacks (CGameManagerHud.cpp): true when `element` of the player whose HUD is being
    // drawn has an override, composed onto MCC's own values. `caller`: the game's return address.
    bool Transform(int game_element, float* dx, float* dy, float* scale, const void* caller = nullptr);
    bool Anchor(int game_element, int* anchor);
    unsigned Color(int user, unsigned argb);

    // Halo CE: pixels to move `element` of `player`'s HUD within a view of the given size.
    bool OffsetCE(int player, int game_element, float view_width, float view_height, float* dx, float* dy);

    // The HUD window (overlay menu bar).
    void ImGuiContext();
}
