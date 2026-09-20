#include "haloreach.h"

#include "common.h"

#include <intrin.h>

#include "log/DebugFlags.h"

namespace HaloReach::Entry::CuiCanvasScale {
    typedef unsigned __int64 (*GetSplitscreenPlayerCount_t)();

    // Ultrawide CUI canvas fit for split-screen.
    //
    // Reach draws every per-player CUI screen - scoreboard, loadout, the
    // per-player pause menu - with the transform
    //
    //     cameraMatrix (slot-derived)  x  Scale(backbufferW/1152, backbufferH/720)
    //
    // built by FUN_1802f1890 (0x2F1890) and uploaded as shader constant
    // 0x310004 by FUN_1802f5310 (0x2F5310).
    //
    // The camera term is correct. Its projection scale is 2*near/width and
    // 2*near/height, taken from the render-view stack's +0x38 rectangle, which
    // is the slot rectangle - the same four shorts the D3D viewport is set from
    // (DAT_180c9fae0 + 0x38 == DAT_180c9fb18, written per slot by FUN_18026c204
    // and handed to the viewport setter by the view-stack push callback).
    //
    // The Scale term is not. It is derived from the whole backbuffer and is a
    // single global with no slot and no player-count term. Because the
    // projection's 2*near/width cancels against the viewport width, a CUI
    // element authored at (u, v) canvas units lands at
    //
    //     pixel_x = u * backbufferW / 1152      pixel_y = v * backbufferH / 720
    //
    // regardless of which window is drawing. The ratio scaleX/scaleY is
    // (W/H) * (720/1152), a constant 1.1111 for ANY 16:9 backbuffer - that
    // constant is the canvas's own 16:10 -> 16:9 mapping and is the shape the
    // tags are authored against, which is why 16:9 renders correctly at every
    // resolution. At 3840x1080 the ratio is 2.2222: exactly 2x too wide.
    //
    // This corrects it by substituting the 16:9-equivalent backbuffer width,
    // leaving the real height alone:
    //
    //     effectiveWidth = backbufferHeight * 16/9
    //     scaleX         = effectiveWidth / 1152
    //     scaleY         = backbufferHeight / 720      (unchanged)
    //
    // Deliberately NOT the slot width: slotW/1152 gives 0.833 at 1920x1080
    // against the correct 1.667 and would break every 16:9 case.
    //
    // Runtime-validated at 1920x1080/3840x1080 across 2P-4P, Left/Right and
    // native quadrants - see REVERSE_ENGINEERING.md "CUI canvas scale". The 4P
    // result matters beyond 4P: it validates the correction for a native
    // split-screen layout, which is why this gates on split-screen rather than
    // Left/Right specifically.
    //
    // Out of scope, deliberately: 1P fullscreen (same mechanism, but stock
    // Reach behaviour on every install, not a split-screen regression); windows
    // 4/5, excluded by the call-site gate; the separate backbuffer-aspect term
    // at 0x2F234A (scratch-target width, (W/H)/(16:9)) - no runtime symptom
    // attributed to it; the respawn/identity strip's vertical placement -
    // scaleY is unchanged, so this cannot affect it.

    // Reach's canvas reference space. 1152.0f is a float constant at 0xA8B560.
    // 720 is a *16-bit* value (0x02D0) at 0xA35C42, read with `MOVSX ... word
    // ptr`; Ghidra's decompile renders that as a 32-bit load, which is wrong -
    // read as 32 bits the value is 41943760 and the UI would collapse.
    constexpr float kCanvasRefWidth = 1152.0f;
    constexpr float kCanvasRefHeight = 720.0f;
    constexpr float kSixteenByNine = 16.0f / 9.0f;

    // Backbuffer dimensions, published by the graphics startup/resize paths.
    // Read here exactly as the native code reads them - signed 16-bit.
    constexpr __int64 kRvaBackbufferWidth = 0xB43A90;
    constexpr __int64 kRvaBackbufferHeight = 0xB43A94;

    // The shared CUI canvas scale (two floats) and the cache keys that gate its
    // recomputation. FUN_1802f1890 (0x2F18AC), the inlined copy in
    // FUN_1802f1a2c (0x2F1B8C) and the one in FUN_1802f2e74 (0x2F2F47) all use
    // the same guard: recompute only when (backbufferW, backbufferH) differ
    // from these keys. Seeding the keys with the live dimensions makes every
    // one of those sites take the "unchanged" branch and honour our values.
    constexpr __int64 kRvaCanvasScaleX = 0xB4BBD8;
    constexpr __int64 kRvaCanvasScaleY = 0xB4BBDC;
    constexpr __int64 kRvaCanvasCacheW = 0x4E38C8C;
    constexpr __int64 kRvaCanvasCacheH = 0x4E38C94;

    // 0x2AAB24 is reached from four call sites; only one is the per-player
    // window draw:
    //
    //   0x26CEE4  UpdatePlayerFrame   CALL rel32 -> return address 0x26CEE9  <- this one
    //   0x26FCE5  FUN_18026fca4       windows 4 and 5 (fullscreen)
    //   0x26FACD  FUN_18026fa88       loops windows 4..5 only
    //   0x1D3D1D  FUN_1801d3894
    //
    // Gating on the return address keeps the correction strictly inside the
    // per-player draw, so windows 4/5 and the other readers of the same global
    // (DrawLetterboxAndNotificationBars, DrawHintTextOverlay, FUN_1802d2018,
    // FUN_1802d5884, FUN_180305b44, FUN_1802dd4b0) are untouched.
    constexpr __int64 kPerPlayerCallSiteReturnRva = 0x26CEE9;

    // Restores every byte of modified global state, including the cache keys,
    // so that after the draw the engine is bit-identical to what it would have
    // been. Restoring the keys as well as the scales matters: leaving the keys
    // seeded while putting the old scales back would let a later reader use a
    // stale scale without recomputing it. The native globals are legitimately
    // stale with respect to the live backbuffer for one frame after a
    // resolution change, and restoring them verbatim preserves that.
    struct CanvasScaleOverride {
        float* scaleX = nullptr;
        float* scaleY = nullptr;
        int* cacheW = nullptr;
        int* cacheH = nullptr;
        float savedScaleX = 0.0f;
        float savedScaleY = 0.0f;
        int savedCacheW = 0;
        int savedCacheH = 0;

        void Apply(__int64 hModule, int backbufferWidth, int backbufferHeight,
                   float effectiveWidth) {
            scaleX = (float*)(hModule + kRvaCanvasScaleX);
            scaleY = (float*)(hModule + kRvaCanvasScaleY);
            cacheW = (int*)(hModule + kRvaCanvasCacheW);
            cacheH = (int*)(hModule + kRvaCanvasCacheH);

            savedScaleX = *scaleX;
            savedScaleY = *scaleY;
            savedCacheW = *cacheW;
            savedCacheH = *cacheH;

            *scaleX = effectiveWidth / kCanvasRefWidth;
            *scaleY = (float)backbufferHeight / kCanvasRefHeight;

            // The native guard compares against the sign-extended 16-bit reads,
            // so seed the keys with the same values the caller derived.
            *cacheW = backbufferWidth;
            *cacheH = backbufferHeight;
        }

        ~CanvasScaleOverride() {
            if (scaleX == nullptr) return;
            *scaleX = savedScaleX;
            *scaleY = savedScaleY;
            *cacheW = savedCacheW;
            *cacheH = savedCacheH;
        }
    };

    // Re-entry guard. Only the outermost per-player invocation owns the
    // save/restore; a nested 0x2AAB24 must not overwrite the saved values.
    // thread_local so a draw on another thread cannot disturb the depth.
    static thread_local int g_depth = 0;

    HaloReachEntry(entry, OFFSET_HALOREACH_PF_CUI_WINDOW_DRAW, void, detour,
                   void* param_1, void* param_2, unsigned int window, void* param_4,
                   unsigned int param_5, unsigned char param_6) {
        typedef void (*cui_window_draw_t)(void*, void*, unsigned int, void*, unsigned int,
                                          unsigned char);

        __int64 hModule = entry.m_target - entry.m_offset;

        CanvasScaleOverride canvasFit;

        if (AlphaRing::DebugFlags::g_cuiUltrawideCanvasFit && g_depth == 0 &&
            (__int64)_ReturnAddress() - hModule == kPerPlayerCallSiteReturnRva && window < 4) {
            auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
            int player_count = (int)GetSplitscreenPlayerCount();

            // Split-screen player windows only. The canvas scale has no layout
            // term, so this is keyed on there being more than one local player
            // rather than on Left/Right - 4P native quadrants need it too, and
            // were validated. 1P keeps stock behaviour; windows 4/5 are already
            // excluded by the call-site gate and again by window < player_count.
            if (player_count >= 2 && window < (unsigned int)player_count) {
                int backbufferWidth = *(short*)(hModule + kRvaBackbufferWidth);
                int backbufferHeight = *(short*)(hModule + kRvaBackbufferHeight);

                if (backbufferWidth > 0 && backbufferHeight > 0) {
                    const float sixteenByNineWidth = (float)backbufferHeight * kSixteenByNine;

                    // Only ever NARROW the canvas, never widen it. A backbuffer
                    // at or below 16:9 - 16:10, 4:3, 5:4, a tall window - is
                    // already correct, and substituting a 16:9-equivalent width
                    // there would make CUI wider than stock rather than fixing
                    // anything. Every validated case is a no-op under this
                    // comparison at 16:9 and a reduction above it.
                    if ((float)backbufferWidth > sixteenByNineWidth)
                        canvasFit.Apply(hModule, backbufferWidth, backbufferHeight,
                                        sixteenByNineWidth);
                }
            }
        }

        ++g_depth;
        ((cui_window_draw_t)entry.m_pOriginal)(param_1, param_2, window, param_4, param_5, param_6);
        --g_depth;
    }
}
