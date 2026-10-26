#include "common.h"

#include "input/Input.h"
#include "hook/Hook.h"
#include "render/Render.h"
#include "mcc/mcc.h"
#include "mcc/settings/MenuSettings.h"

static bool Initialize() {
    bool result;

    result = AlphaRing::Log::Init();

    assertm(result, "failed to initialize log");

    result = AlphaRing::Hook::Initialize();

    // false means an MCC build the offsets weren't written for (e.g. after an
    // update). Install nothing and let the game run unmodified: assertm stays
    // live in Release builds (common.h), so asserting here would stop MCC.
    if (!result) {
        LOG_ERROR("AlphaRing disabled: unsupported MCC build, the game runs unmodified.");
        return false;
    }

    LOG_INFO("Initialized hook.");

    result = AlphaRing::Filesystem::Init();

    assertm(result, "failed to initialize filesystem");

	LOG_INFO("Initialized filesystem.");

    result = AlphaRing::Input::Init();

    assertm(result, "failed to initialize input");

	LOG_INFO("Initialized input.");

    // after Input::Init, which loads the cfg the controller profiles are seeded from
    AlphaRing::MenuSettings::Initialize();

    result = AlphaRing::Render::Initialize();

    assertm(result, "failed to initialize render");

	LOG_INFO("Initialized render.");

    result = MCC::Initialize();

    assertm(result, "failed to initialize mcc");

	LOG_INFO("Initialized mcc.");

    LOG_INFO("AlphaRing initialization complete.");

    return true;
}

static bool Shutdown() {
    LOG_INFO("Shutting down");

    AlphaRing::Filesystem::Shutdown();
    AlphaRing::Input::Shutdown();
    AlphaRing::Hook::Shutdown();
    AlphaRing::Log::Shutdown();

    return true;
}

BOOL APIENTRY DllMain(HANDLE handle, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)Initialize, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr)
            return Shutdown();
    }

    return true;
}