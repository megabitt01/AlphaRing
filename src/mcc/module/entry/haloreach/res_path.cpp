#include "haloreach.h"

#include "common.h"

namespace HaloReach::Entry::ResPath {
    // Publishes which splitscreen slot is currently being rendered, or -1
    // outside a player frame, so a slot value can be attributed to CHUD
    // constant uploads (chud_const.cpp's g_chudUpload/g_chudCensus logging)
    // without collapsing both slots under a change-only filter keyed on value
    // alone. This used to also feed D3D11 GPU-boundary probes (RSSetViewports/
    // RSSetScissorRects/CreateTexture2D in Hook.cpp); those answered the
    // portrait-viewport question they existed for and were removed from the
    // release branch, but chud_const.cpp's own census is a separate, still-
    // live consumer, so this file stays.
    //
    // UpdatePlayerFrame wraps the scene draws for one slot, so setting this
    // around the original call scopes it correctly. volatile because it is
    // written here and read from another translation unit.
    //
    // This file previously also logged the `res` propagation path
    // (m_config_table -> frame+0x3a4 -> DAT_184e38a08 -> 0x88-stride descriptor
    // table). That question is answered and the result was negative: res
    // propagates identically in stock and portrait, so it is not the variable.
    // See commit 18e0bcd and the notes repo; the logging is removed rather than
    // left dormant, because the entry array is capped at MAX_ENTRY and probes
    // that have served their purpose should not hold slots.
    volatile int g_activeSlot = -1;

    HaloReachEntry(entry, OFFSET_HALOREACH_PF_UPDATE_PLAYER_FRAME_INNER, void, detour,
                   __int64 frame) {
        typedef void (*detour_t)(__int64);

        int prevActive = g_activeSlot;
        g_activeSlot = (frame != 0) ? *(int*)(frame + 0x39c) : -1;

        ((detour_t)entry.m_pOriginal)(frame);

        g_activeSlot = prevActive;
    }
}
