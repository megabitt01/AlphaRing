#include "halo3.h"
#include "offset_halo3odst.h"

#include "common.h"
#include "mcc/module/CModule.h"
#include "mcc/module/patch/CPatch.h"

#include <Windows.h>

#include <cstring>
#include <vector>

// Split screen: Halo 3 and ODST give every skinned object drawn in a frame - in any view, and again for shadows -
// its bone matrices from one 0x29000-byte pool (nodes x 0x30 + 0x44 bytes an object), a smaller pool than Reach's
// (haloreach/skinning.cpp). Once bodies pile up, four views use it all, and the later views' Spartans come out
// black, over-bright or not at all. The allocator returns -1 when it's full, and that object is drawn without its
// bones. The pool is moved to a 4 MB buffer of our own: its users are pointed at the buffer and the allocator's limit
// raised. A handle is the plain byte offset, masked to 28 bits where it's read, so nothing else changes.
//
// Both games share the engine. The two DLLs are loaded side by side at the main menu, so each keeps its own writes.
namespace Halo3::Entry::Skinning {
    constexpr size_t kPoolSize = 0x400000;
    constexpr unsigned kStockSize = 0x29000;

    struct Write { unsigned char* at; unsigned char original[4]; int size; };

    struct Game {
        const char* name;
        __int64 pool_offset, allocate_offset;
        // Its users in MCC 1.3528: lea reg, [pool] each followed by `and reg, 0xFFFFFFF` (the handle), plus the
        // startup clear (lea rcx, [pool] for a memset). A build laid out differently is left alone; these checks are
        // also what keeps the fixed offsets from being trusted on another build.
        size_t users;
        std::vector<Write> writes;
        char* pool;
    };
    Game s_halo3{"Halo 3", OFFSET_HALO3_DAT_SKINNING_POOL, OFFSET_HALO3_PF_SKINNING_POOL_ALLOCATE, 6, {}, nullptr};
    Game s_odst{"ODST", OFFSET_HALO3ODST_DAT_SKINNING_POOL, OFFSET_HALO3ODST_PF_SKINNING_POOL_ALLOCATE, 5, {}, nullptr};

    Game* GameOf(int title) {
        if (title == MCC::Module::MODULE_HALO3) return &s_halo3;
        if (title == MCC::Module::MODULE_HALO3ODST) return &s_odst;
        return nullptr;
    }

    // The bytes are code, so the instruction cache is flushed after each write.
    bool Poke(Game& game, unsigned char* at, const void* bytes, int size) {
        Write w{at, {}, size};
        memcpy(w.original, at, size);
        if (!CPatch::apply(at, bytes, size)) return false;
        FlushInstructionCache(GetCurrentProcess(), at, size);
        game.writes.push_back(w);
        return true;
    }

    // Puts the module's bytes back, last write first (the allocator's limit, then the leas), so code never reaches
    // past the buffer its lea points at. A write that fails stops it there - the writes left, and the pool they may
    // still reach, stay for the next try.
    bool Restore(Game& game) {
        while (!game.writes.empty()) {
            const Write& w = game.writes.back();
            if (!CPatch::apply(w.at, w.original, w.size)) return false;
            FlushInstructionCache(GetCurrentProcess(), w.at, w.size);
            game.writes.pop_back();
        }
        if (game.pool) VirtualFree(game.pool, 0, MEM_RELEASE);
        game.pool = nullptr;
        return true;
    }

    // The module is going away: its bytes back if possible, and the writes forgotten either way (they'd point into
    // whatever is mapped there next). A pool the module's code may still reach is left allocated rather than freed.
    void Unload(int title) {
        auto game = GameOf(title);
        if (game == nullptr || Restore(*game)) return;
        LOG_WARNING("{}: skinning pool writes not undone at unload - the pool stays allocated", game->name);
        game->writes.clear();
        game->pool = nullptr;
    }

    // The module's PE headers (for its code sections and size), or nullptr if it isn't a 64-bit image.
    const IMAGE_NT_HEADERS64* NtHeaders(const unsigned char* module) {
        auto dos = (const IMAGE_DOS_HEADER*)module;
        if (module == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        auto nt = (const IMAGE_NT_HEADERS64*)(module + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return nullptr;
        return nt;
    }

    // The users reach the pool with 32-bit RIP-relative addresses, so the buffer has to sit within 2 GB of the
    // module: the first free range after it.
    char* AllocateAfter(unsigned char* module, size_t image_size) {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const uintptr_t granularity = info.dwAllocationGranularity;
        uintptr_t at = ((uintptr_t)module + image_size + granularity - 1) & ~(granularity - 1);
        const uintptr_t limit = (uintptr_t)module + 0x7FF00000 - kPoolSize;
        MEMORY_BASIC_INFORMATION mbi;
        while (at < limit && VirtualQuery((void*)at, &mbi, sizeof(mbi))) {
            uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (mbi.State == MEM_FREE && end - at >= kPoolSize)
                if (auto p = VirtualAlloc((void*)at, kPoolSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return (char*)p;
            at = (end + granularity - 1) & ~(granularity - 1);
        }
        return nullptr;
    }

    // and eax / r32 / r8d-r15d, 0xFFFFFFF
    bool IsHandleMask(const unsigned char* p) {
        static const unsigned char kMask[4] = {0xFF, 0xFF, 0xFF, 0x0F};
        return (p[0] == 0x25 && memcmp(p + 1, kMask, 4) == 0) ||
               (p[0] == 0x81 && (p[1] & 0xF8) == 0xE0 && memcmp(p + 2, kMask, 4) == 0) ||
               (p[0] == 0x41 && p[1] == 0x81 && (p[2] & 0xF8) == 0xE0 && memcmp(p + 3, kMask, 4) == 0);
    }

    void Enlarge(int title, __int64 hModule) {
        auto game = GameOf(title);
        if (game == nullptr) return;
        if (!Restore(*game)) {
            LOG_WARNING("{}: skinning pool not enlarged - an earlier enlargement couldn't be undone", game->name);
            return;
        }

        auto module = (unsigned char*)hModule;
        auto nt = NtHeaders(module);
        if (nt == nullptr) return;
        auto pool = module + game->pool_offset;
        auto allocate = module + game->allocate_offset;

        // The allocator's limit (cmp edx, 0x29000), and the handle it returns (and r9d, 0xFFFFFFF).
        unsigned char* limit = nullptr;
        bool handle = false;
        for (int i = 0; i < 0x60; ++i) {
            if (!limit && memcmp(allocate + i, "\x81\xFA", 2) == 0 && *(unsigned*)(allocate + i + 2) == kStockSize)
                limit = allocate + i + 2;
            handle = handle || IsHandleMask(allocate + i);
        }

        // Every lea reg, [rip + pool] in the code: the users, each followed by its handle's mask, and the clear.
        std::vector<unsigned char*> leas;
        size_t users = 0;
        auto section = IMAGE_FIRST_SECTION(nt);
        for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_CNT_CODE)) continue;
            auto begin = module + section->VirtualAddress, end = begin + section->Misc.VirtualSize - 14;
            for (auto p = begin; p < end; ++p)
                if ((p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8D && (p[2] & 0xC7) == 0x05 &&
                    p + 7 + *(int*)(p + 3) == pool) {
                    leas.push_back(p);
                    users += IsHandleMask(p + 7);
                }
        }
        if (!limit || !handle || users != game->users || leas.size() != game->users + 1) {
            LOG_WARNING("{}: skinning pool not enlarged - allocator {}, {} users, {} references", game->name,
                        limit && handle ? "found" : "not found", users, leas.size());
            return;
        }

        game->pool = AllocateAfter(module, nt->OptionalHeader.SizeOfImage);
        if (game->pool == nullptr) {
            LOG_WARNING("{}: skinning pool not enlarged - no memory near the module", game->name);
            return;
        }

        // The users reach the new buffer first, and only then does the allocator hand out more than the stock size.
        // The first write that fails stops the install, and Restore takes back what was written, last first.
        bool ok = true;
        for (auto lea : leas) {
            int disp = (int)(game->pool - (char*)(lea + 7));
            if (!(ok = Poke(*game, lea + 3, &disp, 4))) break;
        }
        unsigned size = kPoolSize;
        ok = ok && Poke(*game, limit, &size, 4);
        if (!ok) {
            LOG_WARNING("{}: skinning pool not enlarged - a write failed{}", game->name, Restore(*game) ? "" : ", and undoing it too");
            return;
        }
        LOG_INFO("{}: skinning pool 0x{:X} -> 0x{:X} bytes ({} users)", game->name, kStockSize, kPoolSize, users);
    }
}
