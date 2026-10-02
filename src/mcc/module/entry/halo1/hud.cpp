#include "halo1.h"

#include "mcc/hud/Hud.h"
#include "mcc/splitscreen/LeftRight.h"

#include <cmath>

// Per-player HUD element offsets for Halo CE (mcc/hud/Hud.h). MCC's own host offsets are added
// to X only in CE, so offsets are applied here instead, to every placed HUD point.
namespace Halo1::Entry::Hud {
    // hud_calculate_point(local player, placement, element header, unused, use scale override,
    // scale override, out point (int16 x, y relative to the viewport), MCC element id). The scale
    // override is a float on the stack, so the original is called through this exact type.
    using calculate_point_t = __int64(__fastcall*)(short, void*, void*, void*, bool, float, short*, int);

    EntryFeature("Halo CE per-player HUD", OFFSET_HALO1_PV_HUD_VIEWPORT_BOUNDS);
    // CE's settings require it (mcc/hud/Hud.cpp): without this hook none of them apply.
    const ::AlphaRing::Feature& Hooks() { return *entry_feature; }

    Halo1Entry(entry, OFFSET_HALO1_PF_HUD_CALCULATE_POINT, __int64, detour, short player, void* placement, void* header,
               void* unused, bool use_scale, float scale, short* out, int element) {
        auto result = ((calculate_point_t)entry.m_pOriginal)(player, placement, header, unused, use_scale, scale, out, element);

        auto& viewport = *(const MCC::Splitscreen::LeftRight::Rect*)(entry.m_target - entry.m_offset +
                                                                    OFFSET_HALO1_PV_HUD_VIEWPORT_BOUNDS);
        float dx, dy;
        if (out && MCC::Hud::OffsetCE(player, element, viewport.right - viewport.left, viewport.bottom - viewport.top,
                                      &dx, &dy)) {
            out[0] = (short)(out[0] + std::lround(dx));
            out[1] = (short)(out[1] + std::lround(dy));
        }
        return result;
    }
}
