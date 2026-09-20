#include "haloreach.h"

#include "common.h"

#include "log/DebugFlags.h"

#include "global/Global.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"

#include <cstring>

namespace HaloReach::Entry::HudAnchor {
    // This function derives the per-player HUD "safe area" reference frame -
    // an (xInset, yInset, halfWidth, halfHeight) tuple - from a rect fit to a
    // target aspect (4:3 if resolution==1, else 16:9) inside the slot's raw
    // bounds. Confirmed (live logging against a working quadrant slot of the
    // same width): the X-axis fit is fine on its own; only Y is wrong - the
    // original's fit assumes a landscape/quadrant shape and has no path for a
    // taller-than-wide slot.
    //
    // SAFETY: do not add anything else to this hook without re-confirming
    // live. Forcing this function's shared basis-matrix computation down its
    // "identity" fallback path (zeroing its row-selector around the original
    // call) made everything stop rendering, most likely a race against the
    // render thread reading that same memory mid-frame.
    //
    // g_logBasis: read-only basis-matrix instrumentation, gated on
    // DebugFlags::g_hudFrame, strictly reads (the transient-mutation approach
    // above is what caused the render-thread race) and change-filtered (a hot
    // per-player-per-frame path; unfiltered logging has caused a watchdog
    // crash in this project before).
    constexpr bool g_logBasis = AlphaRing::DebugFlags::g_hudFrame;

    // Portrait Y override - currently OFF. The game's own frame for a
    // 960x1080 slot already spans y 73..1007 of 1080 (aspect 0.914, near
    // full-height); both overrides tried (16:9 band, full-height) were worse.
    // Also the decisive test for whether this hook is involved in HUD
    // distortion at all: some ultrawide reports show the same distortion with
    // landscape slots, where this portrait branch cannot fire.
    constexpr bool g_applyPortraitYOverride = false;

    // "Vertical HUD shape" (hud_target) frame-shortening experiment. Superseded
    // by the Left/Right canvas-height fit; see the use site.
    constexpr bool g_applyHudShapeTarget = false;

    // Centre the HUD content box in its slot.
    //
    // THE BUG: ComputeHudAnchorFrame emits a horizontally asymmetric frame for
    // a vertical split - measured mirrored insets, not centred ones (960x1080
    // slot, 854x934 content box: slot 0 xInset=101, slot 1 xInset=5, versus a
    // centred 53 for both; slot 1 also yInset=89 against slot 0's centred 73).
    //
    // ORTHOGONAL TO THE COMPRESSION, deliberately: xInset/yInset are pure
    // position offsets, halfW/halfH govern scale and are NOT touched here, so
    // this cannot affect the separate aspect-driven compression problem -
    // verifiable in the probe (CHUDVS elem 6/7 should report identical insets
    // for both slots) rather than only by eye. Applies to any slot shape, not
    // just portrait: the asymmetry is a property of the original's own
    // output.
    constexpr bool g_centreHudBox = true;

    void LogFrameAndBasis(__int64 hModule, const char* when) {
        if (!g_logBasis) return;

        short y0 = *(short*)(hModule + 0xD1F6D0);
        short x0 = *(short*)(hModule + 0xD1F6D2);
        short y1 = *(short*)(hModule + 0xD1F6D4);
        short x1 = *(short*)(hModule + 0xD1F6D6);

        int   res    = *(int*)(hModule + 0xD1F710);
        float szZ    = *(float*)(hModule + 0xD1F6F0);
        float szW    = *(float*)(hModule + 0xD1F6F4);
        float xInset = *(float*)(hModule + 0xD1F790);
        float halfW  = *(float*)(hModule + 0xD1F798);
        float yInset = *(float*)(hModule + 0xD1F794);
        float halfH  = *(float*)(hModule + 0xD1F79C);

        // The basis block is 5 float4 at D1F740, ending exactly where
        // chud_screen_scale_and_offset begins at D1F790.
        const float* b = (const float*)(hModule + 0xD1F740);

        // Change filter: remember whole distinct states (linear-scan, log only
        // a state never seen before, go quiet once full - bounded by
        // construction). Keying on a subset (tried: single-previous-sample;
        // (slot rect, res, call site) - the slot rect is SLOT-LOCAL, so both
        // slots collide on one key and ping-pong) thrashed to 164MB/7.9MB.
        // 48: the canvas-fit test adds an "lr_canvas" state per corrected slot
        // on top of each case's "original" states.
        constexpr int STATE_MAX = 48;
        struct State { float v[11]; };
        static State s_seen[STATE_MAX] = {};
        static int   s_count = 0;

        State now{};
        now.v[0] = (float)x0;  now.v[1] = (float)y0;  now.v[2] = (float)x1;
        now.v[3] = (float)y1;  now.v[4] = (float)res; now.v[5] = xInset;
        now.v[6] = halfW;      now.v[7] = yInset;     now.v[8] = halfH;
        now.v[9] = szZ;        now.v[10] = szW;

        for (int i = 0; i < s_count; ++i)
            if (memcmp(s_seen[i].v, now.v, sizeof(now.v)) == 0) return;

        if (s_count >= STATE_MAX) return;
        s_seen[s_count++] = now;

        int slotW = x1 - x0, slotH = y1 - y0;

        // The number the radar test turns on: a circle in normalised space
        // renders with this width/height ratio. 1.0 means round.
        float frameAspect = (halfH != 0.0f) ? (halfW / halfH) : 0.0f;
        float slotAspect  = (slotH  != 0)    ? ((float)slotW / (float)slotH) : 0.0f;

        // Canvas-to-pixel scale per axis, as the CHUD vertex shader applies it:
        // px = xInset + 2*halfW * x/refW', py = yInset + 2*halfH * y/refH.
        float sx = (szZ != 0.0f) ? (2.0f * halfW / szZ) : 0.0f;
        float sy = (szW != 0.0f) ? (2.0f * halfH / szW) : 0.0f;

        LOG_INFO("[HudFrame] {} | slot {}x{} at ({},{})-({},{}) aspect={:.4f} res={} | "
                 "xInset={:.2f} halfW={:.2f} yInset={:.2f} halfH={:.2f} | "
                 "frameAspect={:.4f} | screenZW=({:.2f},{:.2f}) | Sx={:.4f} Sy={:.4f}",
                 when, slotW, slotH, x0, y0, x1, y1, slotAspect, res,
                 xInset, halfW, yInset, halfH, frameAspect, szZ, szW, sx, sy);

        if (!AlphaRing::DebugFlags::g_hudBasis) return;

        for (int row = 0; row < 5; ++row)
            LOG_INFO("[HudBasis] {} | basis[{}] = ({: .6f}, {: .6f}, {: .6f}, {: .6f})",
                     when, row, b[row * 4 + 0], b[row * 4 + 1], b[row * 4 + 2], b[row * 4 + 3]);
    }

    HaloReachEntry(entry, OFFSET_HALOREACH_PF_HUD_ANCHOR, void, detour) {
        ((detour_t)entry.m_pOriginal)();

        __int64 hModule = entry.m_target - entry.m_offset;

        LogFrameAndBasis(hModule, "original");

        int resolution = *(int*)(hModule + 0xD1F710);

        // LEFT/RIGHT CANVAS-HEIGHT FIT (production, g_lrHudCanvasFit; validated
        // in game 2026-09-16: Sx == Sy logged, radar/weapon/notifications/medals
        // unstretched, nameplates stay on target, Top/Bottom and 3p quadrants
        // unchanged).
        //
        // Reach sizes and anchors widgets in reference-canvas units and the
        // shader maps canvas -> frame per axis, so a widget's pixel shape is
        // scaled by (halfW/halfH)/(refW'/refH). Reach keeps that at ~1 on
        // ultrawide screens by widening refW' (D1F6F0) while leaving the frame
        // alone. A full-height Left/Right slot needs the other axis: raise refH
        // (D1F6F4) until both axes use the same canvas-to-pixel scale. The frame
        // stays the whole slot, so edge anchors stay at the slot edges, and
        // every canvas consumer (shader, anchors, marker projection) reads the
        // value after this point.
        //
        // Same slots as the semantic-3 override in hud_layout_probe.cpp: 2p
        // Left/Right both slots, 3p Left/Right slot 0. Never 1p, Top/Bottom, 4p
        // or 3p quadrants.
        if (AlphaRing::DebugFlags::g_lrHudCanvasFit && resolution == 3) {
            typedef unsigned __int64 (*GetSplitscreenPlayerCount_t)();
            auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(
                    hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
            int playerCount = (int)GetSplitscreenPlayerCount();
            int slot = *(int*)(hModule + OFFSET_HALOREACH_DAT_CURRENT_HUD_PLAYER);

            if (AlphaRing::SplitscreenConfigStore::UsesFullHeightLeftRightSlot(playerCount, slot)) {
                float refW  = *(float*)(hModule + 0xD1F6F0);
                float halfW = *(float*)(hModule + 0xD1F798);
                float halfH = *(float*)(hModule + 0xD1F79C);

                if (refW > 1.0f && halfW > 1.0f && halfH > 1.0f) {
                    *(float*)(hModule + 0xD1F6F4) = refW * halfH / halfW;
                    LogFrameAndBasis(hModule, "lr_canvas");
                }
                return;
            }
        }

        if (resolution != 2) return;

        short y0 = *(short*)(hModule + 0xD1F6D0);
        short x0 = *(short*)(hModule + 0xD1F6D2);
        short y1 = *(short*)(hModule + 0xD1F6D4);
        short x1 = *(short*)(hModule + 0xD1F6D6);

        int width = x1 - x0;
        int height = y1 - y0;

        // HUD SHAPE TARGET - must run BEFORE centring, which derives insets
        // from halfW/halfH and would otherwise use a stale extent. Portrait
        // slots only (target 1: halfH = halfW/3.42857 matches stock radar
        // aspect; target 2: halfH = halfW/2.71887 gives a true circle).
        //
        // SUPERSEDED (2026-09-16) by the canvas-height fit above, which keeps
        // proportions without shortening the frame. Left/Right slots now run
        // semantic 3, so no supported layout reaches this portrait semantic-2
        // path. Disabled rather than deleted; the saved hud_target value is
        // ignored.
        if (g_applyHudShapeTarget) {
            int target = AlphaRing::Global::Global()->splitscreen_hud_target;
            if (target != 0 && height > width) {
                float halfW = *(float*)(hModule + 0xD1F798);
                if (halfW > 1.0f) {
                    constexpr float T_MATCH_STOCK = 768.0f / 224.0f;   // 3.42857
                    constexpr float T_ROUND       = 1.0f / 0.3678f;    // 2.71887
                    float t = (target == 2) ? T_ROUND : T_MATCH_STOCK;
                    float halfH = halfW / t;
                    if (halfH > 1.0f && halfH * 2.0f <= (float)height)
                        *(float*)(hModule + 0xD1F79C) = halfH;
                }
            }
        }

        if (g_centreHudBox) {
            float halfW = *(float*)(hModule + 0xD1F798);
            float halfH = *(float*)(hModule + 0xD1F79C);

            // Leave the original alone rather than write a degenerate frame.
            if (halfW > 1.0f && halfH > 1.0f &&
                halfW * 2.0f <= (float)width && halfH * 2.0f <= (float)height) {
                *(float*)(hModule + 0xD1F790) = ((float)width  - halfW * 2.0f) * 0.5f;
                *(float*)(hModule + 0xD1F794) = ((float)height - halfH * 2.0f) * 0.5f;
            }
        }

        if (height <= width) return;

        // Disabled (superseded by the canvas-height fit above). Would span the
        // slot's FULL height instead of a centred 16:9 band - X left alone
        // since the original's width-constrained 16:9 fit already spans the
        // full slot width. KNOWN INCOMPLETE when this was live: halfH both
        // positions AND scales widgets (shader constant
        // chud_screen_scale_and_offset), so this one number can't satisfy both
        // - measured halfH=540 against halfW=427 (0.791) against an on-screen
        // radar of 0.786, correctly placed but vertically stretched. A proper
        // fix needs a second lever (the canvas-height fit is that lever).
        if (!g_applyPortraitYOverride) return;

        *(float*)(hModule + 0xD1F794) = 0.0f;              // yInset
        *(float*)(hModule + 0xD1F79C) = height * 0.5f;     // halfHeight

        LogFrameAndBasis(hModule, "override");
    }
}
