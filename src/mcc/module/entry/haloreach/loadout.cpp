#include "haloreach.h"

#include "common.h"

#include <cstring>
#include <intrin.h>

#include "log/DebugFlags.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"

namespace HaloReach::Entry::Loadout {
    typedef unsigned __int64 (*GetSplitscreenPlayerCount_t)();

    // 0x2CA86C is Reach's generic per-window CUI resolution-variant selector
    // (used by every per-player CUI screen, not only loadout). For windows 0-3
    // it reads m_config_table[playerCount*4 + window].res: 3 selects
    // resolution_widescreen_half (0x80046), 2 selects
    // resolution_widescreen_quarter (0x80047), and any other value (including
    // 1P's 0) selects the base resolution_widescreen (0x80045). Windows 4/5
    // always get 0x80045.
    //
    // res=1 (two-player black bars removed) and res=5 (legacy custom vertical
    // marker) would therefore select the fullscreen variant for a two-player
    // window; this hook maps both to the half variant that stock res=3 uses.
    // See REVERSE_ENGINEERING.md "Scoreboard and loadout: shared CUI
    // screen-variant path".
    //
    // Same bounded distinct-state filter as hud_anchor.cpp. This hook is called
    // several thousand times per run per slot; unfiltered it produced 25k lines.
    static bool SeenBefore(int a, int b, int c, int d) {
        struct S { int v[4]; };
        constexpr int MAXN = 24;
        static S   seen[MAXN] = {};
        static int count = 0;
        S now{ { a, b, c, d } };
        for (int i = 0; i < count; ++i)
            if (memcmp(seen[i].v, now.v, sizeof(now.v)) == 0) return true;
        if (count >= MAXN) return true;
        seen[count++] = now;
        return false;
    }

    HaloReachEntry(entry, OFFSET_HALOREACH_PF_SPLITSCREEN_RESOLUTION_RESOURCE, int, detour, int slot_index) {
        __int64 hModule = entry.m_target - entry.m_offset;

        // WHICH call site passed this slot index.
        //
        // The lookup is reached from five static call sites, and every one of
        // them passes the slot straight through from its own caller, so a static
        // XREF walk cannot attribute slot=4/5 without several more hops. Worse,
        // the function's address is also stored at DATA 0x184e6bafc, so it can
        // be called INDIRECTLY - static xrefs cannot enumerate those callers at
        // all.
        //
        // The return address settles it directly and covers the indirect case.
        // Logged as an RVA so it can be pasted into Ghidra as
        // 0x180000000 + rva. Known static call sites, for reference:
        //   0x2C9F36 FUN_1802c9efc      0x2CA04F FUN_1802ca014
        //   0x2C9FC2 FUN_1802c9f88      0x2F30FC SetupLoadoutBackdropCamera
        //   0x386397 FUN_180386384
        __int64 retRva = (__int64)_ReturnAddress() - hModule;

        if (slot_index >= 0 && slot_index < 4) {
            auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
            int player_count = (int)GetSplitscreenPlayerCount();
            int* p_resolution = (int*)(hModule + 0xB43C40 + 0x10 + (size_t)(slot_index + player_count * 4) * 20);

            // The selector indexes m_config_table by (slot + playerCount*4), so
            // the variant depends on the live player count at lookup time as
            // well as the slot. Log both, per caller, when diagnosing a CUI
            // screen's layout.
            if (!SeenBefore(0, slot_index, (int)retRva, *p_resolution))
            PROBE_LOG(AlphaRing::DebugFlags::g_loadout,
                      "[Loadout] slot={} player_count={} -> table index {} | resolution={} | caller rva={:#x} | {}",
                      slot_index, player_count, slot_index + player_count * 4, *p_resolution, retRva,
                      (*p_resolution == 1 || *p_resolution == 5) ? "OVERRIDE 0x80046"
                                                                 : "passthrough to original");

            if (*p_resolution == 1 || *p_resolution == 5) return 0x80046;
        } else {
            if (!SeenBefore(1, slot_index, (int)retRva, 0))
            PROBE_LOG(AlphaRing::DebugFlags::g_loadout,
                      "[Loadout] slot={} OUT OF RANGE -> passthrough | caller rva={:#x}",
                      slot_index, retRva);
        }

        int result = ((detour_t)entry.m_pOriginal)(slot_index);

        // 0x80046 = resolution_widescreen_half (res=3), 0x80047 =
        // resolution_widescreen_quarter (res=2), 0x80045 =
        // resolution_widescreen (everything else, and windows 4/5).
        if (!SeenBefore(2, slot_index, result, (int)retRva))
        PROBE_LOG(AlphaRing::DebugFlags::g_loadout,
                  "[Loadout] slot={} caller rva={:#x} -> original returned {:#x}",
                  slot_index, retRva, result);

        // Full-height Left/Right CUI layout (2P slots 0/1, 3P slot 0). These
        // slots keep res=3, which selects resolution_widescreen_half (0x80046),
        // the variant used by the stock wide, short Top/Bottom window. In a
        // full-height Left/Right half it left the loadout invisible;
        // resolution_widescreen_quarter (0x80047) renders the loadout,
        // scoreboard and per-player pause menu correctly (runtime-validated
        // 2026-09-17: 2P at 1920x1080 and 3840x1080, 3P P1 at 3840x1080). 3P
        // slots 1/2 are quadrants with res=2 and already get 0x80047 natively.
        // Only the returned variant changes - table res, Top/Bottom, 1P/4P and
        // windows 4/5 are untouched.
        if (result == 0x80046 && slot_index >= 0 && slot_index < 4) {
            auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
            int player_count = (int)GetSplitscreenPlayerCount();
            if (AlphaRing::SplitscreenConfigStore::UsesFullHeightLeftRightSlot(player_count, slot_index)) {
                if (!SeenBefore(3, slot_index, (int)retRva, player_count))
                PROBE_LOG(AlphaRing::DebugFlags::g_loadout,
                          "[Loadout] slot={} player_count={} caller rva={:#x} -> full-height Left/Right: 0x80046 -> 0x80047",
                          slot_index, player_count, retRva);
                return 0x80047;
            }
        }

        return result;
    }
}
