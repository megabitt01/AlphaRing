#pragma once

#include <cstddef>
#include <vector>

#include "Offset.h"

struct _IMAGE_NT_HEADERS64;

// Finding offsets from byte patterns (the tables tools/offsets/gen_patterns.py writes to
// lib/game/inc/<version>/patterns_*.inc). No hooks or logging here: offset_test.exe runs the same code
// over the game's files that the DLL runs over the loaded modules.
namespace AlphaRing::Offsets {
    // How a pattern leads to its offset. bytes: "48 8B 05 ?? ?? ?? ??" (?? any byte).
    //   At:  the match plus delta.
    //   Rel: the rel32 at match + field (a call's or a RIP-relative operand's), counted from match + next
    //        (the end of that instruction), plus delta.
    struct Pattern {
        const char* bytes;
        int field; // -1: At
        int next;
        __int64 delta;
    };

    constexpr Pattern At(const char* bytes, __int64 delta) { return {bytes, -1, 0, delta}; }
    constexpr Pattern Rel(const char* bytes, int field, int next, __int64 addend) { return {bytes, field, next, addend}; }

    constexpr int kMaxPatterns = 2;

    // Code offsets must land in executable sections, data offsets outside them.
    enum Kind { Code, Data };

    struct OffsetPatterns {
        Offset* offset;
        __int64 known; // its value in the headers' build
        Kind kind;
        Pattern patterns[kMaxPatterns];
    };

    // A module build by its PE header.
    struct ModuleBuild {
        unsigned timestamp;
        unsigned image_size;

        bool operator==(const ModuleBuild& other) const {
            return timestamp == other.timestamp && image_size == other.image_size;
        }
    };

    struct ModuleTable {
        const char* name;
        ModuleBuild build; // the build the offsets were written for
        const OffsetPatterns* offsets;
        size_t count;
    };

    enum class Result {
        Found,     // every pattern that matched once agrees
        NoPattern, // none generated: known build only
        NotFound,  // no pattern matched
        Ambiguous, // matched, but never just once
        Conflict,  // unique matches disagree, or the address isn't the kind it should be
    };

    struct Resolution {
        __int64 value = 0;
        Result result = Result::NotFound;
        int patterns = 0; // the offset's
        int agreed = 0;   // of them, matched once and led to value
    };

    const char* ResultName(Result result);

    // The 64-bit PE headers at `base` (a mapped image or a file), or null.
    const _IMAGE_NT_HEADERS64* NtHeaders(const unsigned char* base);

    // The build of the image mapped at `base` (false: not a PE image).
    bool ReadBuild(const unsigned char* base, ModuleBuild& build);

    // Looks up every offset of `table` in the image mapped at `base` - a loaded module, or a file laid out
    // the way the loader lays it out - in one pass over its executable sections. Leaves the offsets alone.
    std::vector<Resolution> Scan(const ModuleTable& table, const unsigned char* base);
}
