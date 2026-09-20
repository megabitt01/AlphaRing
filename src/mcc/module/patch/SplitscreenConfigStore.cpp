#include "SplitscreenConfigStore.h"

#include <fstream>
#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <filesystem>
#include <Windows.h>

#include "common.h"

namespace {
    std::map<std::string, float> g_states;
    bool g_loaded = false;
    std::atomic<AlphaRing::SplitscreenConfigStore::TwoPlayerLayout> g_twoPlayerLayout{
            AlphaRing::SplitscreenConfigStore::TwoPlayerLayout::TopBottom};
    std::atomic<unsigned> g_layoutGeneration{0};

    // Publishes the layout and bumps the generation only on an actual change,
    // so re-selecting the current layout never triggers a render-target rebuild.
    void PublishTwoPlayerLayout(AlphaRing::SplitscreenConfigStore::TwoPlayerLayout layout) {
        auto previous = g_twoPlayerLayout.exchange(layout, std::memory_order_acq_rel);
        if (previous != layout)
            g_layoutGeneration.fetch_add(1, std::memory_order_acq_rel);
    }

    // Flat, pre-resolved view of g_states for Apply() to walk. Rebuilt whenever
    // g_states changes, so the per-frame path never touches the map (no string
    // building, no allocation) - it just compares floats against live memory.
    struct TableWrite {
        int   index;       // 0 .. ENTRY_COUNT-1
        int   byte_offset; // 0/4/8/12 = x0/y0/x1/y1, 16 = resolution
        bool  is_int;      // resolution is stored as __int32, the rest as float
        float value;
    };
    std::vector<TableWrite> g_writes;

    struct LayoutEntry {
        float x0;
        float y0;
        float x1;
        float y1;
        __int32 resolution;
    };
    static_assert(sizeof(LayoutEntry) == AlphaRing::SplitscreenConfigStore::ENTRY_SIZE);

    constexpr LayoutEntry kTopBottom[2] = {
        {0.097395837f, 0.0f, 0.902604163f, 0.5f, 3},
        {0.097395837f, 0.5f, 0.902604163f, 1.0f, 3},
    };

    constexpr LayoutEntry kLeftRight[2] = {
        {0.0f, 0.0f, 0.5f, 1.0f, 3},
        {0.5f, 0.0f, 1.0f, 1.0f, 3},
    };

    // Stock three-player entries 12-14, read from haloreach.dll's .data (see
    // docs/REVERSE_ENGINEERING.md, "Saved preference vs. active layout").
    constexpr LayoutEntry kStock3P[3] = {
        {0.097395837f, 0.0f, 0.902604163f, 0.5f, 3},
        {0.0f,         0.5f, 0.5f,         1.0f, 2},
        {0.5f,         0.5f, 1.0f,         1.0f, 2},
    };

    // Three-player Left/Right: player 1 owns the whole left half, matching
    // two-player Left/Right, and players 2 and 3 split the right half (top,
    // bottom). Player 1's slot has the exact extent of a two-player Left/Right
    // slot, so it shares that slot's res=3 surface and HUD semantic; players 2
    // and 3 are stock quadrants and keep res=2.
    constexpr LayoutEntry kLeftRight3P[3] = {
        {0.0f, 0.0f, 0.5f, 1.0f, 3},
        {0.5f, 0.0f, 1.0f, 0.5f, 2},
        {0.5f, 0.5f, 1.0f, 1.0f, 2},
    };

    constexpr int TWO_PLAYER_FIRST_ENTRY   = 2 * AlphaRing::SplitscreenConfigStore::SLOT_COUNT;
    constexpr int THREE_PLAYER_FIRST_ENTRY = 3 * AlphaRing::SplitscreenConfigStore::SLOT_COUNT;

    // Legacy key: the layout selection used to overwrite the user's painter
    // debug choice here, so its saved value cannot be trusted. Dropped on load.
    constexpr const char* LEGACY_PAINTER_DEBUG_KEY = "disable_bars_debug";

    void WriteBytes(void* dst, const void* src, size_t size) {
        DWORD oldProtect;
        if (VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(dst, src, size);
            VirtualProtect(dst, size, oldProtect, &oldProtect);
        }
    }

    void RebuildWrites() {
        using namespace AlphaRing::SplitscreenConfigStore;

        g_writes.clear();

        for (const auto& [key, value] : g_states) {
            auto dot = key.find('.');
            if (dot == std::string::npos) continue;

            int index;
            try {
                index = std::stoi(key.substr(0, dot));
            } catch (...) {
                continue;
            }

            // index -1 holds non-table settings (disable_bars_debug) - not a
            // table entry, so it has no place in the per-frame write list.
            if (index < 0 || index >= ENTRY_COUNT) continue;

            std::string field = key.substr(dot + 1);

            if      (field == "x0")  g_writes.push_back({index,  0, false, value});
            else if (field == "y0")  g_writes.push_back({index,  4, false, value});
            else if (field == "x1")  g_writes.push_back({index,  8, false, value});
            else if (field == "y1")  g_writes.push_back({index, 12, false, value});
            else if (field == "res") g_writes.push_back({index, 16, true,  value});
        }
    }

    // Anchored to this DLL's own directory - see PatchConfig.cpp for why
    // (relative "./" paths aren't stable across the module's lifetime).
    const std::string& ConfigPath() {
        static std::string path = [] {
            char module_path[MAX_PATH]{};
            HMODULE hModule = nullptr;
            GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&ConfigPath), &hModule);
            GetModuleFileNameA(hModule, module_path, MAX_PATH);

            auto dir = std::filesystem::path(module_path).parent_path();
            return (dir / "alpha_ring_splitscreen.cfg").string();
        }();
        return path;
    }

    std::string Trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end   = s.find_last_not_of(" \t\r\n");
        return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
    }

    std::string MakeKey(int index, const char* field) {
        return std::to_string(index) + "." + field;
    }

    AlphaRing::SplitscreenConfigStore::TwoPlayerLayout ResolveTwoPlayerLayoutState() {
        using AlphaRing::SplitscreenConfigStore::TwoPlayerLayout;

        auto current = g_states.find(MakeKey(-1, "two_player_layout"));
        if (current != g_states.end())
            return current->second == (float)TwoPlayerLayout::LeftRight
                 ? TwoPlayerLayout::LeftRight
                 : TwoPlayerLayout::TopBottom;

        auto legacy = g_states.find(MakeKey(-1, "vertical_split"));
        if (legacy != g_states.end() && legacy->second != 0.0f)
            return TwoPlayerLayout::LeftRight;

        return TwoPlayerLayout::TopBottom;
    }

    void EraseEntryState(int index) {
        std::string prefix = std::to_string(index) + ".";
        for (auto it = g_states.begin(); it != g_states.end(); ) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) it = g_states.erase(it);
            else ++it;
        }
    }

    void SetEntryState(int index, const LayoutEntry& entry) {
        g_states[MakeKey(index, "x0")] = entry.x0;
        g_states[MakeKey(index, "y0")] = entry.y0;
        g_states[MakeKey(index, "x1")] = entry.x1;
        g_states[MakeKey(index, "y1")] = entry.y1;
        g_states[MakeKey(index, "res")] = (float)entry.resolution;
    }

    void SetLeftRightEntryStates() {
        for (int slot = 0; slot < 2; ++slot)
            SetEntryState(TWO_PLAYER_FIRST_ENTRY + slot, kLeftRight[slot]);
        for (int slot = 0; slot < 3; ++slot)
            SetEntryState(THREE_PLAYER_FIRST_ENTRY + slot, kLeftRight3P[slot]);
    }

    void EraseLeftRightEntryStates() {
        for (int slot = 0; slot < 2; ++slot)
            EraseEntryState(TWO_PLAYER_FIRST_ENTRY + slot);
        for (int slot = 0; slot < 3; ++slot)
            EraseEntryState(THREE_PLAYER_FIRST_ENTRY + slot);
    }

    void WriteFile() {
        std::ofstream ofs(ConfigPath());
        if (!ofs) return;

        ofs << "# AlphaRing Splitscreen Config Editor table - auto-generated by the game.\n"
               "# Only read at haloreach.dll's load time, not watched live.\n";

        for (const auto& [key, value] : g_states)
            ofs << key << "=" << value << "\n";
    }
}

namespace AlphaRing::SplitscreenConfigStore {
    void Load() {
        if (g_loaded) return;
        g_loaded = true;

        std::ifstream ifs(ConfigPath());
        if (!ifs.is_open()) return;

        std::string line;
        while (std::getline(ifs, line)) {
            auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            line = Trim(line);
            if (line.empty()) continue;

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = Trim(line.substr(0, eq));
            std::string val = Trim(line.substr(eq + 1));

            try {
                g_states[key] = std::stof(val);
            } catch (...) {}
        }

        bool migrated = g_states.erase(MakeKey(-1, LEGACY_PAINTER_DEBUG_KEY)) != 0;

        auto layout = ResolveTwoPlayerLayoutState();
        // The three-player entries are owned by the Left/Right layout. Bring a
        // saved preference up to the current placement: preferences saved before
        // three-player support hold only entries 8/9, and ones saved before the
        // 2026-09-16 placement change put player 1 on the right half.
        if (layout == TwoPlayerLayout::LeftRight) {
            bool current = true;
            for (int slot = 0; slot < 3 && current; ++slot) {
                const LayoutEntry& want = kLeftRight3P[slot];
                const int index = THREE_PLAYER_FIRST_ENTRY + slot;
                const std::pair<const char*, float> fields[] = {
                    {"x0", want.x0}, {"y0", want.y0}, {"x1", want.x1}, {"y1", want.y1},
                    {"res", (float)want.resolution},
                };
                for (const auto& [name, value] : fields) {
                    auto it = g_states.find(MakeKey(index, name));
                    if (it == g_states.end() || it->second != value) { current = false; break; }
                }
            }
            if (!current) {
                for (int slot = 0; slot < 3; ++slot)
                    SetEntryState(THREE_PLAYER_FIRST_ENTRY + slot, kLeftRight3P[slot]);
                migrated = true;
            }
        }

        g_twoPlayerLayout.store(layout, std::memory_order_release);
        RebuildWrites();
        if (migrated) WriteFile();

        LOG_INFO("SplitscreenConfigStore::Load: loaded {} saved value(s), {} table write(s)",
                 g_states.size(), g_writes.size());
    }

    bool Get(int index, const char* field, float& out_value) {
        auto it = g_states.find(MakeKey(index, field));
        if (it == g_states.end()) return false;
        out_value = it->second;
        return true;
    }

    void Set(int index, const char* field, float value) {
        g_states[MakeKey(index, field)] = value;
        RebuildWrites();
        WriteFile();
    }

    TwoPlayerLayout GetTwoPlayerLayout() {
        return g_twoPlayerLayout.load(std::memory_order_acquire);
    }

    ActiveLayout ResolveActiveLayout(int playerCount) {
        return (playerCount == 2 || playerCount == 3)
                       && GetTwoPlayerLayout() == TwoPlayerLayout::LeftRight
             ? ActiveLayout::LeftRight
             : ActiveLayout::Native;
    }

    bool UsesFullHeightLeftRightSlot(int playerCount, int slot) {
        if (ResolveActiveLayout(playerCount) != ActiveLayout::LeftRight) return false;
        if (slot < 0 || slot >= playerCount) return false;

        const LayoutEntry& entry = playerCount == 2 ? kLeftRight[slot] : kLeftRight3P[slot];
        return entry.resolution == 3;
    }

    void SetTwoPlayerLayout(TwoPlayerLayout layout, __int64 hModule) {
        g_states[MakeKey(-1, "two_player_layout")] = (float)layout;
        g_states.erase(MakeKey(-1, "vertical_split"));

        if (layout == TwoPlayerLayout::LeftRight) {
            SetLeftRightEntryStates();
        } else {
            // Do not persist the stock entries. Leaving them out of Apply's
            // write list lets Reach restore its own table on every level load
            // and lets the existing black-bar patches keep owning this shape.
            EraseLeftRightEntryStates();
        }

        RebuildWrites();
        WriteFile();

        if (layout == TwoPlayerLayout::LeftRight) {
            Apply(hModule);
        } else if (hModule != 0) {
            // Make the selection visible immediately instead of waiting for
            // Reach's next level-load reset. The caller re-applies any enabled
            // black-bar patch afterwards, since these stock bytes cover them.
            auto writeEntry = [hModule](int index, const LayoutEntry& entry) {
                auto p_entry = (unsigned char*)(hModule + TABLE_OFFSET
                                                + (__int64)index * ENTRY_SIZE);
                WriteBytes(p_entry, &entry, sizeof(LayoutEntry));
            };
            for (int slot = 0; slot < 2; ++slot)
                writeEntry(TWO_PLAYER_FIRST_ENTRY + slot, kTopBottom[slot]);
            for (int slot = 0; slot < 3; ++slot)
                writeEntry(THREE_PLAYER_FIRST_ENTRY + slot, kStock3P[slot]);
        }

        // Publish the mode only after its geometry has been applied. HUD hot
        // paths read this atomic rather than allocating a config-map key every
        // time UpdatePlayerHudView runs.
        PublishTwoPlayerLayout(layout);
    }

    unsigned GetLayoutGeneration() {
        return g_layoutGeneration.load(std::memory_order_acquire);
    }

    void ClearEntry(int index) {
        EraseEntryState(index);
        RebuildWrites();
        WriteFile();
    }

    void Apply(__int64 hModule) {
        if (hModule == 0 || g_writes.empty()) return;

        for (const auto& w : g_writes) {
            auto p_field = (unsigned char*)(hModule + TABLE_OFFSET
                                            + (__int64)w.index * ENTRY_SIZE
                                            + w.byte_offset);

            if (w.is_int) {
                __int32 desired = (__int32)w.value;
                __int32 current;
                memcpy(&current, p_field, sizeof(current));
                if (current != desired)
                    WriteBytes(p_field, &desired, sizeof(desired));
            } else {
                // Bitwise compare rather than float ==, so a stored NaN still
                // settles instead of being rewritten every single frame.
                float current;
                memcpy(&current, p_field, sizeof(current));
                if (memcmp(&current, &w.value, sizeof(current)) != 0)
                    WriteBytes(p_field, &w.value, sizeof(w.value));
            }
        }
    }
}
