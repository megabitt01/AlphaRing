// Left/Right split screen for Halo CE (mcc/splitscreen/LeftRight): CE lays its views out on a grid that
// always grows by rows first; two players get it turned into two columns, and three players the windows of
// both: player 1 a two-player half, players 2 and 3 the right quarters of a four-player grid.
#include "halo1.h"

#include "mcc/CGameGlobal.h"

#include "mcc/module/entry/PreservingThunk.h"
#include "mcc/splitscreen/LeftRight.h"

namespace Halo1::Entry::Splitscreen {
    using MCC::Splitscreen::LeftRight::Rect;

    EntryFeature("Halo CE Left/Right split screen", OFFSET_HALO1_PF_SPLIT_VIEWS, OFFSET_HALO1_PV_WINDOW_BOUNDS,
                 OFFSET_HALO1_PV_SCREEN_BOUNDS, OFFSET_HALO1_PF_FILL_RECT, OFFSET_HALO1_PV_ANNIVERSARY_SHOWN);

    // Side by side is Classic's layout (Back switches to Anniversary's stacked views in the middle of a mission).
    bool OnScreen(__int64 module, int views) {
        return *(int*)(module + OFFSET_HALO1_PV_ANNIVERSARY_SHOWN) == 0 &&
               MCC::Splitscreen::LeftRight::OnScreen(CGameGlobal::Halo1, views);
    }

    int Views(__int64 module) { return ((int (*)())(module + OFFSET_HALO1_PF_SPLIT_VIEWS))(); }

    Halo1Entry(entry_grid, OFFSET_HALO1_PF_SPLIT_GRID, void, split_grid, int views, int* columns, int* rows) {
        ((split_grid_t)entry_grid.m_pOriginal)(views, columns, rows);
        if (views == 2 && OnScreen(entry_grid.m_target - entry_grid.m_offset, 2)) {
            *columns = 2;
            *rows = 1;
        }
    }

    Halo1Entry(entry_window, OFFSET_HALO1_PF_SPLIT_WINDOW, void, split_window, int view, int views, short* rect,
               short* copy) {
        if (views == 3 && view >= 0 && view < 3 && OnScreen(entry_window.m_target - entry_window.m_offset, 3)) {
            views = view == 0 ? 2 : 4;
            if (view == 2) view = 3;
        }
        ((split_window_t)entry_window.m_pOriginal)(view, views, rect, copy);
    }

    // The stock dividers are 4px bands across the screen's middle (and one down it for 3-4 players).
    Halo1Entry(entry_dividers, OFFSET_HALO1_PF_SPLIT_DIVIDERS, void, split_dividers) {
        __int64 module = entry_dividers.m_target - entry_dividers.m_offset;
        int views = Views(module);
        if (!OnScreen(module, views)) return ((split_dividers_t)entry_dividers.m_pOriginal)();
        // the dividers' window is the screen
        MCC::Splitscreen::LeftRight::PaintBands(*(const Rect*)(module + OFFSET_HALO1_PV_WINDOW_BOUNDS), 2, views,
                                                (void (*)(Rect*, unsigned))(module + OFFSET_HALO1_PF_FILL_RECT));
    }

    // A zoomed view's scope effects (pistol, sniper) blur the screen image under the scope mask. MCC re-samples
    // each view's part of it with factors it works out for the stock grid - how many views the screen is wide
    // and tall, and the view's place in them - which side by side put a player's zoomed view in the wrong part
    // of the screen, squeezed and stretched. These are the same factors, from the view's own window; for the
    // stock layouts they come out as MCC's.
    extern ::Entry entry_scope_grid;
    void ScopeGrid(char* locals) {
        __int64 module = entry_scope_grid.m_target - entry_scope_grid.m_offset;
        if (!OnScreen(module, Views(module))) return;
        auto view = *(const Rect*)(module + OFFSET_HALO1_PV_WINDOW_BOUNDS);
        auto screen = *(const Rect*)(module + OFFSET_HALO1_PV_SCREEN_BOUNDS);
        float width = (float)(view.right - view.left), height = (float)(view.bottom - view.top);
        if (width <= 0 || height <= 0) return;
        *(float*)(locals + 0x40) = (screen.right - screen.left) / width;  // screen width in views
        *(float*)(locals + 0x44) = (screen.bottom - screen.top) / height; // screen height in views
        *(float*)(locals + 0x48) = (screen.left - view.left) / width;     // minus the view's column
        *(float*)(locals + 0x64) = (screen.top - view.top) / height;      // minus the view's row
    }
    constexpr unsigned char kReloadRow[] = {0xF3, 0x44, 0x0F, 0x10, 0x7C, 0x24, 0x64}; // movss xmm15, [rsp+0x64]
    ::Entry entry_scope_grid(Halo1EntrySet(), OFFSET_HALO1_SCOPE_GRID_SET,
                             MidFunctionThunk(&ScopeGrid, &entry_scope_grid.m_pOriginal, kReloadRow, sizeof(kReloadRow)),
                             entry_feature);
}
