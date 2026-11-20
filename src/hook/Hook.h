#pragma once

#include <initializer_list>

#include "Offset.h"

#define DefDetourFunction(return_type, call_type, name, ...) \
    static return_type (call_type *ppOriginal_##name)(__VA_ARGS__); \
    static return_type call_type name(__VA_ARGS__)

namespace AlphaRing::Hook {
    struct DetourFunction {
        const char* function_name;
        void** ppOriginal;
        void* detour;
    };

    // MCC's executable: the Steam build's offset (looked up from patterns in an unknown build), and the Windows
    // Store 1.3498 build's.
    struct DetourOffset {
        const AlphaRing::Offset& offset_steam;
        const AlphaRing::Offset& offset_ws;
        void* detour;
        void** ppOriginal;
    };

    struct FunctionOffset {
        const AlphaRing::Offset& offset_steam;
        const AlphaRing::Offset& offset_ws;
        void** ppFunction;
    };

    struct PatchFunction {
        const char* function_name;
        const char* patch;
        size_t size;
    };

    struct Detour_t {
        void* pTarget;
        void* detour;
        void** ppOriginal;
    };

    bool Initialize();
    bool Shutdown();

    bool IsWS();
    bool IsWine();

    bool Detour(const std::initializer_list<Detour_t>& hooks);
    // An offset this MCC build lacks (src/offsets) is logged and skipped; Offset leaves its pointer null.
    bool Detour(const std::initializer_list<DetourOffset>& hooks);
    bool Detour(const char* module_name, const std::initializer_list<DetourFunction>& hooks);
    void Offset(const std::initializer_list<FunctionOffset>& offsets);
    bool Patch(const char* module_name, const std::initializer_list<PatchFunction>& patches);
}