#pragma once

#define DefGlobal(name) \
    struct name##_t;    \
    extern name##_t s_##name; \
    inline name##_t* name() {return &s_##name;} \
    struct name##_t

#define ImplGlobal(name) \
    name##_t s_##name;

namespace AlphaRing::Global {
    DefGlobal(Global) {
        bool wireframe;
        bool wireframe_model;
        bool wireframe_structure;
        bool show_imgui = false;
        // on menu
        bool show_imgui_mouse = false;
        bool pause_game_on_menu_shown = false;
        bool disable_input_on_menu_shown = true;

        // Debug-only: skips HaloReach's 2-player black-bar painter entirely
        // (neither the original function nor our per-slot detour runs). Needed
        // when testing non-shipped splitscreen viewport shapes (e.g. left/right)
        // via the Splitscreen Config Editor, since the painter otherwise assumes
        // every slot is a horizontal strip and paints over the other slot's half.
        bool disable_splitscreen_bars_debug = false;

        // Vertical-splitscreen HUD shape, flippable at runtime from Dev Tools.
        //
        //   0 = off      leave the game's own frame untouched
        //   1 = A        match stock proportions (frameAspect 3.4286)
        //   2 = B        geometrically round radar (frameAspect 2.7190)
        //
        // Measured basis: radar aspect = 0.368 * (halfW / halfH), 5/5 within
        // 1% over frameAspect 0.914-3.43 and screen heights 720-1440. Stock
        // Reach renders the radar at 1.264 - it is NOT round - so A reproduces
        // the familiar shape while B is geometrically correct but unfamiliar.
        int splitscreen_hud_target = 0;

        // Alpha Ring-owned per-slot split-screen FOV (fov_baseline.cpp).
        // Horizontal degrees with MCC FOVSetting semantics (70-120). Slots
        // with *_set false keep Reach's native split-screen FOV.
        bool  splitscreen_fov_set[4] = {};
        float splitscreen_fov_deg[4] = { 78.0f, 78.0f, 78.0f, 78.0f };
    };

    namespace Halo3 {
        DefGlobal(Physics) {
            bool enable_bump_possession;
        };

        DefGlobal(Render) {
            bool model;
            bool structure;
        };
    }

    namespace MCC {
        DefGlobal(Splitscreen) {
            bool b_override;
            int player_count = 1;

            // player 0
            bool b_player0_use_km = true;
            bool b_override_profile = true;
            bool b_use_player0_profile = true;
        };
    }
}
