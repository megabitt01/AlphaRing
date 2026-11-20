#include "Hook.h"

#include "common.h"

#include "utils.h"
#include "MinHook.h"

#include <cstring>

#include "offset_mcc.h"
#include "offsets/Offsets.h"

namespace AlphaRing::Hook {
    enum eDistro {
        Steam,
        WindowsStore,
        None
    };

    static eDistro distro;
    static __int64 hModule;
    static FileVersion version;
    static bool ws_offsets; // the Windows Store 1.3498 build: its own offsets (OFFSET_MCC_WS_*)

    bool IsWS() {
        return distro == WindowsStore;
    }

    static const AlphaRing::Offset& Pick(const AlphaRing::Offset& offset_steam, const AlphaRing::Offset& offset_ws) {
        return ws_offsets ? offset_ws : offset_steam;
    }

    // An MCC build we look offsets up in has to have every one of the executable's, but these, which nothing uses
    // yet. (Not the XUID lookup: without its hook a split-screen player changing team freezes the game.)
    static bool Optional(const AlphaRing::Offset& offset) {
        for (const AlphaRing::Offset* optional : {&OFFSET_MCC_PF_GET_PROFILE, &OFFSET_MCC_PV_WINDOWFOCUSED})
            if (&offset == optional) return true;
        return strncmp(offset.name, "OFFSET_MCC_WS_", 14) == 0; // the Windows Store build's
    }

    // Detects Wine/Proton by probing for a function ntdll only exports under Wine.
    // Cheap, reliable, and avoids depending on env vars Proton may or may not set.
    bool IsWine() {
        static const bool is_wine = [] {
            HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
            return hNtdll != nullptr && GetProcAddress(hNtdll, "wine_get_version") != nullptr;
        }();

        return is_wine;
    }

    bool Initialize() {
        bool result;
        char buffer[1024];

        result = MH_Initialize() == MH_OK;

        assertm(result, "failed to initialize minhook");

		LOG_INFO("Initializing AlphaRing...");
        LOG_INFO("Running under Wine/Proton: {}", IsWine());
        LOG_INFO("Created by WinterSquire, updated by thejackbitt\n");
		LOG_WARNING("** CURRENTLY IN BETA - STRAP IN FOR A BUMPY RIDE **");
		LOG_WARNING(" == This version only supports the steam version of the game ==");

        if ((hModule = (__int64)GetModuleHandleA("MCC-Win64-Shipping.exe")) != 0) {
            distro = Steam;
        } else if ((hModule = (__int64)GetModuleHandleA("MCCWinStore-Win64-Shipping.exe")) != 0) {
			distro = WindowsStore;
        } else {
            distro = None;
        }

        assertm(distro != None, "failed to get distro type");

        LOG_INFO("Game Version[{}]: {}", IsWS() ? "Windows Store" : "Steam", GAME_VERSION);

        version = FileVersion(hModule);
        if (distro == WindowsStore && version == FileVersion::fromString("1.3498.0.0")) {
            ws_offsets = true;
            return true;
        }

        // The build the offsets were written for uses them as they are; any other (an MCC update) has them looked
        // up, as each game module will.
        if (AlphaRing::Offsets::Prepare(AlphaRing::Offsets::kModuleMCC, hModule)) return true;

        // Use logging instead of MessageBox for Wine/Proton compatibility
        LOG_WARNING("MCC {} isn't the build AlphaRing's offsets are for ({}): pattern mode", version.toString(),
                    GAME_VERSION);
        auto& table = AlphaRing::Offsets::kTables[AlphaRing::Offsets::kModuleMCC];
        for (size_t i = 0; i < table.count; ++i) {
            auto& offset = *table.offsets[i].offset;
            if (!offset.found() && !Optional(offset)) {
                LOG_ERROR("MCC {}: {} not found", version.toString(), offset.name);
                return false;
            }
        }

        return true;
    }

    bool Shutdown() {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        return true;
    }

    bool Detour(const std::initializer_list<Detour_t> &hooks) {
        for (auto &hook : hooks) {
            if (MH_CreateHook(hook.pTarget, hook.detour, hook.ppOriginal) != MH_OK ||
                MH_EnableHook(hook.pTarget) != MH_OK)
                return false;
        }
        return true;
    }

    bool Detour(const std::initializer_list<DetourOffset>& hooks) {
        void* pTarget;

        for (auto &hook : hooks) {
            auto& offset = Pick(hook.offset_steam, hook.offset_ws);
            if (!offset.found()) { // an MCC build without it (src/offsets): what it's for stays off
                LOG_WARNING("[Offsets] not hooking {}: it isn't in this build", offset.name);
                continue;
            }
            if ((pTarget = (LPVOID) (hModule + offset)),
                    MH_CreateHook(pTarget, hook.detour, hook.ppOriginal) != MH_OK ||
                    MH_EnableHook(pTarget) != MH_OK)
                return false;
        }
        return true;
    }

    bool Detour(const char *module_name, const std::initializer_list<DetourFunction>& hooks) {
        auto hModule = GetModuleHandleA(module_name);

        if (hModule == nullptr)
            return false;

        for (auto &hook : hooks) {
            auto pTarget = GetProcAddress(hModule, hook.function_name);

            if (pTarget == nullptr)
                return false;

            if (MH_CreateHook(pTarget, hook.detour, hook.ppOriginal) != MH_OK ||
                MH_EnableHook(pTarget) != MH_OK)
                return false;
        }

        return true;
    }

    void Offset(const std::initializer_list<FunctionOffset> &offsets) {
        int patch_count = 0;
        for (auto &offset : offsets) {
            if (offset.ppFunction == nullptr) continue;
            auto& picked = Pick(offset.offset_steam, offset.offset_ws);
            if (!picked.found()) {
                LOG_WARNING("[Offsets] {} isn't in this build", picked.name);
                *offset.ppFunction = nullptr;
                continue;
            }
            *offset.ppFunction = (void*)(hModule + picked);
        }
    }

    bool Patch(const char *module_name, const std::initializer_list<PatchFunction> &patches) {
        auto hModule = GetModuleHandleA(module_name);

        if (hModule == nullptr)
            return false;

        for (auto &patch : patches) {
            auto pTarget = (LPVOID)GetProcAddress(hModule, patch.function_name);

            if (pTarget == nullptr)
                return false;

            DWORD dwOldProtect;
            VirtualProtect(pTarget, patch.size, PAGE_EXECUTE_READWRITE, &dwOldProtect);
            memcpy(pTarget, patch.patch, patch.size);
            VirtualProtect(pTarget, patch.size, dwOldProtect, &dwOldProtect);
        }

        return true;
    }
}
