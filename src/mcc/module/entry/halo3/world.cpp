#include "halo3.h"

#include "common.h"

#include "mcc/CGameGlobal.h"
#include "mcc/spawn/Command.h"

#include <queue>
#include <mutex>

namespace Halo3::Entry::World {
    std::mutex tasks_mutex;
    std::queue<std::function<void()>> tasks;

    void AddTask(const std::function<void()>& func) {
        std::unique_lock<std::mutex> lock(tasks_mutex);
        tasks.push(func);
    }

    void ExecuteTask() {
        std::function<void()> func = nullptr;

        std::unique_lock<std::mutex> lock(tasks_mutex);
        if (!tasks.empty()) { func = tasks.front();tasks.pop();}
        lock.unlock();

        if (func != nullptr) func();
    }

    void Prologue() {
        MCC::Command::Tick(CGameGlobal::Halo3); // spawning's tasks due on this tick (mcc/spawn/Command.h)
        ExecuteTask();
    }

    void Epilogue() {

    }

    Halo3Entry(entry, OFFSET_HALO3_PF_WORLD, void, detour) {
        Prologue();
        ((detour_t )entry.m_pOriginal)();
        Epilogue();
    }
}

// Spawning's engine calls must run on this (the simulation) thread (mcc/spawn/Command.h).
static const bool s_registered_scheduler =
    (MCC::Command::RegisterScheduler(CGameGlobal::Halo3, Halo3::Entry::World::AddTask), true);
