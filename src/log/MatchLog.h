#pragma once

#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <string>

#include "Log.h"
#include "DebugFlags.h"

// [MatchStart] probe: how a match is set up - game_setup/game_restart, the
// player queries the game makes (who asks, for which player, what it gets) and
// the events Alpha Ring sends back. Gated by DebugFlags::g_matchStart.
//
// Every line is bounded: the per-player query lines are logged once per match
// (a match starts at each game_setup/game_restart) or when their answer
// changes, never per call, as these hooks run every frame.
// The format string must be a literal, the first argument.
#define MATCH_LOG(...) \
    do { if (AlphaRing::DebugFlags::g_matchStart) LOG_INFO("[MatchStart] " __VA_ARGS__); } while (0)

namespace AlphaRing::MatchLog {
    // Bumped by each game_setup/game_restart.
    inline std::atomic<long> g_match{0};

    inline void NewMatch() { ++g_match; }

    // True the first time it's called for `seen` in the current match.
    inline bool FirstThisMatch(std::atomic<long>& seen) {
        long match = g_match.load();
        return seen.exchange(match) != match;
    }

    // "groundhog.dll+0x1A2B3C" for a code address, e.g. a hook's _ReturnAddress().
    inline std::string Describe(const void* address) {
        char buffer[MAX_PATH + 32];
        AlphaRing::Log::DescribeAddress(address, buffer, sizeof(buffer));
        return buffer;
    }
}
