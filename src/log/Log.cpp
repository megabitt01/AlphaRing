#include "Log.h"

#include "spdlog.h"
#include "sinks/stdout_color_sinks.h"
#include "sinks/basic_file_sink.h"

#include "common.h"

#include <Windows.h>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <vector>

namespace AlphaRing::Log {
    std::shared_ptr<spdlog::logger> default_logger;

    static LPTOP_LEVEL_EXCEPTION_FILTER previous_exception_filter = nullptr;

    // Real console screen buffer, obtained once up front. Used directly by spdlog's
    // console sink and by the stdio tee below, independent of whatever stdout/stderr
    // get redirected to later.
    static HANDLE console_handle = nullptr;

    // Everything written through the CRT's stdout/stderr (raw printf/fprintf/std::cout,
    // the CRT's own assert() diagnostics, third-party library output, etc.) is captured
    // by redirecting those streams into this pipe instead of the console directly.
    static HANDLE stdout_pipe_read = nullptr;
    static HANDLE stdout_pipe_write = nullptr;
    static HANDLE tee_thread = nullptr;
    static FILE* raw_log_file = nullptr;

    static std::string LogFilePath() {
        return AlphaRing::Filesystem::ModuleFile("alpha_ring_info.log");
    }

    // Last-resort write used for crashes and assertion failures: opens and appends to
    // the log file directly and synchronously, bypassing the logger and the stdio tee
    // thread entirely, so the reason survives even if those are stuck or already dead.
    static void WriteRawLine(const std::string& line) {
        OutputDebugStringA(line.c_str());
        OutputDebugStringA("\n");

        FILE* file = _fsopen(LogFilePath().c_str(), "a", _SH_DENYNO);
        if (file != nullptr) {
            fprintf(file, "%s\n", line.c_str());
            fclose(file);
        }
    }

    void AssertFailure(const char* expr, const char* msg, const char* file, int line) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "Assertion failed: %s (%s) at %s:%d", msg, expr, file, line);

        if (default_logger) {
            default_logger->critical(buffer);
            default_logger->flush();
        }

        // Always written directly too: default_logger's own file sink can't be trusted
        // to have survived if this assert is what's about to crash the process.
        WriteRawLine(buffer);
    }

    static const char* ExceptionCodeName(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
            case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
            case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
            default: return "UNKNOWN";
        }
    }

    void DescribeAddress(const void* address, char* out, size_t size) {
        HMODULE module = nullptr;
        char path[MAX_PATH]{};
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(address), &module) ||
            GetModuleFileNameA(module, path, MAX_PATH) == 0) {
            snprintf(out, size, "unknown module");
            return;
        }
        const char* name = path;
        for (const char* c = path; *c; ++c)
            if (*c == '\\' || *c == '/') name = c + 1;
        snprintf(out, size, "%s+0x%llX", name,
                 (unsigned long long)(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    }

    static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* info) {
        char where[MAX_PATH + 32];
        DescribeAddress(info->ExceptionRecord->ExceptionAddress, where, sizeof(where));

        // An access violation also says what it touched: [0] 0 read / 1 write / 8 execute, [1] the address.
        char access[96] = "";
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            info->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR op = info->ExceptionRecord->ExceptionInformation[0];
            snprintf(access, sizeof(access), ", %s 0x%llX",
                     op == 0 ? "reading" : op == 1 ? "writing" : op == 8 ? "executing" : "accessing",
                     (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
        }

        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "Unhandled exception 0x%08lX (%s) at address 0x%p (%s)%s",
            info->ExceptionRecord->ExceptionCode,
            ExceptionCodeName(info->ExceptionRecord->ExceptionCode),
            info->ExceptionRecord->ExceptionAddress, where, access);

        if (default_logger) {
            default_logger->critical(buffer);
            default_logger->flush();
        }

        WriteRawLine(buffer);

        return previous_exception_filter != nullptr
            ? previous_exception_filter(info)
            : EXCEPTION_CONTINUE_SEARCH;
    }

    // Drains stdout_pipe_read for the lifetime of the process: every chunk is mirrored
    // to the real console (so the pop-up terminal is unaffected) and appended verbatim
    // to alpha_ring_info.log, so the file always matches what the terminal showed.
    static DWORD WINAPI TeeThreadProc(LPVOID) {
        char buffer[4096];
        DWORD bytes_read = 0;

        while (ReadFile(stdout_pipe_read, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
            if (console_handle != nullptr && console_handle != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(console_handle, buffer, bytes_read, &written, nullptr);
            }

            if (raw_log_file != nullptr) {
                fwrite(buffer, 1, bytes_read, raw_log_file);
                fflush(raw_log_file);
            }
        }

        return 0;
    }

    // Redirects the CRT's stdout/stderr to stdout_pipe_write without disturbing the
    // process-wide STD_OUTPUT_HANDLE/STD_ERROR_HANDLE (spdlog's console sink is
    // constructed against the console handle directly, so it is unaffected by this).
    static void RedirectStdioToPipe() {
        HANDLE duplicate = nullptr;
        DuplicateHandle(GetCurrentProcess(), stdout_pipe_write, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS);

        int fd = _open_osfhandle(reinterpret_cast<intptr_t>(duplicate), _O_TEXT);
        _dup2(fd, _fileno(stdout));
        _dup2(fd, _fileno(stderr));
        _close(fd);

        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }

    // Best-effort throughout: main.cpp asserts on the result and asserts stay live in
    // Release (common.h), so a console, log file or pipe we can't get only narrows
    // where the log goes; it must never be what stops the game.
    bool Init() {
        // No console under Wine/Proton: it opens as a separate window that takes focus
        // from the game (gamescope), and closing it closes MCC.
        if (!AlphaRing::Hook::IsWine() && AllocConsole()) {
            freopen("CONIN$", "r", stdin);

            console_handle = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }

        std::vector<spdlog::sink_ptr> sinks;

        if (console_handle != nullptr && console_handle != INVALID_HANDLE_VALUE) {
            sinks.push_back(std::make_shared<spdlog::sinks::wincolor_sink<spdlog::details::console_mutex>>(
                console_handle, spdlog::color_mode::automatic));
        }

        // basic_file_sink throws when it can't open the file (read-only game folder,
        // antivirus lock); logging then carries on without the file.
        bool file_logging = false;
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(LogFilePath(), true));
            file_logging = true;
        } catch (const std::exception& e) {
            OutputDebugStringA("AlphaRing: file logging unavailable: ");
            OutputDebugStringA(e.what());
            OutputDebugStringA("\n");
        }

        default_logger = std::make_shared<spdlog::logger>("default", sinks.begin(), sinks.end());

        // Log everything and flush immediately after every message: if the game crashes
        // a moment later, nothing buffered should be lost.
        default_logger->set_level(spdlog::level::trace);
        default_logger->flush_on(spdlog::level::trace);

        spdlog::register_logger(default_logger);

        previous_exception_filter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);

        if (file_logging)
            LOG_INFO("Log initialized, writing to {}", LogFilePath());
        else
            LOG_WARNING("Log initialized, but {} could not be opened", LogFilePath());

        // File already exists (created/truncated by file_sink above), so this only appends.
        raw_log_file = _fsopen(LogFilePath().c_str(), "a", _SH_DENYNO);

        // Without the pipe, raw stdout/stderr output just isn't mirrored into the log.
        if (CreatePipe(&stdout_pipe_read, &stdout_pipe_write, nullptr, 0)) {
            RedirectStdioToPipe();

            tee_thread = CreateThread(nullptr, 0, TeeThreadProc, nullptr, 0, nullptr);
        }

        return true;
    }

    bool Shutdown() {
        if (default_logger) {
            default_logger->flush();
        }

        // Closes every remaining handle to the pipe's write end (stdout's, stderr's, and
        // the original from CreatePipe); once all are gone the tee thread's blocking
        // ReadFile returns and its loop exits on its own.
        fclose(stdout);
        fclose(stderr);

        if (stdout_pipe_write != nullptr) {
            CloseHandle(stdout_pipe_write);
            stdout_pipe_write = nullptr;
        }

        if (tee_thread != nullptr) {
            WaitForSingleObject(tee_thread, 2000);
            CloseHandle(tee_thread);
            tee_thread = nullptr;
        }

        if (stdout_pipe_read != nullptr) {
            CloseHandle(stdout_pipe_read);
            stdout_pipe_read = nullptr;
        }

        if (raw_log_file != nullptr) {
            fclose(raw_log_file);
            raw_log_file = nullptr;
        }

        fclose(stdin);

        if (console_handle != nullptr && console_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(console_handle);
            console_handle = nullptr;
        }

        FreeConsole();

        return true;
    }
}
