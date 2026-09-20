#include "haloreach.h"

#include "common.h"

#include "log/DebugFlags.h"

#include <cstring>

namespace HaloReach::Entry::ResPath { extern volatile int g_activeSlot; }

namespace HaloReach::Entry::ChudConst {
    // Instruments the constants as the SHADER receives them, not as we think
    // they were computed.
    //
    // FUN_180271200(id, ptr, size) is the engine's "upload one constant block"
    // call. The id encodes which cbuffer: bits 16-26 select the family, the low
    // 16 bits the element index within it. Established previously:
    //
    //     0x30000   = CHUDVS (table 3), element 0 = chud_screen_size
    //     0x30001   = CHUDVS element 1 = start of the 5-float4 basis block
    //     0x310000  = table 0x31, element 0 - the loadout backdrop camera block
    //                 (observed live from SetupLoadoutBackdropCamera)
    //
    // chud_screen_size matters because the generic icon vertex shader's FINAL
    // output position is `r0.xy / chud_screen_size.xy`. That is the last divide
    // before output and it is per-axis, so if .xy disagrees with the surface
    // actually bound, every widget stretches by exactly that ratio.
    //
    // WHY HOOK HERE RATHER THAN TRACE XREFS. Five runs now separate cleanly on
    // the render-target detour's scaleX (>=1 clean, <1 distorted, 5/5), while
    // slot shape and scaleY each appear on both sides. Rather than walk XREFs
    // from the slot rect forward - which has a poor hit rate in this binary and
    // cannot see indirect calls - this reads the bytes handed to the GPU. The
    // value that reaches the consumer is the only one that settles it.
    //
    // PREDICTION ON THE RECORD (2026-09-13): chud_screen_size.x will carry the
    // STOCK-shaped width while the bound surface is the scaled one, and their
    // ratio will equal scaleX. If the log shows something else, the prediction
    // is wrong and gets recorded as wrong - it does not get reinterpreted to
    // fit.

    // Bounded distinct-state filter, present from the outset because this is a
    // hot path. Same shape as hud_anchor.cpp's: remember whole states, never
    // key on a subset (keying is what made two earlier filters thrash), and go
    // permanently quiet once full.
    namespace {
        // TWO INDEPENDENT POOLS.
        //
        // The first version shared one 32-state pool between the targeted
        // lookup and the catch-all discovery branch. Discovery filled all 32
        // before the target was ever seen, so the probe went quiet and produced
        // no data on the thing it existed to measure. A shared budget between a
        // specific question and an open-ended one always starves the specific
        // one - keep them separate.
        template <int N, int W>
        struct Pool {
            float v[N][W]{};
            int   n = 0;
            bool  First(const float* now) {
                for (int i = 0; i < n; ++i)
                    if (memcmp(v[i], now, sizeof(float) * W) == 0) return false;
                if (n >= N) return false;
                memcpy(v[n++], now, sizeof(float) * W);
                return true;
            }
        };

        Pool<24, 6> s_target;      // table 3 (CHUDVS) - the question
        Pool<96, 6> s_discovery;
    }


    // ------------------------------------------------------------------
    // CENSUS: which cbuffer element is uploaded PER WIDGET?
    //
    // Scoping question: chud_screen_scale_and_offset couples spread and size
    // through halfW/halfH, so the frame cannot move widgets apart without
    // resizing them. The CHUDWidgetVS cbuffer (cb1) separates them - it carries
    // chud_widget_offset (position) and chud_widget_transform1/2/3 (size) as
    // DISTINCT constants, per widget instance. If it is uploaded through
    // FUN_180271200 we can scale the offset alone and get spread without resize.
    // Its table index in the id scheme has never been determined.
    //
    // The discriminator is upload frequency, not value. A per-SLOT constant
    // (like CHUDVS table 3) uploads a couple of times per frame. A
    // per-WIDGET-INSTANCE buffer uploads once per widget - tens of times per
    // frame - with a DIFFERENT value almost every time, because each widget sits
    // somewhere else.
    //
    // So count, per (table, elem): total uploads, and how many distinct payloads
    // were seen (FNV-1a over the first 16 bytes, sampled into a small set).
    // Normalised against CHUDVS elem 0, which is ~2 per frame (one per slot),
    // that gives uploads-per-slot-frame directly.
    //
    // Dumps ONCE after enough frames and then goes permanently quiet - same
    // bounded discipline as every other probe here.
    namespace {
        struct Cell {
            int table, elem; unsigned size;
            unsigned uploads, distinct;
            unsigned hashes[24];
        };
        Cell s_cells[256]{};
        int  s_cellCount = 0;
        unsigned s_chudvs0 = 0;
        bool s_dumped = false;

        unsigned HashPayload(const void* p, unsigned n) {
            if (n > 16) n = 16;
            const unsigned char* b = (const unsigned char*)p;
            unsigned h = 2166136261u;
            for (unsigned i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
            return h;
        }

        void Census(int table, int elem, unsigned size, const void* payload) {
            if (s_dumped) return;

            Cell* c = nullptr;
            for (int i = 0; i < s_cellCount; ++i)
                if (s_cells[i].table == table && s_cells[i].elem == elem) { c = &s_cells[i]; break; }
            if (c == nullptr) {
                if (s_cellCount >= 256) return;
                c = &s_cells[s_cellCount++];
                c->table = table; c->elem = elem; c->size = size;
                c->uploads = 0; c->distinct = 0;
            }
            c->uploads++;

            unsigned h = HashPayload(payload, size);
            bool seen = false;
            for (unsigned i = 0; i < c->distinct; ++i)
                if (c->hashes[i] == h) { seen = true; break; }
            if (!seen && c->distinct < 24) c->hashes[c->distinct++] = h;

            if (table == 3 && elem == 0) s_chudvs0++;

            // ~2 CHUDVS elem-0 uploads per frame (one per slot), so 400 is
            // roughly 200 frames - a few seconds, enough for every widget to
            // have been drawn.
            if (s_chudvs0 < 400) return;
            s_dumped = true;

            float frames = (float)s_chudvs0 / 2.0f;
            LOG_INFO("[ChudCensus] ==== {} distinct (table,elem) over ~{:.0f} frames ====",
                     s_cellCount, frames);
            LOG_INFO("[ChudCensus] per-frame >= 10 with distinct==24 (saturated) is the "
                     "per-widget signature; ~1-2 per frame is a per-slot constant");

            // simple selection sort by uploads, descending - only runs once
            for (int a = 0; a < s_cellCount; ++a) {
                int best = a;
                for (int b = a + 1; b < s_cellCount; ++b)
                    if (s_cells[b].uploads > s_cells[best].uploads) best = b;
                if (best != a) { Cell t = s_cells[a]; s_cells[a] = s_cells[best]; s_cells[best] = t; }
            }
            int shown = s_cellCount < 40 ? s_cellCount : 40;
            for (int i = 0; i < shown; ++i) {
                Cell& e = s_cells[i];
                LOG_INFO("[ChudCensus] table={:<4} elem={:<4} size={:<3} | uploads={:<7} "
                         "per-frame={:<8.1f} distinct>={}{}",
                         e.table, e.elem, e.size, e.uploads, e.uploads / frames, e.distinct,
                         (e.uploads / frames >= 10.0f && e.distinct >= 24)
                             ? "   <-- PER-WIDGET CANDIDATE" : "");
            }
        }
    }

    HaloReachEntry(entry, OFFSET_HALOREACH_PF_UPLOAD_CONSTANT, __int64, detour,
                   unsigned int id, void* ptr, unsigned int size) {
        typedef __int64 (*detour_t)(unsigned int, void*, unsigned int);

        if (AlphaRing::DebugFlags::g_chudCensus && ptr != nullptr)
            Census((int)((id >> 16) & 0x7ff), (int)(id & 0xffff), size, ptr);

        if (AlphaRing::DebugFlags::g_chudUpload && ptr != nullptr) {
            int slot  = HaloReach::Entry::ResPath::g_activeSlot;
            int table = (int)((id >> 16) & 0x7ff);
            int elem  = (int)(id & 0xffff);
            const float* v = (const float*)ptr;

            float f0 = size >= 4  ? v[0] : 0.0f, f1 = size >= 8  ? v[1] : 0.0f;
            float f2 = size >= 12 ? v[2] : 0.0f, f3 = size >= 16 ? v[3] : 0.0f;

            // TARGET: any CHUDVS element, not just 0x30000. The previous run saw
            // no table 3 at all, which may mean the pool starved before it
            // appeared - or that chud_screen_size never comes through this
            // function. Logging every table-3 element distinguishes those.
            if (table == 3) {
                float now[6] = { (float)slot, (float)elem, f0, f1, f2, f3 };
                if (s_target.First(now))
                    LOG_INFO("[ChudConst] *** CHUDVS *** slot={} elem={} size={} | ({:.2f}, {:.2f}, {:.2f}, {:.2f}){}",
                             slot, elem, size, f0, f1, f2, f3,
                             elem == 0 ? "   <-- chud_screen_size, shader divides by x,y" : "");
            }
            else {
                float now[6] = { (float)table, (float)elem, (float)size, f0, 0.0f, 11.0f };
                if (s_discovery.First(now))
                    LOG_INFO("[ChudConst] slot={} id={:#x} table={} elem={} size={} | first4=({:.4f}, {:.4f}, {:.4f}, {:.4f})",
                             slot, id, table, elem, size, f0, f1, f2, f3);
            }
        }

        return ((detour_t)entry.m_pOriginal)(id, ptr, size);
    }
}
