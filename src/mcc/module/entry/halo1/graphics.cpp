#include "halo1.h"

#include "mcc/CGameGlobal.h"
#include "mcc/splitscreen/Splitscreen.h"

namespace Halo1::Entry::Graphics {
    Halo1Entry(entry, OFFSET_HALO1_PF_GAME_START, __int64, detour, void* self, void* manager, unsigned char* game_options, void* a4) {
        MCC::Splitscreen::ClassicGraphicsScope classic(game_options, CGameGlobal::Halo1);
        return ((detour_t)entry.m_pOriginal)(self, manager, game_options, a4);
    }
}
