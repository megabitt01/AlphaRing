#pragma once

#include "Tables.h"

// The offsets (lib/game/inc/<version>/offset_*.h) are written for one MCC build. A module of that build
// uses them as they are, with no scanning. A module of any other build - an MCC update - gets them looked up
// from byte patterns as it loads; whatever isn't found is marked so (Offset::found), and the hooks, patches
// and features that use it stay off (Feature, EntryFeature) instead of hooking the wrong code.
namespace AlphaRing::Offsets {
    // As a module loads (module: MCC::Module::eModule, or kModuleMCC for MCC's executable): true for the known
    // build, where there's nothing to do. For another build, looks up the module's offsets the first time (later
    // loads are the same file, its offsets relative to its base) and logs each one, then returns false.
    bool Prepare(int module, __int64 hModule);
}
