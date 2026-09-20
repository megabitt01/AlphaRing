#include "haloreach.h"

#include "common.h"

#include "log/DebugFlags.h"
#include "global/Global.h"

#include <intrin.h>

namespace HaloReach::Entry::FovBaseline {
    // Per-player split-screen FOV through Reach's own FOV baseline.
    // Policy: 1P always native. In split-screen (2-4 local players) a slot
    // whose Alpha Ring override is ON gets that value; OFF gets Reach's
    // native split-screen result (the original getter), unchanged.
    //
    // FUN_1800c8554(useProfile) returns the horizontal FOV (radians, at the
    // fixed 4:3 reference) that every camera mode and the observer feed into
    // weapon zoom, spring interpolation and screen effects, ending in the
    // per-slot cameraInput+0x40/+0x6C written once per game tick. Natively it
    // reads MCC's FOVSetting only while fewer than 2 local players exist, and
    // only for local user 0, so split-screen gets the 78deg global default.
    // Answering per slot here keeps every downstream consumer (world camera,
    // marker projection, zoom) on one consistent value - nothing downstream is
    // written. See docs/REVERSE_ENGINEERING.md, "Halo Reach split-screen FOV".
    //
    // The getter has no slot parameter, so the two per-slot loops that reach
    // it provide a thread-local slot context:
    //   director update FUN_1800c4458: per slot FUN_1800c5040(slot), then the
    //       director's update, which runs the camera modes
    //   observer update FUN_1800c7f98: per slot FUN_1800c88c8(slot) first,
    //       then interpolation and FUN_1800c9cf8
    // Both loops have a single exit. The loop hooks open a scope (context -1,
    // restored on exit, so nesting yields native values, never another
    // slot's); the per-slot hooks set the slot only inside such a scope.
    // Calls with no context (script camera switches, observer resets, UI)
    // stay native.
    namespace {
        // Both per-slot loops iterate local slots 0..3.
        constexpr int MAX_SLOTS = 4;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

        // Native MCC UniversalFOV slider (Data/settings/optionsdata.xml).
        constexpr float kUiMinDeg = 70.0f;
        constexpr float kUiMaxDeg = 120.0f;

        thread_local int t_scopeDepth = 0;
        thread_local int t_slot = -1;

        struct LoopScope {
            int prevSlot;
            LoopScope() : prevSlot(t_slot) { ++t_scopeDepth; t_slot = -1; }
            ~LoopScope() { t_slot = prevSlot; --t_scopeDepth; }
        };

        __int64 g_hModule = 0;

        __int64 PlayersGlobals() {
            if (g_hModule == 0) return 0;
            unsigned tlsIndex = *(unsigned*)(g_hModule + OFFSET_HALOREACH_DAT_TLS_INDEX);
            __int64 tlsArray = (__int64)__readgsqword(0x58);
            if (tlsArray == 0) return 0;
            __int64 tls = *(__int64*)(tlsArray + (__int64)tlsIndex * 8);
            if (tls == 0) return 0;
            return *(__int64*)(tls + 0x158);
        }

        // [SplitFov] support line: logs only when a slot's source changes
        // (value changes during a slider drag exhausted a value-keyed filter
        // in validation), with a per-slot cap in case a source ever flaps.
        enum { SOURCE_ALPHA_RING = 0, SOURCE_NATIVE = 1 };
        struct LastResolved { int source; bool has; int lines; };
        LastResolved g_lastResolved[MAX_SLOTS] = {};
        constexpr int RESOLVED_LOG_CAP = 16;

        void LogResolved(int slot, int source, float rad) {
            if (!AlphaRing::DebugFlags::g_splitFovSourceLog) return;
            auto& last = g_lastResolved[slot];
            if (last.has && last.source == source) return;
            if (last.lines >= RESOLVED_LOG_CAP) return;
            last = { source, true, last.lines + 1 };
            static const char* names[] = { "alpha_ring", "native" };
            LOG_INFO("[SplitFov] baseline slot={} source={} horizontal_deg={:.3f}",
                     slot, names[source], rad * kRadToDeg);
        }
    }

    namespace Director {
        HaloReachEntry(entry, OFFSET_HALOREACH_PF_DIRECTOR_UPDATE, void, detour, float dt) {
            typedef void (*detour_t)(float);
            g_hModule = entry.m_target - entry.m_offset;
            LoopScope scope;
            ((detour_t)entry.m_pOriginal)(dt);
        }
    }

    namespace DirectorSlot {
        HaloReachEntry(entry, OFFSET_HALOREACH_PF_DIRECTOR_SLOT_PREUPDATE, void, detour, int slot) {
            typedef void (*detour_t)(int);
            if (t_scopeDepth > 0) t_slot = slot;
            ((detour_t)entry.m_pOriginal)(slot);
        }
    }

    namespace Observer {
        HaloReachEntry(entry, OFFSET_HALOREACH_PF_OBSERVER_UPDATE, void, detour, float dt) {
            typedef void (*detour_t)(float);
            g_hModule = entry.m_target - entry.m_offset;
            LoopScope scope;
            ((detour_t)entry.m_pOriginal)(dt);
        }
    }

    namespace ObserverSlot {
        HaloReachEntry(entry, OFFSET_HALOREACH_PF_APPLY_RAW_INPUT, void, detour, int slot) {
            typedef void (*detour_t)(int);
            if (t_scopeDepth > 0) t_slot = slot;
            ((detour_t)entry.m_pOriginal)(slot);
        }
    }

    namespace Getter {
        HaloReachEntry(entry, OFFSET_HALOREACH_PF_GET_FOV_BASELINE, float, detour, char useProfile) {
            typedef float (*detour_t)(char);
            float native = ((detour_t)entry.m_pOriginal)(useProfile);

            if (!AlphaRing::DebugFlags::g_splitscreenFovBaseline) return native;
            if (useProfile == 0 || t_scopeDepth <= 0) return native;
            int slot = t_slot;
            if (slot < 0 || slot >= MAX_SLOTS) return native;
            // 1P stays fully native.
            __int64 pg = PlayersGlobals();
            if (pg == 0 || *(short*)(pg + 0xB4) < 2) return native;

            // Override OFF = Reach's native split-screen FOV.
            auto* g = AlphaRing::Global::Global();
            if (!g->splitscreen_fov_set[slot]) {
                LogResolved(slot, SOURCE_NATIVE, native);
                return native;
            }

            float deg = g->splitscreen_fov_deg[slot];
            if (!(deg >= kUiMinDeg)) deg = kUiMinDeg;  // also catches NaN
            if (deg > kUiMaxDeg) deg = kUiMaxDeg;
            float rad = deg * kDegToRad;
            LogResolved(slot, SOURCE_ALPHA_RING, rad);
            return rad;
        }
    }
}
