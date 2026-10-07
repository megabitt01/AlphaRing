#include "halo3odst.h"

#include "common.h"

#include "mcc/CGameGlobal.h"
#include "mcc/spawn/Command.h"
#include <queue>
#include <mutex>

namespace Halo3ODST::Entry::World {
    std::mutex tasks_mutex;
    std::queue<std::function<void()>> tasks;

    void ExecuteTask() {
        std::function<void()> func = nullptr;

        std::unique_lock<std::mutex> lock(tasks_mutex);
        if (!tasks.empty()) { func = tasks.front();tasks.pop();}
        lock.unlock();

        if (func != nullptr) func();
    }

    void AddTask(const std::function<void()>& func) {
        std::unique_lock<std::mutex> lock(tasks_mutex);
        tasks.push(func);
    }

    void Prologue() {
        ExecuteTask();
    }

    void Epilogue() {

    }

    Halo3ODSTEntry(entry, OFFSET_HALO3ODST_PF_WORLD, void, detour) {
        Prologue();
        ((detour_t)entry.m_pOriginal)();
        Epilogue();
    }
}

// Spawning's engine calls must run on this (the simulation) thread (mcc/spawn/Command.h).
static const bool s_registered_scheduler =
    (MCC::Command::RegisterScheduler(CGameGlobal::Halo3ODST, Halo3ODST::Entry::World::AddTask), true);
