#pragma once

#include <spdlog.h>

namespace AlphaRing::Log {
    extern std::shared_ptr<spdlog::logger> default_logger;

    bool Init();
    bool Shutdown();

    // AlphaRing opens no console, but one was reported open with the game: if MCC has one, logs what it is
    // (its title and the processes attached to it) and detaches from it, which closes it unless another
    // process (a terminal MCC was started from) shares it. `when` names the moment checked, for the log.
    void DropConsole(const char* when);

    // Logs an assertion failure (reason, expression, and source location) before the
    // process aborts, so the cause survives in alpha_ring_info.log even without a debugger.
    void AssertFailure(const char* expr, const char* msg, const char* file, int line);

    // "mcc-win64-shipping.exe+0x1A2B3C" for a code address, into `out`.
    void DescribeAddress(const void* address, char* out, size_t size);
}

// Null-guarded so a log call can never be what crashes the game, e.g. one made
// before Log::Init() has created the logger.
#define LOG_INFO(...) do { if (AlphaRing::Log::default_logger) AlphaRing::Log::default_logger->info(__VA_ARGS__); } while (0)
#define LOG_ERROR(...) do { if (AlphaRing::Log::default_logger) AlphaRing::Log::default_logger->error(__VA_ARGS__); } while (0)
#define LOG_WARNING(...) do { if (AlphaRing::Log::default_logger) AlphaRing::Log::default_logger->warn(__VA_ARGS__); } while (0)
#define LOG_DEBUG(...) do { if (AlphaRing::Log::default_logger) AlphaRing::Log::default_logger->debug(__VA_ARGS__); } while (0)
#define LOG_CRITICAL(...) do { if (AlphaRing::Log::default_logger) AlphaRing::Log::default_logger->critical(__VA_ARGS__); } while (0)
