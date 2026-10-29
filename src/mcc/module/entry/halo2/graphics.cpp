#include "halo2.h"

#include "mcc/CGameGlobal.h"
#include "mcc/splitscreen/Splitscreen.h"

namespace Halo2::Entry::Graphics {
    Halo2Entry(entry, OFFSET_HALO2_PF_COPY_GAME_OPTIONS, __int64, detour, unsigned char* game_options, void* a2, void* a3, void* a4) {
        MCC::Splitscreen::ClassicGraphicsScope classic(game_options, CGameGlobal::Halo2);
        return ((detour_t)entry.m_pOriginal)(game_options, a2, a3, a4);
    }
}
