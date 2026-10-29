// Each split-screen view's own title-safe box, which its crosshair and aim centre on
// (MCC::Splitscreen::LeftRight::ViewSafeBox): Reach works a view's rect and its box out together.
#include "haloreach.h"

#include "mcc/module/entry/PreservingThunk.h"
#include "mcc/splitscreen/LeftRight.h"

namespace HaloReach::Entry::ViewSafe {
    // a leaf, hence the preserving thunk
    PreservedEntry(entry_viewport_rect, HaloReachEntrySet(), OFFSET_HALOREACH_PF_COMPUTE_VIEWPORT_RECT,
                   viewport_rect, void* slot, void* players, void* view, void* box, __int64) {
        return MCC::Splitscreen::LeftRight::ViewportRect(entry_viewport_rect.m_pOriginal, slot, players, view, box);
    }
}
