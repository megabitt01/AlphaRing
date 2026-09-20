#include "haloreach.h"

#include <cstring>
#include <functional>
#include <string>

#include "common.h"
#include "log/DebugFlags.h"
#include "mcc/module/patch/SplitscreenConfigStore.h"

namespace HaloReach::Entry::World {void AddTask(const std::function<void()>& func);}

namespace {
    // ---------------------------------------------------------------------
    // CAPTURE PROBE (DebugFlags::g_renderThrottle). Read-only, bounded.
    //
    // Reach's split-screen render-quality downgrade is one table lookup:
    // FUN_180270258 (0x270258), called once per frame from UpdateAllPlayerViews
    // at 0xC358E, selects a 0x50-byte "performance throttle" record by
    // GetSplitscreenPlayerCount() alone - index (count-1) - and copies it to the
    // live block at 0xCA0240, of which 0x270349 is the DLL's only writer.
    //
    // The probe captures the live block and reconstructs both candidate records
    // for the active player count - tag[count] and tag[1P] - each run through
    // Reach's own MCC quality-tier pass, so which record is live is proven
    // rather than inferred. That is what distinguishes a native frame from one
    // where the force-1P patch is active.
    // ---------------------------------------------------------------------
    constexpr int   THROTTLE_SIZE       = 0x50;
    constexpr int   THROTTLE_MAX_STATES = 16;  // <= 16 x 6 + 5 lines for the process
    constexpr float THROTTLE_SCALE_NOOP = 0.0f;  // DAT_180a7a434, the value +0x4C is tested against

    float ThrottleF(const unsigned char* r, int off) {
        float v; std::memcpy(&v, r + off, sizeof v); return v;
    }
    int ThrottleI(const unsigned char* r, int off) {
        int v; std::memcpy(&v, r + off, sizeof v); return v;
    }
    unsigned ThrottleU(const unsigned char* r, int off) {
        unsigned v; std::memcpy(&v, r + off, sizeof v); return v;
    }

    // Field names come from the debug setter FUN_180270414, which maps each name
    // to record_base(count) + offset. Eight fields it cannot set are printed by
    // offset instead of being given a guessed name.
    std::string ThrottleFields(const unsigned char* r) {
        char buf[512];
        std::snprintf(buf, sizeof buf,
            "flags=0x%08X water=%.3f decorator=%.3f effect=%.3f instance=%.3f "
            "object_fade=%.3f +18=%.3f +1C=%.3f decals=%.3f structure_lod=%.3f "
            "cpu_lights=%d +2C=%.3f gpu_lights=%d +34=%.3f +38=%d +3C=%.3f "
            "shadow_count=%d shadow_quality=%.3f aniso=%.3f +4C=%.3f",
            ThrottleU(r, 0x00), ThrottleF(r, 0x04), ThrottleF(r, 0x08),
            ThrottleF(r, 0x0C), ThrottleF(r, 0x10), ThrottleF(r, 0x14),
            ThrottleF(r, 0x18), ThrottleF(r, 0x1C), ThrottleF(r, 0x20),
            ThrottleF(r, 0x24), ThrottleI(r, 0x28), ThrottleF(r, 0x2C),
            ThrottleI(r, 0x30), ThrottleF(r, 0x34), ThrottleI(r, 0x38),
            ThrottleF(r, 0x3C), ThrottleI(r, 0x40), ThrottleF(r, 0x44),
            ThrottleF(r, 0x48), ThrottleF(r, 0x4C));
        return buf;
    }

    unsigned ThrottleHash(const unsigned char* r) {
        unsigned h = 2166136261u;
        for (int i = 0; i < THROTTLE_SIZE; ++i) { h ^= r[i]; h *= 16777619u; }
        return h;
    }

    // The selector's optional post-copy rescale (0x270380-0x2703EA): when flags
    // bit 9 is set, +0x4C != 0.0, FUN_180058b5c() holds and the total player
    // count exceeds 2, it multiplies +0x14, +0x18 and +0x24 by +0x4C in place.
    // Measured inert on this build (bit 9 clear, +0x4C == 0.0 in every record),
    // but modelled anyway so the reconstruction stays correct if that changes.
    // The two runtime predicates are not replicated; the two data conditions are.
    void ThrottleApplyRescale(unsigned char* r) {
        if (!((ThrottleU(r, 0x00) >> 9) & 1u)) return;
        const float s = ThrottleF(r, 0x4C);
        if (s == THROTTLE_SCALE_NOOP) return;
        for (int off : {0x14, 0x18, 0x24}) {
            const float v = ThrottleF(r, off) * s;
            std::memcpy(r + off, &v, sizeof v);
        }
    }

    // Reconstructs what the live block would hold had the selector chosen this
    // record: the optional rescale, then Reach's OWN MCC quality-tier pass
    // (0x3AEF8) on a scratch copy - the same two steps the selector runs at
    // 0x270380 and 0x2703FC. Calling the game's function rather than
    // reimplementing it means the live MCC graphics settings are applied
    // exactly, instead of a guess at the multipliers.
    //
    // 0x3AEF8 is a pure transform over the buffer it is handed - it reads
    // globals and writes only through its dst pointer, and the throttle tail is
    // its only caller - so running it over our own scratch buffer has no side
    // effects. src and dst are the same buffer, as at the native call site,
    // because the pass leaves decals (+0x20) and +0x4C untouched and ORs into
    // the destination's flags word.
    void ThrottleExpected(__int64 hModule, const unsigned char* record, unsigned char* out) {
        std::memcpy(out, record, THROTTLE_SIZE);
        ThrottleApplyRescale(out);
        if (!*(void**)(hModule + OFFSET_HALOREACH_DAT_QUALITY_TIER_GATE)) return;
        using tier_t = void (*)(unsigned __int64, const void*, void*);
        ((tier_t)(hModule + OFFSET_HALOREACH_PF_APPLY_QUALITY_TIER))(0, out, out);
    }

    // Read-only reimplementation of the tag-block resolution inside
    // FUN_180270258 (0x27028B-0x270342). Returns nullptr exactly where the
    // selector would have fallen through to the static table. Deliberately free
    // of C++ objects so the caller can wrap it in __try/__except: it walks
    // engine pointers derived from static analysis only.
    const unsigned char* ResolveThrottleTagRecord(__int64 hModule, int playerCount) {
        if (playerCount < 1) return nullptr;

        const __int64 segTable = hModule + OFFSET_HALOREACH_DAT_TAG_SEGMENT_TABLE;
        const __int64 tagIndex = *(__int64*)(hModule + OFFSET_HALOREACH_DAT_TAG_INDEX_TABLE);
        if (!tagIndex) return nullptr;

        unsigned tagref = 0xFFFFFFFFu;

        const __int64 scenario = *(__int64*)(hModule + OFFSET_HALOREACH_DAT_SCENARIO_GLOBALS);
        if (scenario) tagref = *(unsigned*)(scenario + 0x724);

        if (tagref == 0xFFFFFFFFu) {
            // Fall back to the engine globals tag, whose body holds the
            // reference at +0x64.
            const __int64 engine = *(__int64*)(hModule + OFFSET_HALOREACH_DAT_ENGINE_GLOBALS);
            if (!engine) return nullptr;
            const unsigned gref = *(unsigned*)(engine + 0x554);
            if (gref == 0xFFFFFFFFu) return nullptr;

            const unsigned gHandle = *(unsigned*)(tagIndex + (__int64)(gref & 0xFFFF) * 8 + 4);
            const __int64 gSeg = *(__int64*)(segTable + (__int64)(gHandle >> 28) * 8);
            if (!gSeg) return nullptr;
            tagref = *(unsigned*)(gSeg + (__int64)gHandle * 4 + 0x64);
            if (tagref == 0xFFFFFFFFu) return nullptr;
        }

        const unsigned handle = *(unsigned*)(tagIndex + (__int64)(tagref & 0xFFFF) * 8 + 4);
        const __int64 segBase = *(__int64*)(segTable + (__int64)(handle >> 28) * 8);
        if (!segBase) return nullptr;

        // Short block -> the selector uses the static table instead.
        const int blockCount = *(int*)(segBase + (__int64)handle * 4 + 4);
        if (playerCount > blockCount) return nullptr;

        const unsigned elements = *(unsigned*)(segBase + (__int64)handle * 4 + 8);
        const __int64 elemBase = *(__int64*)(segTable + (__int64)(elements >> 28) * 8);
        if (!elemBase) return nullptr;

        return (const unsigned char*)(elemBase + ((__int64)elements + (__int64)(playerCount - 1) * 0x14) * 4);
    }

    // Copies the tag record out under SEH so a wrong pointer inference degrades
    // to "unresolved" instead of taking the game down.
    bool CopyThrottleTagRecord(__int64 hModule, int playerCount, unsigned char* out) {
        __try {
            const unsigned char* rec = ResolveThrottleTagRecord(hModule, playerCount);
            if (!rec) return false;
            std::memcpy(out, rec, THROTTLE_SIZE);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void CaptureRenderThrottle(__int64 hModule) {
        if (!AlphaRing::DebugFlags::g_renderThrottle) return;

        // The static fallback table at 0xB43E40 is not read here: use_static
        // (0x4E389B0) measured 0 and no static record ever matched the live
        // block, so on this build it is dead data. If that is ever in doubt
        // again, dump it from the DLL image rather than from a hot path.
        const unsigned char* live = (const unsigned char*)(hModule + OFFSET_HALOREACH_DAT_RENDER_THROTTLE_LIVE);

        using count_t = int (*)();
        const int playerCount =
            ((count_t)(hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT))();
        if (playerCount < 1 || playerCount > 4) return;

        // The MCC graphics setting bytes the tier pass reads. These are part of
        // the captured state, not context: a preset change that moves them but
        // leaves the live block identical - every field already clamped or
        // saturated - is exactly the result worth logging, and keying on the
        // block hash alone would silently suppress it.
        const unsigned char* settings =
            (const unsigned char*)(hModule + OFFSET_HALOREACH_DAT_MCC_QUALITY_SETTINGS);
        unsigned settingsKey = 2166136261u;
        for (int i = 0; i < OFFSET_HALOREACH_V_MCC_QUALITY_SETTING_COUNT; ++i) {
            settingsKey ^= settings[i];
            settingsKey *= 16777619u;
        }

        char settingsText[64];
        {
            int n = 0;
            for (int i = 0; i < OFFSET_HALOREACH_V_MCC_QUALITY_SETTING_COUNT && n < 60; ++i)
                n += std::snprintf(settingsText + n, sizeof settingsText - n, "%s%d",
                                   i ? "," : "", (int)settings[i]);
        }

        // Bounded distinct-state set keyed on the WHOLE captured state - player
        // count, a hash of all 0x50 live bytes, and the MCC setting bytes.
        // Keying on a subset would let a changed input escape the filter.
        const unsigned hash = ThrottleHash(live);
        static struct { int count; unsigned hash; unsigned settingsKey; } s_seen[THROTTLE_MAX_STATES];
        static int s_seenCount = 0;
        for (int i = 0; i < s_seenCount; ++i)
            if (s_seen[i].count == playerCount && s_seen[i].hash == hash &&
                s_seen[i].settingsKey == settingsKey) return;
        if (s_seenCount >= THROTTLE_MAX_STATES) return;
        s_seen[s_seenCount].count       = playerCount;
        s_seen[s_seenCount].hash        = hash;
        s_seen[s_seenCount].settingsKey = settingsKey;
        ++s_seenCount;

        // Both candidate records: the one this player count would select
        // natively, and the 1-player one the force-1P patch pins it to.
        unsigned char tagNow[THROTTLE_SIZE], tagOne[THROTTLE_SIZE];
        const bool haveTagNow = CopyThrottleTagRecord(hModule, playerCount, tagNow);
        const bool haveTagOne = (playerCount == 1)
                                ? (haveTagNow ? (std::memcpy(tagOne, tagNow, THROTTLE_SIZE), true) : false)
                                : CopyThrottleTagRecord(hModule, 1, tagOne);

        unsigned char expNow[THROTTLE_SIZE], expOne[THROTTLE_SIZE];
        if (haveTagNow) ThrottleExpected(hModule, tagNow, expNow);
        if (haveTagOne) ThrottleExpected(hModule, tagOne, expOne);

        const bool matchTagNow = haveTagNow && std::memcmp(live, tagNow, THROTTLE_SIZE) == 0;
        const bool matchTagOne = haveTagOne && std::memcmp(live, tagOne, THROTTLE_SIZE) == 0;
        const bool matchExpNow = haveTagNow && std::memcmp(live, expNow, THROTTLE_SIZE) == 0;
        const bool matchExpOne = haveTagOne && std::memcmp(live, expOne, THROTTLE_SIZE) == 0;

        // tag[1P] is checked before tag[count] so the patched case is named
        // unambiguously; at 1P the two are the same record and either label is
        // correct.
        const char* source = "UNRESOLVED";
        if      (matchExpOne) source = "tag[1P] x MCC tier  <- FORCED-1P ACTIVE";
        else if (matchTagOne) source = "tag[1P] raw (tier pass did not run)";
        else if (matchExpNow) source = "tag[count] x MCC tier  <- NATIVE";
        else if (matchTagNow) source = "tag[count] raw (tier pass did not run)";

        const bool rescaleBit   = (ThrottleU(live, 0x00) >> 9) & 1u;
        const bool rescaleArmed = rescaleBit && ThrottleF(live, 0x4C) != THROTTLE_SCALE_NOOP;

        const bool tierRan = *(void**)(hModule + OFFSET_HALOREACH_DAT_QUALITY_TIER_GATE) != nullptr;

        LOG_INFO("[Throttle] {}p hash=0x{:08X} mcc_settings=[{}] tier_gate={} source={}",
                 playerCount, hash, settingsText, tierRan, source);
        LOG_INFO("[Throttle] {}p   match tag[1P]x tier={} raw={} | tag[{}p]x tier={} raw={} "
                 "| resolved(1p={} {}p={}) rescale(bit9={} armed={})",
                 playerCount, matchExpOne, matchTagOne, playerCount, matchExpNow, matchTagNow,
                 haveTagOne, playerCount, haveTagNow, rescaleBit, rescaleArmed);
        LOG_INFO("[Throttle] {}p   live       {}", playerCount, ThrottleFields(live));
        if (haveTagNow)
            LOG_INFO("[Throttle] {}p   tag[{}p]    {}", playerCount, playerCount, ThrottleFields(tagNow));
        else
            LOG_INFO("[Throttle] {}p   tag[{}p]    <unresolved - selector would fall back to the static table>",
                     playerCount, playerCount);
        if (haveTagOne) {
            // Raw then tiered, so the tier pass's effect on the forced record is
            // readable field by field without diffing against another run.
            LOG_INFO("[Throttle] {}p   tag[1P]raw {}", playerCount, ThrottleFields(tagOne));
            LOG_INFO("[Throttle] {}p   tag[1P]xT  {}", playerCount, ThrottleFields(expOne));
        }
    }

    // PRODUCTION BEHAVIOR GATE, not a probe (DebugFlags::g_splitRtLiveRebuild).
    // Validated in game 2026-09-16 (2P and 3P, both switch directions, no
    // Alt+Tab). Top/Bottom and Left/Right both select render-target variant
    // res=3, and Reach's pool has
    // no key, dirty flag or lazy re-request: each surface is sized once, when
    // RT_POOL_INIT runs (engine start or resize). A live layout switch therefore
    // keeps the surface sized for the previous layout until the next match.
    //
    // When the layout generation differs from the one the pool was built
    // against, run Reach's own release/init pair - the same pair its pending-
    // resize path (FUN_18025103c) runs immediately before UpdateAllPlayerViews,
    // i.e. this exact point in the frame. RT_CREATE is never called directly
    // (it overwrites entries without releasing them), the swapchain is not
    // resized, and no host event is posted.
    //
    // Guard: the 0x2D4220 scratch-target stack must be empty, otherwise a
    // descriptor checked out by an open pass would be released under it.
    void RebuildRenderTargetsIfLayoutChanged(__int64 hModule) {
        if (!AlphaRing::DebugFlags::g_splitRtLiveRebuild) return;

        unsigned current = AlphaRing::SplitscreenConfigStore::GetLayoutGeneration();
        unsigned built   = HaloReach::Entry::SplitscreenRt::BuiltLayoutGeneration();
        if (current == built) return;

        // Bounded: at most one line per rebuild/deferral transition, and a hard
        // cap for the session so a pathological toggle loop cannot flood.
        constexpr int LOG_CAP = 64;
        static int s_logged = 0;
        static unsigned s_deferredGeneration = ~0u;

        int scratchDepth = *(int*)(hModule + OFFSET_HALOREACH_DAT_RT_SCRATCH_DEPTH);
        if (scratchDepth != 0) {
            if (s_deferredGeneration != current && s_logged < LOG_CAP) {
                ++s_logged;
                LOG_INFO("[SplitRtRebuild] deferred gen {}->{}: scratch depth {}",
                         built, current, scratchDepth);
            }
            s_deferredGeneration = current;
            return;
        }

        using release_t = __int64 (*)();
        using init_t    = int (*)();
        ((release_t)(hModule + OFFSET_HALOREACH_PF_RT_POOL_RELEASE))();
        int hr = ((init_t)(hModule + OFFSET_HALOREACH_PF_RT_POOL_INIT))();

        // Record even on failure so a failing init is not retried every frame.
        HaloReach::Entry::SplitscreenRt::MarkLayoutGenerationBuilt(current);

        if (s_logged < LOG_CAP) {
            ++s_logged;
            LOG_INFO("[SplitRtRebuild] gen {}->{} layout={} pool init hr=0x{:08X}",
                     built, current,
                     (int)AlphaRing::SplitscreenConfigStore::GetTwoPlayerLayout(),
                     (unsigned)hr);
        }
    }
}

namespace HaloReach::Entry::Render {
    void Prologue() {

    }
    void Epilogue() {
//        if (MainRenderer()->ShowContext() && MainRenderer()->NewFrame()) {
//            ImGui::Begin("Halo Reach");
//            if (ImGui::Button("Add Player")) {
//                HaloReach::Entry::World::AddTask([]() {
//                    NativeHaloReach()->NativeFunc()->local_player_add(L"UWU", L"UWU");
//                });
//            }
//            ImGui::End();
//        }
    }
    HaloReachEntry(entry, OFFSET_HALOREACH_PF_RENDER, void, detour) {
        // Re-assert any saved Splitscreen Config Editor values before the frame
        // is drawn. The game rewrites c_splitscreen_config::m_config_table back
        // to its shipped values on every level load, so a one-shot restore at
        // module-load time does not survive into a match (confirmed in-game
        // 2026-09-12: starting a second match without re-applying comes up as
        // stock splitscreen). Doing it here means a custom slot shape is in
        // place from the first frame of a level rather than from whenever the
        // user happens to open the F1 menu.
        //
        // Apply() compares before it writes, so once the table matches this is
        // a few 4-byte reads per frame and no writes.
        AlphaRing::SplitscreenConfigStore::Apply(entry.m_target - entry.m_offset);
        RebuildRenderTargetsIfLayoutChanged(entry.m_target - entry.m_offset);

        Prologue();
        ((detour_t)entry.m_pOriginal)();

        // After the original, so the live throttle block reflects the record
        // this frame actually applied - the selector runs inside
        // UpdateAllPlayerViews (0xC358E), not before it.
        CaptureRenderThrottle(entry.m_target - entry.m_offset);

        Epilogue();
    }
}
