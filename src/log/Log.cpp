#include "Log.h"

#include "spdlog.h"
#include "sinks/stdout_color_sinks.h"
#include "sinks/basic_file_sink.h"

#include "common.h"

#include <Windows.h>
#include <filesystem>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <share.h>

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

    // Resolves alpha_ring_info.log next to this DLL, regardless of the process's
    // current working directory (which the host game controls, not us).
    static std::string LogFilePath() {
        char path[MAX_PATH]{};
        HMODULE module = nullptr;

        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&LogFilePath),
            &module);

        if (module == nullptr || GetModuleFileNameA(module, path, MAX_PATH) == 0) {
            return "alpha_ring_info.log";
        }

        return (std::filesystem::path(path).parent_path() / "alpha_ring_info.log").string();
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

    static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* info) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "Unhandled exception 0x%08lX (%s) at address 0x%p",
            info->ExceptionRecord->ExceptionCode,
            ExceptionCodeName(info->ExceptionRecord->ExceptionCode),
            info->ExceptionRecord->ExceptionAddress);

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

    bool Init() {
        bool result = AllocConsole();
        assertm(result, "failed to allocate console");

        freopen("CONIN$", "r", stdin);

        console_handle = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

        auto console_sink = std::make_shared<spdlog::sinks::wincolor_sink<spdlog::details::console_mutex>>(
            console_handle, spdlog::color_mode::automatic);
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LogFilePath(), true);

        default_logger = std::make_shared<spdlog::logger>(
            "default",
            spdlog::sinks_init_list{console_sink, file_sink}
        );

        // Log everything and flush immediately after every message: if the game crashes
        // a moment later, nothing buffered should be lost.
        default_logger->set_level(spdlog::level::trace);
        default_logger->flush_on(spdlog::level::trace);

        spdlog::register_logger(default_logger);

        previous_exception_filter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);

        LOG_INFO("Log initialized, writing to {}", LogFilePath());

        // File already exists (created/truncated by file_sink above), so this only appends.
        raw_log_file = _fsopen(LogFilePath().c_str(), "a", _SH_DENYNO);

        result = CreatePipe(&stdout_pipe_read, &stdout_pipe_write, nullptr, 0);
        assertm(result, "failed to create stdout pipe");

        RedirectStdioToPipe();

        tee_thread = CreateThread(nullptr, 0, TeeThreadProc, nullptr, 0, nullptr);

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
