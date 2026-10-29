// Halo 4's HUD in a Left/Right half. A half is 960x1080 for the quarter layout's 640x360 units (HUD units:
// pixels over min(width / 1280, height / 720) of the screen), and the layout sits in the view's corner, so the
// HUD kept to the top half of the view, the motion tracker at mid-height.
//
// Halo 4 already fits its 1280x720 layout to screens of another shape (ultrawide): four helpers give the
// screen's shape against 16:9 - wider or not, the aspect over 16:9's, the extra width or height either side
// - and the UI uses them to centre each screen, keep edge-anchored elements at the screen's edges and place
// what it projects from the world (nametags, waypoints). While a half's HUD is updated or drawn, the helpers
// answer for that view and its layout instead (the other split views keep the game's own answers). The
// crosshair sits at the view's centre already, so the screen's centring would move it: its group is moved back.
#include "halo4.h"

#include "mcc/module/entry/PreservingThunk.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"
#include "mcc/splitscreen/LeftRight.h"

#include <algorithm>
#include <cmath>

namespace Halo4::Entry::HudFit {
    namespace LeftRight = MCC::Splitscreen::LeftRight;
    namespace Store = AlphaRing::SplitscreenConfigStore;

    EntryFeature("Halo 4 split-screen HUD fit", OFFSET_HALO4_PV_SPLITSCREEN_TABLE, OFFSET_HALO4_PV_SCREEN_SIZE,
                 OFFSET_HALO4_PF_HUD_LAYOUT, OFFSET_HALO4_PF_SPLITSCREEN_PLAYER_COUNT, OFFSET_HALO4_PF_UI_USER_INDEX,
                 OFFSET_HALO4_VT_UI_GROUP, OFFSET_HALO4_VT_HUD_RETICLE_GROUP);
    const ::AlphaRing::Feature& Hooks() { return *entry_feature; }

    struct Fit {
        bool on = false;
        float aspect = 1.0f;                  // the view's aspect over its layout's (wide when above 1)
        float extra_x = 0.0f, extra_y = 0.0f; // HUD units either side of the layout
    };

    thread_local int t_user = -1; // the user whose HUD is being updated or drawn
    Fit s_fits[4];                // each user's, as of this frame (Refresh)
    bool s_any_fit = false;       // this frame
    bool s_ever_fit = false;      // since the module loaded: crosshair groups may carry our offset

    Fit Compute(__int64 module, int players, int user) {
        Fit fit;
        if (players < 2 || players > 4 || user >= players) return fit;
        // a Left/Right half (both two-player views, the three-player left one)
        if (!LeftRight::Active(players) || Store::LeftRightRegion(players, user).resolution != 3) return fit;
        auto& view = ((const Store::LayoutEntry*)(module + OFFSET_HALO4_PV_SPLITSCREEN_TABLE))
            [players * Store::SLOT_COUNT + user];
        auto screen = (const int*)(module + OFFSET_HALO4_PV_SCREEN_SIZE);
        float width = (float)screen[0], height = (float)screen[1];
        if (width < 1 || height < 1) return fit;
        float scale = (std::min)(width / 1280.0f, height / 720.0f);
        float view_w = width * (view.x1 - view.x0) / scale, view_h = height * (view.y1 - view.y0) / scale;
        int layout = ((int (*)(int))(module + OFFSET_HALO4_PF_HUD_LAYOUT))(user);
        float layout_w = layout == kHalfHudLayout ? 960.0f : layout == kQuarterHudLayout ? 640.0f : 1280.0f;
        float layout_h = layout == kFullHudLayout ? 720.0f : 360.0f;
        if (view_h < 1 || (std::abs(view_w - layout_w) < 1 && std::abs(view_h - layout_h) < 1)) return fit;
        fit.on = true;
        fit.aspect = (view_w / view_h) / (layout_w / layout_h);
        if (fit.aspect > 1.0f) fit.extra_x = (view_w - layout_w) * 0.5f;
        else fit.extra_y = (view_h - layout_h) * 0.5f;
        return fit;
    }

    void Refresh(__int64 module) {
        s_any_fit = false;
        if (!entry_feature->Available()) return; // from splitscreen.cpp's render hook: only with the hooks below
        int players = ((int (*)())(module + OFFSET_HALO4_PF_SPLITSCREEN_PLAYER_COUNT))();
        for (int user = 0; user < 4; ++user) {
            s_fits[user] = Compute(module, players, user);
            s_any_fit |= s_fits[user].on;
        }
        s_ever_fit |= s_any_fit;
    }
    const bool s_reset = (Halo4SplitscreenEntrySet()->on_remove([] { s_ever_fit = false; }), true);

    // The fit of the user whose HUD is being worked on, when it's on.
    const Fit* Current() {
        return s_any_fit && t_user >= 0 && t_user < 4 && s_fits[t_user].on ? &s_fits[t_user] : nullptr;
    }

    struct UserScope {
        int outer = t_user;
        explicit UserScope(int user) { t_user = user; }
        ~UserScope() { t_user = outer; }
    };

    // whose HUD
    Halo4SplitscreenEntry(entry_update, OFFSET_HALO4_PF_HUD_UPDATE, __int64, hud_update, void* hud, __int64 user,
                          void* r8, void* r9) {
        UserScope scope((int)user);
        return ((hud_update_t)entry_update.m_pOriginal)(hud, user, r8, r9);
    }

    Halo4SplitscreenEntry(entry_render, OFFSET_HALO4_PF_HUD_RENDER, __int64, hud_render, void* rcx, __int64 user,
                          void* r8, void* r9) {
        UserScope scope((int)user);
        return ((hud_render_t)entry_render.m_pOriginal)(rcx, user, r8, r9);
    }

    // the helpers: leaves, whose callers keep values in volatile registers across them
    extern ::Entry entry_wide, entry_aspect, entry_extra_width, entry_extra_height;

    __int64 Wide(void*, void*, void*, void*, __int64) {
        if (const Fit* fit = Current()) return fit->aspect > 1.0f;
        return ((bool (*)())entry_wide.m_pOriginal)();
    }

    float Aspect(void* inverse, void*, void*, void*, __int64) {
        if (const Fit* fit = Current()) return (unsigned char)(__int64)inverse ? 1.0f / fit->aspect : fit->aspect;
        return ((float (*)(void*))entry_aspect.m_pOriginal)(inverse);
    }

    float ExtraWidth(void*, void*, void*, void*, __int64) {
        if (const Fit* fit = Current()) return fit->extra_x;
        return ((float (*)())entry_extra_width.m_pOriginal)();
    }

    float ExtraHeight(void*, void*, void*, void*, __int64) {
        if (const Fit* fit = Current()) return fit->extra_y;
        return ((float (*)())entry_extra_height.m_pOriginal)();
    }

    ::Entry entry_wide(Halo4SplitscreenEntrySet(), OFFSET_HALO4_PF_UI_WIDE, PreservingThunk((const void*)&Wide),
                       entry_feature);
    ::Entry entry_aspect(Halo4SplitscreenEntrySet(), OFFSET_HALO4_PF_UI_ASPECT, PreservingThunk((const void*)&Aspect),
                         entry_feature);
    ::Entry entry_extra_width(Halo4SplitscreenEntrySet(), OFFSET_HALO4_PF_UI_EXTRA_WIDTH,
                              PreservingThunk((const void*)&ExtraWidth), entry_feature);
    ::Entry entry_extra_height(Halo4SplitscreenEntrySet(), OFFSET_HALO4_PF_UI_EXTRA_HEIGHT,
                               PreservingThunk((const void*)&ExtraHeight), entry_feature);

    // The UI places some widgets outside the HUD update and draw, in a context naming its user (+0x28).
    int ContextUser(__int64 module, const char* context) {
        if (t_user >= 0 || !s_any_fit) return t_user;
        return ((int (*)(int))(module + OFFSET_HALO4_PF_UI_USER_INDEX))(*(const int*)(context + 0x28));
    }

    // An element anchored to the screen's edges keeps its x there only where the user's HUD settings let the
    // ultrawide layout move it; a split view's edge elements always follow its edges.
    Halo4SplitscreenEntry(entry_anchor, OFFSET_HALO4_PF_UI_ANCHOR, void, anchor, char* element, char* context) {
        UserScope user(ContextUser(entry_anchor.m_target - entry_anchor.m_offset, context));
        ((anchor_t)entry_anchor.m_pOriginal)(element, context);
        const Fit* fit = Current();
        if (fit == nullptr || *(int*)(element + 0x1D4) != 2) return; // x anchoring set up: base +0x1D8, side +0x1DC
        *(float*)(element + 0xBC) = *(float*)(element + 0x1D8) + fit->extra_x * (float)*(int*)(element + 0x1DC);
        *(float*)(element + 0xC0) = 0.0f;
    }

    Halo4SplitscreenEntry(entry_fit_range, OFFSET_HALO4_PF_UI_FIT_RANGE, void, fit_range, char* widget, char* context) {
        UserScope user(ContextUser(entry_fit_range.m_target - entry_fit_range.m_offset, context));
        ((fit_range_t)entry_fit_range.m_pOriginal)(widget, context);
    }

    // The widget tree: a node's first child from its vtable (+0x28), siblings at +0x40; widgets have bit 1 of
    // their type's flags (+8 -> +0xC).
    char* FirstChild(char* node) { return ((char* (*)(char*))(*(void***)node)[0x28 / 8])(node); }
    char* Next(char* node) { return *(char**)(node + 0x40); }
    bool IsWidget(char* node, __int64 vtable) {
        char* type = *(char**)(node + 8);
        return type != nullptr && (*(unsigned char*)(type + 0xC) & 2) && *(__int64*)node == vtable;
    }

    // Each weapon's HUD screen holds its crosshair in a group under one of the screen's top-level groups: the
    // group with a widget at the layout's centre, 640, 360 in HUD units.
    char* ReticleGroup(__int64 module, char* screen) {
        for (char* top = FirstChild(screen); top != nullptr; top = Next(top)) {
            if (!IsWidget(top, module + OFFSET_HALO4_VT_UI_GROUP)) continue;
            for (char* group = FirstChild(top); group != nullptr; group = Next(group)) {
                if (!IsWidget(group, module + OFFSET_HALO4_VT_HUD_RETICLE_GROUP)) continue;
                for (char* widget = FirstChild(group); widget != nullptr; widget = Next(widget))
                    if (*(float*)(widget + 0xBC) == 640.0f && *(float*)(widget + 0xC4) == 360.0f) return group;
            }
        }
        return nullptr;
    }

    Halo4SplitscreenEntry(entry_screen_render, OFFSET_HALO4_PF_UI_SCREEN_RENDER, void, screen_render, char* screen,
                          void* context) {
        if (t_user >= 0 && s_ever_fit) {
            if (char* group = ReticleGroup(entry_screen_render.m_target - entry_screen_render.m_offset, screen)) {
                const Fit* fit = Current(); // back to 0 when the view fits
                *(float*)(group + 0xC0) = fit != nullptr ? -fit->extra_x : 0.0f;
                *(float*)(group + 0xC8) = fit != nullptr ? -fit->extra_y : 0.0f;
            }
        }
        ((screen_render_t)entry_screen_render.m_pOriginal)(screen, context);
    }
}
