#pragma once

#include "Pattern.h"

namespace AlphaRing::Offsets {
    // In MCC::Module::eModule order: halo1, halo2, halo3, halo4, groundhog, halo3odst, haloreach, then MCC's executable.
    constexpr int kModuleCount = 8;
    constexpr int kModuleMCC = 7;

    extern const ModuleTable kTables[kModuleCount];
}
