#include "mcc.h"

#include <offset_mcc.h>

#include "CGameManager.h"
#include "CGameGlobal.h"

#include "mcc/module/Module.h"
#include "mcc/network/Network.h"
#include "mcc/splitscreen/Splitscreen.h"

namespace MCC {
    static bool* bIsInGame;
    static float (__fastcall* deltaTime)(long long qpc);

    float DeltaTime(__int64 a1) {
        return deltaTime(a1);
    }

    bool IsInGame() {
        return *bIsInGame;
    }

    bool Initialize() {
        LOG_INFO("MCC: initializing");

        bool result;
        CGameEngine** ppGameEngine;
        CGameManager* game_manager;
        CDeviceManager** device_manager;

        AlphaRing::Hook::Offset({
            {0x4000BA0/*0x3FFCAA8*/ , 0x3E4F9F8/*0x3E4B048*/, (void**)&ppGameEngine},
            {0x3F7B190/*0x3F76E50*/ , 0x3DCA200/*0x3DC54D0*/, (void**)&game_manager},
            {0x4001B78/*0x3FFFFF8*/ , 0x3E509C0/*0x3E4E590*/, (void**)&device_manager},
            {OFFSET_MCC_PF_DELTA_TIME, OFFSET_MCC_WS_PF_DELTA_TIME, (void**)&deltaTime},
            {0x4000B9F/*0x3FFCAA7*/ ,0x3E4F9F7/*0x3E4B047*/, (void**)&bIsInGame},
            {0x4000BC8/*0x3FFCAC0*/ , 0x3E4FA18/*0x3E4B060*/, (void**)&g_ppGameGlobal},
        });

        assertm(ppGameEngine != nullptr, "MCC: failed to get ppGameEngine");
        assertm(game_manager != nullptr, "MCC: failed to get pGameManager");
        assertm(device_manager != nullptr, "MCC: failed to get ppDeviceManager");

        LOG_DEBUG("MCC: resolved offsets");

        result = CGameEngine::Initialize(ppGameEngine);

        assertm(result, "MCC: failed to initialize GameEngine");

        LOG_DEBUG("MCC: initialized GameEngine");

        result = CGameManager::Initialize(game_manager);

        assertm(result, "MCC: failed to initialize GameManager");

        LOG_DEBUG("MCC: initialized GameManager");

        assertm(GameManager() != nullptr, "MCC:Splitscreen: GameManager is null"); // static instance

        result = CDeviceManager::Initialize(device_manager);

        assertm(result, "MCC: failed to initialize DeviceManager");

        LOG_DEBUG("MCC: initialized DeviceManager");

        if (!Module::Initialize())
        {
            LOG_ERROR("MCC: failed to initialize Module");
			MessageBox(nullptr, "MCC: failed to initialize Module", "Error", MB_OK);
            return false;
        }

        LOG_DEBUG("MCC: initialized Module");

        if (!Splitscreen::Initialize())
        {
            LOG_ERROR("MCC: failed to initialize Splitscreen");
			MessageBox(nullptr, "MCC: failed to initialize Splitscreen", "Error", MB_OK);
            return false;
        }

        LOG_DEBUG("MCC: initialized Splitscreen");

		////Ask user if they want to enable network
  //      if (MessageBox(nullptr, "Would you like to enable network?", "Network", MB_YESNO) == IDYES)
  //      {
  //          if (!Network::Initialize())
  //          {
  //              MessageBox(nullptr, "MCC: failed to initialize Network", "Error", MB_OK);
  //              return false;
  //          }
  //      }

        LOG_INFO("MCC: initialized successfully");

        return true;
    }
}
