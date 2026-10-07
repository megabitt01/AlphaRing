#pragma once

#include <functional>
#include <string>
#include <vector>

// Game-thread execution for AlphaRing features (spawning).
//
// Engines with a simulation-tick hook register a Scheduler that runs a task on that thread
// (Halo 3 and ODST, whose console scripts compile on another thread). For the others, MCC
// hands every "HS: <script>" string it receives through the engine interface to the game's
// console-script compiler on the game thread; that compiler is hooked
// (module/entry/*/console.cpp) and scripts starting with "@ar " are dispatched to the fixed
// handlers registered here instead of being compiled. Only AlphaRing's own menus post commands
// (the overlay's Spawn window and the players' spawn menus).
namespace MCC::Command {
    using Args = std::vector<std::string>;
    using Handler = void (*)(int game, const Args& args);

    // Queue a command for the current game's thread. Returns false when no game is running.
    bool Post(const char* format, ...);

    // Register a handler for "@ar <verb> ...". Handlers run on the game thread.
    void Register(const char* verb, Handler handler);

    // Register how to run a task on `game`'s simulation thread.
    using Scheduler = void (*)(const std::function<void()>& task);
    void RegisterScheduler(int game, Scheduler scheduler);

    // Run `task` on `game`'s simulation thread at a coming tick (the queue runs one task a tick),
    // unless another map has loaded by then. False without a scheduler.
    bool Schedule(int game, const std::function<void()>& task);

    // Run `task` on the `ticks`th simulation tick after this one, beside the queue: every task due
    // on a tick runs then. Dropped if another map loads first. From `game`'s simulation thread;
    // false without a scheduler (its tick hook calls Tick).
    bool ScheduleAfter(int game, int ticks, const std::function<void()>& task);
    // Each simulation tick of `game`, from its tick hook (module/entry/halo3/world.cpp, halo3odst).
    void Tick(int game);

    // Game data a handler changes for the length of a call into the game. A fault in the game is
    // a structured exception, which skips C++ destructors on its way to the dispatcher, so a
    // handler records such a change first (`size` bytes at `at`, saved as they are now) and
    // forgets it once it's put back or handed to a later task; after a fault the dispatcher puts
    // back whatever is still recorded. Game thread only.
    void RecordChange(void* at, size_t size);
    void ForgetChange(void* at);

    // Called by the console hooks; true when `text` was an AlphaRing command.
    bool Dispatch(int game, const char* text);

    // The loaded module base of `game` (0 when not loaded).
    __int64 ModuleBase(int game);
}
