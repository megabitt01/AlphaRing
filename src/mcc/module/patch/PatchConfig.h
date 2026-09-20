#pragma once

#include <string>

// Persists Dev Tools patch checkbox states (keyed by module + patch name)
// across game restarts - the on-disk sibling of input/MenuConfig.h, which
// does the same thing for keybindings.
namespace AlphaRing::PatchConfig {
    void Load();

    // Returns true and fills out_value if a saved state exists for this
    // module/patch pair; returns false (out_value left untouched) otherwise.
    bool Get(const std::string& module, const std::string& patch, bool& out_value);

    // Records the state in memory and rewrites the config file immediately.
    void Set(const std::string& module, const std::string& patch, bool value);
}
