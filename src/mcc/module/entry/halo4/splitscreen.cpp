// Left/Right split screen for Halo 4 (mcc/splitscreen/LeftRight), after XiaoDanny's Reach port.
#include "halo4.h"

#include "mcc/module/entry/PreservingThunk.h"
#include "mcc/module/patch/CPatch.h"
#include "mcc/splitscreen/LeftRight.h"

#include <cstring>

namespace Halo4::Entry::Splitscreen {
    namespace LeftRight = MCC::Splitscreen::LeftRight;

    EntryFeature("Halo 4 Left/Right split screen", OFFSET_HALO4_PV_SPLITSCREEN_TABLE, OFFSET_HALO4_PV_SCREEN_SIZE,
                 OFFSET_HALO4_PF_SPLITSCREEN_PLAYER_COUNT, OFFSET_HALO4_PF_FILL_RECT, OFFSET_HALO4_PF_RT_POOL_RELEASE,
                 OFFSET_HALO4_PF_RT_POOL_INIT, OFFSET_HALO4_PF_RENDER_SETUP_1, OFFSET_HALO4_PF_RENDER_SETUP_2);
    // A half's HUD only fits it with hud_fit.cpp's hooks (without them it keeps to the half's top half): on a build
    // without one of them, none of these go in either and Halo 4 keeps its stock split.
    const bool s_requires_hud_fit = (entry_feature->Requires(HudFit::Hooks()), true);

    // The bar painter sets the render state up itself before drawing (as Reach's does, blackbars.cpp).
    void BeforePainting(__int64 module) {
        ((void (*)(int, int))(module + OFFSET_HALO4_PF_RENDER_SETUP_1))(0, 1);
        ((void (*)(int))(module + OFFSET_HALO4_PF_RENDER_SETUP_2))(0);
    }

    // Each view's own title-safe box (LeftRight::ViewSafeBox, ViewportRect); a leaf, hence the preserving thunk.
    PreservedEntry(entry_viewport_rect, Halo4SplitscreenEntrySet(), OFFSET_HALO4_PF_COMPUTE_VIEWPORT_RECT,
                   viewport_rect, void* slot, void* players, void* view, void* box, __int64) {
        return LeftRight::ViewportRect(entry_viewport_rect.m_pOriginal, slot, players, view, box);
    }

    constexpr LeftRight::Gen3 kGame {
        OFFSET_HALO4_PV_SPLITSCREEN_TABLE, OFFSET_HALO4_PV_SCREEN_SIZE, OFFSET_HALO4_PF_SPLITSCREEN_PLAYER_COUNT,
        OFFSET_HALO4_PF_FILL_RECT, OFFSET_HALO4_PF_RT_POOL_RELEASE, OFFSET_HALO4_PF_RT_POOL_INIT,
        BeforePainting,
    };
    LeftRight::State s_state;

    // Halo 4 lays each view's HUD out from the view's table variant, the full-height halves' (3) taking the
    // two-player layout, which is as wide as a top/bottom half: in a Left/Right half it ran off the right edge.
    // The quarter layout is exactly a half's width.
    Halo4SplitscreenEntry(entry_hud_layout, OFFSET_HALO4_PF_HUD_LAYOUT, int, hud_layout, int user) {
        int layout = ((hud_layout_t)entry_hud_layout.m_pOriginal)(user);
        if (layout != kHalfHudLayout || !LeftRight::Chosen()) return layout;
        __int64 module = entry_hud_layout.m_target - entry_hud_layout.m_offset;
        return LeftRight::Active(LeftRight::Players(kGame, module)) ? kQuarterHudLayout : layout;
    }

    // Halo 4 scales the first-person camera's field of view by the screen's aspect over the view's (0x34EC44, times
    // the screen's, then this divide by the view's). That keeps the gun's framing in its own split-screen views,
    // all wider than tall, but a full-height Left/Right half doubled it: both arms and the whole gun came into frame,
    // drawn big. While Left/Right is on screen the divisor is the screen's aspect too (divss xmm5, xmm0 -> xmm1), so
    // the gun is framed like the world, as Halo 3 and Reach draw it. Switched before a frame is drawn, on the thread
    // drawing it.
    unsigned char* s_viewmodel_divide = nullptr; // the divide's register byte, while it's switched

    void RestoreViewmodelAspect() {
        unsigned char view = 0xE8; // xmm0
        if (s_viewmodel_divide != nullptr) CPatch::apply(s_viewmodel_divide, &view, 1);
        s_viewmodel_divide = nullptr;
    }
    const bool s_restore = (Halo4SplitscreenEntrySet()->on_remove(&RestoreViewmodelAspect), true);

    void ViewmodelAspect(__int64 module, bool left_right) {
        if (left_right == (s_viewmodel_divide != nullptr)) return;
        if (!left_right) return RestoreViewmodelAspect();
        auto divide = (unsigned char*)(module + OFFSET_HALO4_VIEWMODEL_ASPECT_DIVIDE);
        unsigned char screen = 0xE9; // xmm1
        if (OFFSET_HALO4_VIEWMODEL_ASPECT_DIVIDE.found() && memcmp(divide, "\xF3\x0F\x5E\xE8", 4) == 0 &&
            CPatch::apply(divide + 3, &screen, 1))
            s_viewmodel_divide = divide + 3;
    }

    Halo4SplitscreenEntry(entry_render, OFFSET_HALO4_PF_RENDER, void, render) {
        __int64 module = entry_render.m_target - entry_render.m_offset;
        LeftRight::Frame(kGame, module, s_state);
        HudFit::Refresh(module);
        ViewmodelAspect(module, LeftRight::Active(LeftRight::Players(kGame, module)));
        ((render_t)entry_render.m_pOriginal)();
    }

    Halo4SplitscreenEntry(entry_bars, OFFSET_HALO4_PF_DRAW_SPLITSCREEN_BARS, void, draw_bars) {
        if (!LeftRight::PaintDividers(kGame, entry_bars.m_target - entry_bars.m_offset))
            ((draw_bars_t)entry_bars.m_pOriginal)();
    }

    // Halo 4 sizes a pool target's view variant in a helper of its own: the descriptor's size, then for
    // variant 3 three quarters of the width by half the height, both pairs alike. For Left/Right's half
    // surface the helper is asked for the unshrunk size (variant 0 of the same target) and that is halved
    // in width, the way the pool halves for its other variants.
    Halo4SplitscreenEntry(entry_rt_size, OFFSET_HALO4_PF_RT_VARIANT_SIZE, void, variant_size,
                          const unsigned char* desc, int* w, int* h, int* w2, int* h2, int* face, int index) {
        ((variant_size_t)entry_rt_size.m_pOriginal)(desc, w, h, w2, h2, face, index);
        if (!LeftRight::ResizesHalfSurface(s_state, index) || !(*(const unsigned*)(desc + 0x10) & 0x20)) return;

        int full_w, full_h, full_w2, full_h2, full_face = 0;
        ((variant_size_t)entry_rt_size.m_pOriginal)(desc, &full_w, &full_h, &full_w2, &full_h2, &full_face,
                                                  index & ~3);
        *w = *w2 = full_w / 2;
        *h = *h2 = full_h;
    }
}
