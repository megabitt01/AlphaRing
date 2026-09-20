#pragma once

// Verbose probe logging, compiled in but OFF.
//
// These probes exist to be turned back on during investigation, so the code
// stays in place - only the emission is gated. Flip g_probeMaster to true to
// re-enable everything, or flip an individual flag for one subsystem.
//
// They are off by default because they are genuinely dangerous on a hot path:
// the HUD frame/basis probe produced a 164MB log in one short run before its
// change filter was fixed, and an unfiltered per-frame hook earlier in this
// project caused a watchdog crash from sustained disk I/O.
namespace AlphaRing::DebugFlags {
    inline constexpr bool g_probeMaster = false;

    inline constexpr bool g_chudUpload  = false;  // [ChudConst] - broad constant/census logging
    inline constexpr bool g_chudCensus  = g_probeMaster;           // [ChudCensus] uploads-per-frame per cbuffer element
    inline constexpr bool g_hudFrame   = g_probeMaster;            // [HudFrame] only (incl. Sx/Sy, lr_canvas)
    inline constexpr bool g_hudBasis   = g_probeMaster;   // [HudBasis] - inert, no slot dependence
    // [SplitRT] one line per variant-3 render-target allocation. The line also
    // carries the descriptor's flags word and its four size/divisor floats
    // (splitscreen_rt.cpp), which is what makes the pre-halved dimension
    // recoverable - Reach overwrites it before the detour is reached, and the
    // descriptor table is zero in the DLL's file image, so it cannot be read
    // statically.
    //
    // Safe to run unfiltered despite having no change filter: RT_CREATE
    // (0x2669E8) has exactly ONE code caller, FUN_1802663B8, which is called
    // only from RT_POOL_INIT (0x266F90). It is not a per-frame path - lines are
    // emitted only at engine start, on resize, and on a live layout rebuild,
    // ~34 per pool build for the variant-3 family.
    inline constexpr bool g_splitRt    = g_probeMaster;   // [SplitRT] per-allocation lines
    inline constexpr bool g_loadout    = g_probeMaster;   // [Loadout] lookup slot/player_count/resolution

    // DIAGNOSTIC PROBE, bounded, off. [Throttle] - Reach's split-screen
    // render-quality throttle (FUN_180270258, one call per frame from
    // UpdateAllPlayerViews at 0xC358E): one 0x50-byte detail record selected
    // purely by GetSplitscreenPlayerCount(), copied to the live block at
    // 0xCA0240, then transformed by MCC's quality tier (0x3AEF8).
    //
    // What it established (2026-09-18, and why it is kept rather than deleted):
    // the records come from tag data, not the static table in the DLL
    // (use_static is 0 and the static records never matched); at 2+ local
    // players they are ZERO for water, decorators, decals, lights and shadows;
    // and the MCC tier composes on top rather than replacing them. It also
    // proved the "Splitscreen Render Quality (force 1P tier)" patch works, by
    // logging the same live block at 2P/3P/4P as at forced 1P.
    //
    // Per distinct (player count, whole-block hash, MCC setting bytes) it logs
    // the live block, tag[count], tag[1P] raw and tag[1P] after the tier pass,
    // capped at 16 distinct states. Read-only: it never writes engine memory and
    // changes no behavior. Turn on to re-measure - the same structure exists in
    // halo3/halo3odst/halo4/groundhog, and a forced-2P tier is still open.
    inline constexpr bool g_renderThrottle = false;

    // PRODUCTION FEATURE GATE, not a probe. Per-player split-screen FOV
    // (2-4 local players) through Reach's native FOV baseline getter
    // FUN_1800c8554 (fov_baseline.cpp): a slot's Alpha Ring value when its
    // override is on, otherwise Reach's native result. 1P and VehicleFOV
    // stay native; nothing downstream (cameraInput+0x6c, projection) is
    // written. Replaces the retired render-side override, which regressed
    // zoom, the degree mapping and nameplate projection. 2P validated in
    // game 2026-09-17, then 1P-4P with the native-off policy.
    inline constexpr bool g_splitscreenFovBaseline = true;

    // SUPPORT LOG, bounded. [SplitFov] one line per split-screen slot (0-3)
    // whenever that slot's FOV source changes (alpha_ring / native), capped
    // at 16 lines per slot per process. Slider drags do not log.
    inline constexpr bool g_splitFovSourceLog = true;

    // PRODUCTION BEHAVIOR GATE, not a probe. Validated in game 2026-09-16
    // (2P and 3P, both switch directions, no Alt+Tab). A live TwoPlayerLayout
    // change rebuilds Reach's render-target pool on the next
    // UpdateAllPlayerViews through Reach's own release/init pair
    // (RT_POOL_RELEASE 0x2670FC, RT_POOL_INIT 0x266F90) - the same pair its
    // resize path runs - so the shared res=3 surface is re-sized for the new
    // layout instead of being reused until the next match load. Emits one
    // bounded [SplitRtRebuild] line per rebuild. Set false only to reproduce
    // the stale-surface behavior.
    inline constexpr bool g_splitRtLiveRebuild = true;

    // PRODUCTION BEHAVIOR GATE, not a probe. Validated in game 2026-09-16.
    // For full-height Left/Right HUD slots only (the slots that get the
    // semantic-3 correction), hud_anchor.cpp rewrites the live HUD reference
    // height D1F6F4 to refW' * halfH / halfW so canvas-to-pixel scale is equal
    // on both axes (removes the vertical HUD stretch). Frame and refW' are left
    // untouched. Set false only to reproduce the uncorrected HUD.
    inline constexpr bool g_lrHudCanvasFit = true;

    // PRODUCTION BEHAVIOR GATE, not a probe. Validated in game 2026-09-17 at
    // 1920x1080 and 3840x1080, in 2P/3P Left/Right and 4P quadrants.
    //
    // Reach derives its CUI canvas scale from the whole backbuffer
    // (backbufferW/1152, backbufferH/720) with no slot or player-count term,
    // so on a backbuffer wider than 16:9 every per-player CUI screen -
    // scoreboard, loadout, per-player pause - is horizontally over-scaled by
    // (W/H)/(16:9): exactly 2x at 3840x1080. For split-screen player windows
    // only, and only inside the per-player CUI window draw
    // (cui_canvas_scale.cpp, 0x2AAB24 from 0x26CEE4), the horizontal scale is
    // recomputed from the 16:9-equivalent width:
    //     scaleX = (backbufferHeight * 16/9) / 1152
    //     scaleY = backbufferHeight / 720          (unchanged)
    // Applied only when the backbuffer is wider than 16:9, so it is a no-op at
    // 16:9 and never widens a narrower one. All modified globals - both scales
    // and both recompute cache keys - are restored before the draw returns.
    //
    // 1P, windows 4/5 and the HUD/CHUD path are untouched. Set false to
    // reproduce the uncorrected ultrawide CUI.
    inline constexpr bool g_cuiUltrawideCanvasFit = true;
}

// Gated log. Compiles to nothing observable when the flag is false, but keeps
// the call site and its arguments under compilation so the probes cannot rot.
#define PROBE_LOG(flag, ...) do { if (flag) LOG_INFO(__VA_ARGS__); } while (0)
