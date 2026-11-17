// Left/Right split screen for Halo 2 (mcc/splitscreen/LeftRight). Halo 2 lays its views out on a grid it
// grows by rows or by columns, by a mode MCC's build always passes as 1 (rows: Top/Bottom); any other mode
// grows columns first, and with three players gives player 1 the full-height left half.
#include "halo2.h"

#include "mcc/CGameGlobal.h"
#include "mcc/splitscreen/LeftRight.h"

namespace Halo2::Entry::Splitscreen {
    namespace LeftRight = MCC::Splitscreen::LeftRight;

    EntryFeature("Halo 2 Left/Right split screen", OFFSET_HALO2_PV_SPLIT_MODE, OFFSET_HALO2_PV_SPLIT_VIEWS,
                 OFFSET_HALO2_PV_ANNIVERSARY, OFFSET_HALO2_PV_CLASSIC_SHOWN);

    // Side by side is Classic's layout (Back switches to Anniversary's stacked views in the middle of a mission).
    bool ClassicShown(__int64 module) {
        return *(int*)(module + OFFSET_HALO2_PV_ANNIVERSARY) == 0 || *(bool*)(module + OFFSET_HALO2_PV_CLASSIC_SHOWN);
    }

    int Mode(__int64 module, int views, int mode) {
        return ClassicShown(module) && LeftRight::OnScreen(CGameGlobal::Halo2, views) ? 2 : mode;
    }

    Halo2Entry(entry_grid, OFFSET_HALO2_PF_SPLIT_GRID, void, split_grid, int views, int mode, short* grid) {
        ((split_grid_t)entry_grid.m_pOriginal)(views, Mode(entry_grid.m_target - entry_grid.m_offset, views, mode), grid);
    }

    Halo2Entry(entry_cell, OFFSET_HALO2_PF_SPLIT_CELL, void, split_cell, int view, int views, int mode,
               const short* grid, short* cell, short* span) {
        ((split_cell_t)entry_cell.m_pOriginal)(view, views, Mode(entry_cell.m_target - entry_cell.m_offset, views, mode),
                                               grid, cell, span);
    }

    // The dividers take the frame's mode from a global the frame's render entry sets: side by side, a band
    // down the middle (and across the right half for three players) instead of across the screen.
    Halo2Entry(entry_dividers, OFFSET_HALO2_PF_SPLIT_DIVIDERS, void, split_dividers) {
        __int64 module = entry_dividers.m_target - entry_dividers.m_offset;
        auto& mode = *(int*)(module + OFFSET_HALO2_PV_SPLIT_MODE);
        int stock = mode;
        mode = Mode(module, *(const int*)(module + OFFSET_HALO2_PV_SPLIT_VIEWS), stock);
        ((split_dividers_t)entry_dividers.m_pOriginal)();
        mode = stock;
    }
}
