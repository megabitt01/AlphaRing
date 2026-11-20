#include "halo2.h"

#include "mcc/CGameGlobal.h"
#include "mcc/spawn/Command.h"

namespace MCC::Spawn::Halo2 { const AlphaRing::Feature& Offsets(); }

namespace Halo2::Entry::Console {
    // Game-thread entry for AlphaRing commands, see mcc/spawn/Command.h. Only spawning posts them, so the hook goes
    // in only where Halo 2's spawning has every offset it uses (mcc/spawn/halo2.cpp).
    EntryFeature("Halo 2 spawning commands", OFFSET_HALO2_PF_HS_COMPILE);
    const bool s_requires_spawning = (entry_feature->Requires(MCC::Spawn::Halo2::Offsets()), true);

    Halo2Entry(entry, OFFSET_HALO2_PF_HS_COMPILE, bool, detour, const char* text, bool flag) {
        if (MCC::Command::Dispatch(CGameGlobal::Halo2, text))
            return false;
        return ((detour_t)entry.m_pOriginal)(text, flag);
    }
}
