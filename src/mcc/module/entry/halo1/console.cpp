#include "halo1.h"

#include "mcc/CGameGlobal.h"
#include "mcc/spawn/Command.h"

namespace MCC::Spawn::Halo1 { const AlphaRing::Feature& Offsets(); }

namespace Halo1::Entry::Console {
    // Game-thread entry for AlphaRing commands, see mcc/spawn/Command.h. Only spawning posts them, so the hook goes
    // in only where Halo CE's spawning has every offset it uses (mcc/spawn/halo1.cpp).
    EntryFeature("Halo CE spawning commands", OFFSET_HALO1_PF_HS_COMPILE);
    const bool s_requires_spawning = (entry_feature->Requires(MCC::Spawn::Halo1::Offsets()), true);

    Halo1Entry(entry, OFFSET_HALO1_PF_HS_COMPILE, void, detour, const char* text) {
        if (MCC::Command::Dispatch(CGameGlobal::Halo1, text))
            return;
        ((detour_t)entry.m_pOriginal)(text);
    }
}
