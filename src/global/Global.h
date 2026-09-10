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