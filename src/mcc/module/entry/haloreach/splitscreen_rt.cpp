#include "haloreach.h"

#include "common.h"

#include "log/DebugFlags.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"

#include <atomic>

namespace {
    // Layout generation the render-target pool was last built against. Stamped
    // by every RT_CREATE call, so a pool built at match load already counts as
    // current and only a later live layout change reads as stale.
    std::atomic<unsigned> g_builtLayoutGeneration{0};
}

namespace HaloReach::Entry::SplitscreenRt {
    unsigned BuiltLayoutGeneration() {
        return g_builtLayoutGeneration.load(std::memory_order_acquire);
    }

    void MarkLayoutGenerationBuilt(unsigned generation) {
        g_builtLayoutGeneration.store(generation, std::memory_order_release);
    }

    // Makes the splitscreen render-target sizing config-aware.
    //
    // THE BUG: Reach sizes its splitscreen render targets from a literal baked
    // into .rdata (_DAT_180a8ae58 = 0.805208325, DAT_180a8ad74 = 0.5 in
    // FUN_1802663b8's uVar6==3 branch) that duplicates but never reads
    // c_splitscreen_config::m_config_table. With a custom slot shape, camera/
    // viewport/projection/scissor all follow the live config while the surface
    // rendered into stays the stock 1546x540 shape, cropping content.
    //
    // Detouring FUN_1802669e8 (not FUN_1802663b8, which computes into locals a
    // detour can't reach, and not a constant patch on _DAT_180a8ae58/
    // DAT_180a8ad74, which would just bake in one different layout and can't
    // touch DAT_180a8ad74 at all - it's shared with FUN_1802884bc/
    // FUN_18028af8c) lets both axes follow the live table for any rect.
    // FUN_1802669e8 receives the computed pair by writable pointer
    // (out, &computed, originalDescriptor, subIndex, flag), and both the
    // descriptor index and sub-index are recoverable from it.
    //
    // SCOPE: these targets are shared between players, so this picks ONE size
    // per shared surface - the max across every slot bound to the variant, so
    // an asymmetric config isn't undersized. Blocks 2/3 specifically: the
    // stock constant encodes the 2-player shape, which 3-player slot 0 reuses;
    // 4p sizing is untouched. Normalisation is PER-VARIANT - see the branch
    // table at the scale computation below.

    constexpr __int64 CONFIG_TABLE = 0xB43C40;   // c_splitscreen_config::m_config_table
    constexpr int     CONFIG_ENTRY = 20;
    constexpr __int64 DESC_TABLE   = 0xBB9230;
    constexpr int     DESC_STRIDE  = 0x58;
    constexpr __int64 SCREEN_W     = 0xB43A90;
    constexpr __int64 SCREEN_H     = 0xB43A94;
    constexpr __int64 STOCK_FRAC_W = 0xA8AE58;   // _DAT_180a8ae58 = 0.805208325
    constexpr __int64 STOCK_FRAC_H = 0xA8AD74;   // DAT_180a8ad74  = 0.5 (shared - read only)

    struct SplitscreenViewConfig { float x0, y0, x1, y1; int resolution; };

    HaloReachEntry(entry, OFFSET_HALOREACH_PF_RT_CREATE, void*, detour,
                   void* param_1, char* param_2, void* param_3, int subIndex, int param_5) {
        typedef void* (*detour_t)(void*, char*, void*, int, int);

        __int64 hModule = entry.m_target - entry.m_offset;

        g_builtLayoutGeneration.store(AlphaRing::SplitscreenConfigStore::GetLayoutGeneration(),
                                      std::memory_order_release);

        if (param_2 == nullptr)
            return ((detour_t)entry.m_pOriginal)(param_1, param_2, param_3, subIndex, param_5);

        // The sub-target index IS the resolution variant. UpdateAllPlayerViews
        // binds one of four per pool entry with `iVar8 * 4 + DAT_184e38a08`,
        // where DAT_184e38a08 is the res published from m_config_table. The
        // measured sizes confirm the mapping on all four:
        //
        //   res 0 -> 1920x1080  single player, full screen
        //   res 1 -> 1920x540   2p no bars, full width, half height
        //   res 2 ->  960x540   quadrant
        //   res 3 -> 1546x540   2p pillarboxed (the hardcoded 0.805208325 case)
        //
        // So this must follow the LIVE res rather than hardcoding 3. A config
        // using res=1 binds sub-target 1, and keying on 3 would resize a surface
        // nothing ever binds while leaving the bound one stock - silently doing
        // nothing. Read the res from the same table the game publishes it from.
        int index0 = 2 * 4 + 0;
        auto cfg0 = (SplitscreenViewConfig*)(hModule + CONFIG_TABLE + (__int64)index0 * CONFIG_ENTRY);
        int configRes = cfg0->resolution;

        // res 5 exists and falls back to row 0 in the content table; it is not a
        // valid sub-target index here (5 & 3 would alias onto the no-bars
        // variant). Only touch a variant the indexing can actually select.
        if (configRes < 0 || configRes > 3)
            return ((detour_t)entry.m_pOriginal)(param_1, param_2, param_3, subIndex, param_5);

        if ((subIndex & 3) != configRes)
            return ((detour_t)entry.m_pOriginal)(param_1, param_2, param_3, subIndex, param_5);

        int oldW1 = *(int*)(param_2 + 0x04), oldH1 = *(int*)(param_2 + 0x08);
        int oldW2 = *(int*)(param_2 + 0x14), oldH2 = *(int*)(param_2 + 0x18);

        // Largest slot bound to this variant across the 2- and 3-player blocks,
        // straight from the live table. 3p player 1 shares the variant with 2p
        // and, under either layout preference, has the same extent as the 2p
        // slots; the res filter keeps 3p's res=2 quadrants out of the max.
        constexpr int SHARED_ENTRIES[] = { 2 * 4 + 0, 2 * 4 + 1, 3 * 4 + 0, 3 * 4 + 1, 3 * 4 + 2 };
        float maxW = 0.0f, maxH = 0.0f;
        for (int index : SHARED_ENTRIES) {
            auto cfg = (SplitscreenViewConfig*)(hModule + CONFIG_TABLE + (__int64)index * CONFIG_ENTRY);
            if (cfg->resolution != configRes) continue;
            float w = cfg->x1 - cfg->x0;
            float h = cfg->y1 - cfg->y0;
            if (w > maxW) maxW = w;
            if (h > maxH) maxH = h;
        }

        int screenW = *(int*)(hModule + SCREEN_W);
        int screenH = *(int*)(hModule + SCREEN_H);

        // A zero/absurd table (reset mid-load, or read before restore) must not
        // produce a degenerate surface - fall through untouched and let the log
        // show it happened rather than silently allocating something broken.
        bool sane = (maxW > 0.01f && maxW <= 1.0f && maxH > 0.01f && maxH <= 1.0f &&
                     screenW > 0 && screenH > 0);

        if (!sane) {
            LOG_INFO("[SplitRT] sub={} SKIPPED - implausible config (fracW={:.6f} fracH={:.6f} "
                     "screen={}x{}), leaving {}x{}",
                     subIndex, maxW, maxH, screenW, screenH, oldW1, oldH1);
            return ((detour_t)entry.m_pOriginal)(param_1, param_2, param_3, subIndex, param_5);
        }

        // SCALE, don't assign. Sub-index-3 is requested at several resolutions
        // - the full surface AND its downsample pyramid (773x270, 386x135,
        // 231x90, ...). Assigning the full size to every one of them inflates
        // ~33 deliberately-small buffers by ~100MB and flattens the pyramid
        // ratios bloom/exposure rely on. Instead scale by the ratio between the
        // config's shape and the shape the game would have produced:
        //
        //     scaleX = configFracW / stockFracW
        //     scaleY = configFracH / stockFracH
        //
        // A stock config gives (1.0, 1.0) - an exact no-op.
        //
        // The stock pair is PER-VARIANT - read out of FUN_1802663b8's uVar6
        // branch (== subIndex & 3), gated on descriptor flag 0x20:
        //
        //   uVar6 == 0: no branch, dimensions unchanged         -> (1.0,  1.0)
        //   uVar6 == 1: iVar12/=2; iVar11/=2 (HEIGHT only)       -> (1.0,  0.5)
        //   uVar6 == 2: integer halve of both axes               -> (0.5,  0.5)
        //   uVar6 == 3: *_DAT_180a8ae58, *DAT_180a8ad74          -> (0.805, 0.5)
        //
        // _DAT_180a8ae58/DAT_180a8ad74 are read only by the uVar6==3 branch -
        // variant 3's pair does NOT describe variants 1/2, which halve by
        // integer division and have no module constant to read. An earlier
        // version normalised by variant 3's pair unconditionally; that's wrong
        // for every other res and was reachable (Dev Tools "Remove Black Bar"
        // writes entries 8/9 as res=1), inflating an already-correct 1920x540
        // surface to 2384x540 - a geometry error, since FUN_180274854 derives
        // the D3D viewport from the bound render target, not just wasted VRAM.
        float stockFracW, stockFracH;
        switch (configRes) {
            case 0:  stockFracW = 1.0f; stockFracH = 1.0f; break;
            case 1:  stockFracW = 1.0f; stockFracH = 0.5f; break;
            case 2:  stockFracW = 0.5f; stockFracH = 0.5f; break;
            default: stockFracW = *(float*)(hModule + STOCK_FRAC_W);
                     stockFracH = *(float*)(hModule + STOCK_FRAC_H); break;
        }

        if (!(stockFracW > 0.0001f) || !(stockFracH > 0.0001f)) {
            LOG_INFO("[SplitRT] sub={} SKIPPED - implausible stock factors ({:.7f},{:.7f})",
                     subIndex, stockFracW, stockFracH);
            return ((detour_t)entry.m_pOriginal)(param_1, param_2, param_3, subIndex, param_5);
        }

        float scaleX = maxW / stockFracW;
        float scaleY = maxH / stockFracH;

        auto scaled = [](int v, float s) {
            int r = (int)((float)v * s + 0.5f);
            return r < 1 ? 1 : r;   // never produce a zero-dimension surface
        };

        // TRUNCATION FIX: derive the TOP-LEVEL target directly instead of
        // scaling a value the game has already truncated. FUN_1802663b8
        // computes the base with an int cast, so the fractional part is lost
        // BEFORE this detour sees it, and scaleX then amplifies whatever was
        // lost - was misidentified as the cause of a reported lighting fault
        // (see docs/REVERSE_ENGINEERING.md, "Render-target sizing": arithmetic
        // only, no run has reproduced a visible fault from it; the actual fix
        // is exactW/exactH below). Only the top-level target can be derived
        // exactly - pyramid levels arrive already divided by their own
        // divisors and their pre-truncation values can't be reconstructed, so
        // those keep the ratio via the per-variant stock pair below.
        //
        // The top-level base is computed the way the binary computes it, per
        // variant, so the comparison is exact rather than approximate:
        //   res 0: (screenW, screenH)                 no branch
        //   res 1: (screenW, screenH/2)               integer halve of height
        //   res 2: (screenW/2, screenH/2)             integer halve of both
        //   res 3: ((int)(screenW*0.805208f), (int)(screenH*0.5f))
        int topW, topH;
        switch (configRes) {
            case 0:  topW = screenW;     topH = screenH;     break;
            case 1:  topW = screenW;     topH = screenH / 2; break;
            case 2:  topW = screenW / 2; topH = screenH / 2; break;
            default: topW = (int)((float)screenW * stockFracW);
                     topH = (int)((float)screenH * stockFracH); break;
        }

        // Target for the slot, derived once from the config fraction and
        // TRUNCATED, exactly as the binary truncates.
        //
        // INVARIANT: a stock table must be a byte-exact no-op. maxW/maxH are
        // bitwise identical to the per-variant stock pair (0x3F4E2222 for
        // variant 3's width), so truncating here reduces to the binary's own
        // expression on the same inputs and yields the same integer BY
        // CONSTRUCTION, at every resolution, with no gate to get wrong.
        //
        // Do not reintroduce rounding. It broke that invariant wherever the
        // product's fraction reached 0.5 - at 3440 the binary computes 2769 and
        // rounding gave 2770 - and regressed stock Top/Bottom. Truncating also
        // matches the binary on odd dimensions, where it halves with C integer
        // division (res 1/2 above).
        //
        // Deriving from the SCREEN dimension is still deliberate and must stay:
        // it is what avoids amplified truncation for a custom slot shape. 2P
        // Left/Right at 3440 gives 1720 here, versus the 1719 that rescaling an
        // already-truncated base produces.
        //
        // Bisect, runtime validation and the local repro widths are in
        // docs/REVERSE_ENGINEERING.md, "Render-target sizing".
        int exactW = (int)((float)screenW * maxW);
        int exactH = (int)((float)screenH * maxH);

        auto resolve = [&](int w, int h, int& outW, int& outH) {
            if (w == topW && h == topH) { outW = exactW; outH = exactH; }
            else                        { outW = scaled(w, scaleX); outH = scaled(h, scaleY); }
        };

        int newW1, newH1, newW2, newH2;
        resolve(oldW1, oldH1, newW1, newH1);
        resolve(oldW2, oldH2, newW2, newH2);

        int descIndex = (int)(((__int64)param_3 - (hModule + DESC_TABLE)) / DESC_STRIDE);

        *(int*)(param_2 + 0x04) = newW1;
        *(int*)(param_2 + 0x08) = newH1;
        *(int*)(param_2 + 0x14) = newW2;
        *(int*)(param_2 + 0x18) = newH2;

        int newW = newW1, newH = newH1;

        // READ-ONLY diagnostic fields. param_3 is the original descriptor -
        // the same 0x58-byte block FUN_1802663B8 copies (11 qwords, exactly
        // DESC_STRIDE) and reads its sizing terms from.
        //
        // These are the ONLY way to recover the pre-halved dimension. The
        // variant-3 branch computes h2 = trunc(h1 * 0.5) and then overwrites
        // h1 := h2, so by the time this detour runs h1's parity is gone and a
        // logged old height of 67 is consistent with both 134 and 135. That
        // parity is exactly what distinguishes a correct level from a
        // one-pixel-short one. The descriptor table is zero in the DLL's file
        // image (RVA 0xBB9230, .data, populated at startup), so it cannot be
        // dumped statically either.
        //
        // Offsets read straight out of the decompile:
        //   +0x10 flags  bit 0 set => the four floats are DIVISORS of the
        //                screen dimension; clear => absolute sizes
        //   +0x18 w1   +0x1C h1   +0x30 w2   +0x34 h2
        //
        // With bit 0 set the game computes each term as
        // floorf(screenDim / f + 0.5f) - round-half-up, in float32 - BEFORE
        // the per-variant branch, so h1 = floorf(screenH / div_h1 + 0.5f).
        // Reproduce that expression exactly when analysing; a generic round()
        // disagrees at the half-way boundary.
        //
        // Nothing here is written back. Both lambdas are referenced inside
        // PROBE_LOG's dead-but-compiled branch, so they cost nothing and do
        // not warn when the flag is off.
        auto descF     = [&](int off) -> float    { return param_3 ? *(float*)((const char*)param_3 + off) : -1.0f; };
        auto descFlags = [&]()        -> unsigned { return param_3 ? *(unsigned*)((const char*)param_3 + 0x10) : 0u; };

        PROBE_LOG(AlphaRing::DebugFlags::g_splitRt,
                  "[SplitRT] desc={} sub={} res={} screen={}x{} flags=0x{:08X} "
                 "div=({:.4f},{:.4f},{:.4f},{:.4f}) | frac=({:.6f},{:.6f}) stock=({:.6f},{:.6f}) "
                 "scale=({:.4f},{:.4f}) | pair1 {}x{}->{}x{} pair2 {}x{}->{}x{} {}",
                 descIndex, subIndex, configRes, screenW, screenH, descFlags(),
                 descF(0x18), descF(0x1C), descF(0x30), descF(0x34),
                 maxW, maxH, stockFracW, stockFracH,
                 scaleX, scaleY,
                 oldW1, oldH1, newW1, newH1, oldW2, oldH2, newW2, newH2,
                 (newW1 == oldW1 && newH1 == oldH1) ? "(unchanged)" : "(RESIZED)");

        return ((detour_t)entry.m_pOriginal)(param_1, param_2, param_3, subIndex, param_5);
    }
}
