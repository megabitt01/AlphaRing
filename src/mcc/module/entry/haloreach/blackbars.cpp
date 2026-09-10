#include "haloreach.h"

#include "common.h"

#include "global/Global.h"

namespace HaloReach::Entry::BlackBars {
    typedef unsigned __int64 (*GetSplitscreenPlayerCount_t)();
    typedef void (*DrawFilledRect_t)(void* rect, unsigned int color);
    typedef void (*RenderSetup1_t)(int, int);
    typedef void (*RenderSetup2_t)(int);

    // Matches the FUN_1800d3774 rect argument's field order, deduced from the
    // decompiled painter: {top, left, bottom, right}, screen-space pixels.
    struct ScreenRect { short top, left, bottom, right; };

    // c_splitscreen_config::m_config_table entry layout (dep/libmcc), 20 bytes/
    // entry, indexed [slot + player_count * 4] from base 0xB43C40 - same table
    // and indexing loadout.cpp's fix already relies on.
    struct SplitscreenViewConfig { float x0, y0, x1, y1; int resolution; };

    // The game's own black-bar/divider painter (RVA 0x2C6D84) hardcodes its
    // pillarbox math to slot 0's x0/x1 (via two static reads into
    // m_config_table[slot=0][count=2]) and spans the *full* screen height for
    // both bars, regardless of which slot is actually being painted. That's
    // invisible when every slot shares the same x0/x1 (the shipped default),
    // but it's why Player 2's independent "Remove Black Bar" toggle has no
    // visible effect: the shared painter still paints over the bottom half
    // using Player 1's (still-barred) bounds.
    //
    // This detour replaces the player_count==2 case with per-slot-correct
    // drawing - each slot's own bars are confined to that slot's own y0/y1
    // span and use that slot's own x0/x1. It's visually identical to the
    // original when both slots share bounds (the default), and correctly
    // handles independently-customized or fully-removed bars per slot.
    // 1p/3p/4p are untouched - falls through to the original function.
    HaloReachEntry(entry, OFFSET_HALOREACH_PF_DRAW_SPLITSCREEN_BLACK_BARS, void, detour) {
        if (AlphaRing::Global::Global()->disable_splitscreen_bars_debug)
            return;

        __int64 hModule = entry.m_target - entry.m_offset;

        auto GetSplitscreenPlayerCount = (GetSplitscreenPlayerCount_t)(hModule + OFFSET_HALOREACH_PF_GET_SPLITSCREEN_PLAYER_COUNT);
        int player_count = (int)GetSplitscreenPlayerCount();

        if (player_count != 2) {
            ((detour_t)entry.m_pOriginal)();
            return;
        }

        auto DrawFilledRect = (DrawFilledRect_t)(hModule + 0xD3774);
        auto RenderSetup1 = (RenderSetup1_t)(hModule + 0x274488);
        auto RenderSetup2 = (RenderSetup2_t)(hModule + 0x2743A4);

        short screenWidth = *(short*)(hModule + 0xB43A90);
        short screenHeight = *(short*)(hModule + 0xB43A94);

        auto configFor = [&](int slot) -> SplitscreenViewConfig* {
            return (SplitscreenViewConfig*)(hModule + 0xB43C40 + (size_t)(slot + 2 * 4) * 20);
        };
        SplitscreenViewConfig* slot0 = configFor(0);
        SplitscreenViewConfig* slot1 = configFor(1);

        RenderSetup1(0, 1);
        RenderSetup2(0);

        // divider line - unchanged from the original: a thin full-width band
        // at mid-screen height, between the two halves.
        short half = (short)(screenHeight >> 1);
        ScreenRect divider{ (short)(half - 1), 0, (short)(screenHeight - half + 1), screenWidth };
        DrawFilledRect(&divider, 0xff000000);

        auto drawSlotBars = [&](SplitscreenViewConfig* slot) {
            short top = (short)(slot->y0 * screenHeight) - 1;
            short bottom = (short)(slot->y1 * screenHeight);
            short leftBarRight = (short)(slot->x0 * screenWidth);
            short rightBarLeft = (short)(slot->x1 * screenWidth - 1.0f);

            ScreenRect left{ top, -1, bottom, leftBarRight };
            DrawFilledRect(&left, 0xff000000);

            ScreenRect right{ top, rightBarLeft, bottom, screenWidth };
            DrawFilledRect(&right, 0xff000000);
        };

        drawSlotBars(slot0);
        drawSlotBars(slot1);
    }
}
