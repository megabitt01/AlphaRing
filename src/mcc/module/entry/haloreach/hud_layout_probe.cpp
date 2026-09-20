#include "haloreach.h"

#include "common.h"
#include "log/DebugFlags.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"

#include <intrin.h>

// Left/Right HUD semantic fix. See docs/REVERSE_ENGINEERING.md, "Resolved:
// Left/Right HUD fixes" for full context.
//
// ROOT CAUSE (Ghidra xref-confirmed): UpdatePlayerHudView (0x2D94BC) is the
// ONLY writer of D1F710 in haloreach.dll, and every call copies it from a
// separate, persistent per-player field (TLS-rooted context +
// 0x5DE8 + player_index*0x10D68) - NOT from g_SplitscreenConfigTable, which
// correctly holds res=3 for Left/Right throughout. That upstream field can
// read 2, so both the global HUD-layout selector (FUN_1802d91bc, result in
// D1F6F8) and every per-widget lookup (FUN_1802da0c0, keyed directly by
// D1F710) then run on the semantic-2 record, not Left/Right's real res=3
// geometry. This hook forces D1F710 to 3 at the confirmed call boundary
// immediately after that write - the earliest point it can intercept.
//
// SCOPE: this is a boundary fix at the one confirmed seam between the write
// and its consumer chain. It does not modify the upstream +0x5DE8 field, the
// splitscreen table (already correct), D1F6F8's selection mechanism, any
// per-widget record content, or any CHUD transform.
//
// GATE ON THE ACTIVE LAYOUT, NOT THE SAVED PREFERENCE. UpdatePlayerHudView
// runs this selector for every local player at every player count, and the
// saved LeftRight preference persists into 1p/3p/4p games. Gating on the
// preference alone forced the 2p pillarbox HUD record onto a full-screen solo
// slot (see the same invariant in REVERSE_ENGINEERING.md, "Black-bar
// behavior"). The live count comes from GetSplitscreenPlayerCount, which
// UpdatePlayerHudView itself calls later in the same update on this thread.
namespace {
    constexpr __int64 UPDATE_HUD_SELECTOR_RETURN_RVA = 0x2D95DF;

    typedef unsigned __int64 (*GetSplitscreenPlayerCount_t)();
}

namespace HaloReach::Entry::HudLayoutProbe {
    // At the confirmed UpdatePlayerHudView -> FUN_1802d91bc call site, for a
    // slot that is a full-height Left/Right half in the ACTIVE layout
    // (UsesFullHeightLeftRightSlot - both 2p slots, 3p player 1 only; 3p
    // players 2/3 are stock quadrants and keep their native semantic):
    // overwrite the live D1F710 to 3 and call the
    // original global HUD-layout selector with semantic 3 instead of the
    // native value. D1F710 is intentionally left at 3 afterward (not
    // restored), so per-widget FUN_1802da0c0 lookups later in this same
    // player's HUD update also see 3 - required for the fix to be complete,
    // since those lookups read D1F710 directly rather than D1F6F8.
    //
    // Every other call - other return addresses (including FUN_1802d91bc's
    // one other, unrelated caller), any player count or saved layout for which
    // Left/Right is not active - passes through to the original selector
    // completely unchanged.
    HaloReachEntry(entry, OFFSET_HALOREACH_PF_SELECT_HUD_LAYOUT_SUBRECORD,
                   int, detour, __int64 context, int hudDefinitionIndex,
                   unsigned char semantic) {
        __int64 hModule = entry.m_target - entry.m_offset;
        __int64 returnRva = (__int64)_ReturnAddress() - hModule;

        if (returnRva == UPDATE_HUD_SELECTOR_RETURN_RVA) {
            auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(
                    hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
            int playerCount = (int)GetSplitscreenPlayerCount();
            // UpdatePlayerHudView writes the player index (0x2D957D) before
            // this call, so it names the slot whose HUD is being selected.
            int slot = *(int*)(hModule + OFFSET_HALOREACH_DAT_CURRENT_HUD_PLAYER);

            if (AlphaRing::SplitscreenConfigStore::UsesFullHeightLeftRightSlot(playerCount, slot)) {
                *(int*)(hModule + OFFSET_HALOREACH_DAT_CURRENT_HUD_RESOLUTION) = 3;
                return ((detour_t)entry.m_pOriginal)(context, hudDefinitionIndex, 3);
            }
        }

        return ((detour_t)entry.m_pOriginal)(context, hudDefinitionIndex, semantic);
    }
}
