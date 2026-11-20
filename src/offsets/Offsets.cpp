#include "Offsets.h"

#include <chrono>

#include "log/DebugFlags.h"
#include "log/Log.h"

namespace AlphaRing::Offsets {
    namespace {
        bool s_resolved[kModuleCount]; // looked up already: later loads are the same file

        void Resolve(const ModuleTable& table, __int64 hModule) {
            auto start = std::chrono::steady_clock::now();
            auto results = Scan(table, (const unsigned char*)hModule);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

            size_t found = 0;
            for (size_t i = 0; i < table.count; ++i) {
                auto& entry = table.offsets[i];
                auto& result = results[i];
                entry.offset->value = result.value; // 0 unless found
                if (entry.offset->found()) {
                    ++found;
                    LOG_INFO("[Offsets] {}: {} at {:#x} (was {:#x})", table.name, entry.offset->name, result.value,
                             entry.known);
                } else {
                    LOG_WARNING("[Offsets] {}: {} {} - what uses it stays off", table.name, entry.offset->name,
                                ResultName(result.result));
                }
            }
            LOG_WARNING("[Offsets] {}: {} of {} offsets found ({} ms)", table.name, found, table.count, ms);
        }
    }

    bool Prepare(int module, __int64 hModule) {
        if (module < 0 || module >= kModuleCount || hModule == 0) return true;
        auto& table = kTables[module];
        ModuleBuild build{};
        ReadBuild((const unsigned char*)hModule, build);
        bool known = build == table.build;
        if (known && !DebugFlags::g_forceOffsetLookup) return true;
        if (!s_resolved[module]) {
            s_resolved[module] = true;
            LOG_WARNING("[Offsets] {}: {} (timestamp {:#x}, size {:#x}; offsets are for {:#x}, {:#x}) - "
                        "looking them up from patterns", table.name, known ? "lookup forced" : "unknown build",
                        build.timestamp, build.image_size, table.build.timestamp, table.build.image_size);
            Resolve(table, hModule);
        }
        return false;
    }
}
