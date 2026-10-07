#include "Command.h"

#include "common.h"

#include "mcc/mcc.h"
#include "mcc/CGameEngine.h"
#include "mcc/CGameGlobal.h"
#include "mcc/CGameManager.h"
#include "mcc/module/Module.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace MCC::Command {
    static constexpr const char* kPrefix = "@ar ";
    static constexpr int kGames = MCC::Module::MODULE_MCC; // one slot per game module

    static std::unordered_map<std::string, Handler>& Handlers() {
        static std::unordered_map<std::string, Handler> handlers;
        return handlers;
    }

    void Register(const char* verb, Handler handler) {
        Handlers()[verb] = handler;
    }

    static Scheduler s_schedulers[kGames]; // constant-initialized, so safe to fill from static initializers

    void RegisterScheduler(int game, Scheduler scheduler) {
        if ((unsigned)game < kGames) s_schedulers[game] = scheduler;
    }

    // A task belongs to the map it was queued in: after another map loads it would act on the
    // new map's data, so it's dropped.
    bool Schedule(int game, const std::function<void()>& task) {
        if ((unsigned)game >= kGames || s_schedulers[game] == nullptr) return false;
        s_schedulers[game]([generation = CGameManager::load_generation(), task] {
            if (generation == CGameManager::load_generation()) task();
        });
        return true;
    }

    __int64 ModuleBase(int game) {
        auto p_module = MCC::Module::GetSubModule(game);
        return p_module ? p_module->info().hModule : 0;
    }

    bool Post(const char* format, ...) {
        auto p_engine = GameEngine();
        auto p_global = GameGlobal();
        if (p_engine == nullptr || p_global == nullptr || !MCC::IsInGame())
            return false;

        char text[1024];
        int written = snprintf(text, sizeof(text), "%s", kPrefix);
        va_list args;
        va_start(args, format);
        vsnprintf(text + written, sizeof(text) - written, format, args);
        va_end(args);

        int game = p_global->current_game;
        std::string command = text;
        if (!Schedule(game, [game, command] { Dispatch(game, command.c_str()); }))
            p_engine->execute_command("HS: %s", text); // reaches the console hook on the game thread
        return true;
    }

    static Args Tokenize(const char* text) {
        Args args;
        std::string current;
        bool quoted = false, has_token = false;
        for (const char* p = text; *p; ++p) {
            if (*p == '"') { quoted = !quoted; has_token = true; continue; }
            if (!quoted && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
                if (has_token) { args.push_back(current); current.clear(); has_token = false; }
                continue;
            }
            current += *p;
            has_token = true;
        }
        if (has_token) args.push_back(current);
        return args;
    }

    struct Change {
        char* at;
        std::vector<char> saved;
    };
    static thread_local std::vector<Change> t_changes;

    void RecordChange(void* at, size_t size) {
        t_changes.push_back({(char*)at, std::vector<char>((char*)at, (char*)at + size)});
    }

    void ForgetChange(void* at) {
        for (auto it = t_changes.rbegin(); it != t_changes.rend(); ++it)
            if (it->at == at) {
                t_changes.erase(std::next(it).base());
                return;
            }
    }

    // After a fault: the recorded changes, newest first.
    static void RestoreChanges() {
        for (auto it = t_changes.rbegin(); it != t_changes.rend(); ++it)
            memcpy(it->at, it->saved.data(), it->saved.size());
        t_changes.clear();
    }

    static int RecordFault(EXCEPTION_POINTERS* info, DWORD* code, void** address) {
        *code = info->ExceptionRecord->ExceptionCode;
        *address = info->ExceptionRecord->ExceptionAddress;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Engine calls made by handlers can fault on unexpected game state; keep the game alive.
    static bool CallHandler(Handler handler, int game, const Args& args, DWORD* code, void** address) {
        __try {
            handler(game, args);
            t_changes.clear(); // anything a handler recorded is back by now
            return true;
        } __except (RecordFault(GetExceptionInformation(), code, address)) {
            RestoreChanges();
            return false;
        }
    }

    bool Dispatch(int game, const char* text) {
        if (text == nullptr || strncmp(text, kPrefix, strlen(kPrefix)) != 0)
            return false;

        auto args = Tokenize(text + strlen(kPrefix));
        if (args.empty()) return true;

        auto it = Handlers().find(args[0]);
        if (it == Handlers().end()) {
            LOG_WARNING("Command: unknown '{}'", args[0]);
            return true;
        }

        DWORD code = 0;
        void* address = nullptr;
        if (!CallHandler(it->second, game, args, &code, &address))
            LOG_ERROR("Command: '{}' faulted ({:#x} at module+{:#x})", text + strlen(kPrefix), code,
                      (unsigned __int64)address - (unsigned __int64)ModuleBase(game));
        return true;
    }
}
