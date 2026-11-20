#include "haloreach.h"

#include "common.h"
#include "mcc/module/patch/CPatch.h"
#include "offsets/Pattern.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

// Split screen: every skinned object Reach draws in a frame - in any view, and again for shadows - takes its bone
// matrices from one 0x35C00-byte pool, about 55 Spartans' worth. Once bodies pile up, four views use it all, and
// the later views' Spartans and first-person weapons come out black, over-bright or not at all (the last view
// first). The pool is moved to a 4 MB buffer of our own: its users are pointed at the buffer, the byte offset in
// its handles widened from 18 to 22 bits, and the allocator's limit raised to match.
//
// Measured at 4 players: the pool's high-water mark climbs with the pile of bodies and dropped weapons (136 K at
// 343 objects, 213 K at 425) and reaches the limit at about 460, exactly where players 3 and 4 break; Reach's own
// garbage collector keeps about 500 around in split screen. A skinned draw whose allocation fails (-1) is drawn
// without its bones, and the views are drawn in order, so the last ones lose. The pool is CPU-side only (each draw
// copies its matrices into the SkinningVS constants) and nothing reads the frame bits of a handle back.
namespace HaloReach::Entry::Skinning {
    constexpr size_t kPoolSize = 0x400000;       // 22 bits of byte offset
    constexpr unsigned kStockSize = 0x35C00;
    constexpr unsigned kPoolMask = kPoolSize - 1;
    constexpr unsigned char kStockShift = 18, kPoolShift = 22;
    // Its users in MCC 1.3528: ten lea reg, [pool] (one clears it at startup) and ten masks. A build laid out
    // differently is left alone - a mask missed would cut offsets short. These checks are also what keeps the
    // fixed offsets from being trusted on another haloreach.dll build.
    constexpr size_t kLeas = 10, kMasks = 10;

    struct Write { unsigned char* at; unsigned char original[4]; int size; };
    std::vector<Write> s_writes;
    char* s_pool = nullptr;

    // The bytes are code, so the instruction cache is flushed after each write.
    bool Poke(unsigned char* at, const void* bytes, int size) {
        Write w{at, {}, size};
        memcpy(w.original, at, size);
        if (!CPatch::apply(at, bytes, size)) return false;
        FlushInstructionCache(GetCurrentProcess(), at, size);
        s_writes.push_back(w);
        return true;
    }

    // Puts the module's bytes back, last write first (Enlarge's order, reversed: the allocator's size, the masks,
    // the handles' offset width, the leas), so code never reaches past the buffer its lea points at. A write that
    // fails stops it there - the writes left, and the pool they may still reach, stay for the next try.
    bool Restore() {
        while (!s_writes.empty()) {
            const Write& w = s_writes.back();
            if (!CPatch::apply(w.at, w.original, w.size)) return false;
            FlushInstructionCache(GetCurrentProcess(), w.at, w.size);
            s_writes.pop_back();
        }
        if (s_pool) VirtualFree(s_pool, 0, MEM_RELEASE);
        s_pool = nullptr;
        return true;
    }

    // The module is going away: its bytes back if possible, and the writes forgotten either way (they'd point into
    // whatever is mapped there next). A pool the module's code may still reach is left allocated rather than freed.
    void Unload() {
        if (!Restore()) {
            LOG_WARNING("Reach: skinning pool writes not undone at unload - the pool stays allocated");
            s_writes.clear();
            s_pool = nullptr;
        }
    }

    // The users reach the pool with 32-bit RIP-relative addresses, so the buffer has to sit within 2 GB of the
    // module: the first free range after it.
    char* AllocateAfter(unsigned char* module, size_t image_size) {
        constexpr uintptr_t kReach = 0x7FF00000; // past the module's base, with a margin under 2 GB
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const uintptr_t granularity = info.dwAllocationGranularity;
        uintptr_t at = ((uintptr_t)module + image_size + granularity - 1) & ~(granularity - 1);
        const uintptr_t limit = (uintptr_t)module + kReach - kPoolSize;
        MEMORY_BASIC_INFORMATION mbi;
        while (at < limit && VirtualQuery((void*)at, &mbi, sizeof(mbi))) {
            uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (mbi.State == MEM_FREE && end - at >= kPoolSize)
                if (auto p = VirtualAlloc((void*)at, kPoolSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return (char*)p;
            at = (end + granularity - 1) & ~(granularity - 1);
        }
        return nullptr;
    }

    bool IsStockMask(const unsigned char* p) { return memcmp(p, "\xFF\xFF\x03\x00", 4) == 0; }

    void Enlarge(__int64 hModule) {
        if (!Restore()) {
            LOG_WARNING("Reach: skinning pool not enlarged - an earlier enlargement couldn't be undone");
            return;
        }
        if (!AlphaRing::Found({OFFSET_HALOREACH_DAT_SKINNING_POOL, OFFSET_HALOREACH_PF_SKINNING_POOL_ALLOCATE})) return;

        auto module = (unsigned char*)hModule;
        auto nt = AlphaRing::Offsets::NtHeaders(module);
        if (nt == nullptr) return;
        auto pool = module + OFFSET_HALOREACH_DAT_SKINNING_POOL;
        auto allocate = module + OFFSET_HALOREACH_PF_SKINNING_POOL_ALLOCATE;

        // The allocator's limit (mov ecx, 0x35C00) and its handle shift (shl r10d, 0x12).
        unsigned char *limit = nullptr, *shift = nullptr;
        for (int i = 0; i < 0x80; ++i) {
            if (!limit && allocate[i] == 0xB9 && *(unsigned*)(allocate + i + 1) == kStockSize) limit = allocate + i + 1;
            if (!shift && memcmp(allocate + i, "\x41\xC1\xE2", 3) == 0 && allocate[i + 3] == kStockShift)
                shift = allocate + i + 3;
        }

        // Every lea reg, [rip + pool] in the code, and every `and reg, 0x3FFFF` in the functions they're in.
        std::vector<unsigned char*> leas, masks;
        auto section = IMAGE_FIRST_SECTION(nt);
        for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
            if (!(section->Characteristics & IMAGE_SCN_CNT_CODE)) continue;
            auto begin = module + section->VirtualAddress, end = begin + section->Misc.VirtualSize - 7;
            for (auto p = begin; p < end; ++p)
                if ((p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8D && (p[2] & 0xC7) == 0x05 &&
                    p + 7 + *(int*)(p + 3) == pool)
                    leas.push_back(p);
        }
        for (auto lea : leas) {
            DWORD64 base;
            auto function = RtlLookupFunctionEntry((DWORD64)lea, &base, nullptr);
            if (function == nullptr) continue;
            auto p = (unsigned char*)base + function->BeginAddress, end = (unsigned char*)base + function->EndAddress - 4;
            for (; p < end; ++p) {
                unsigned char* imm = p[0] == 0x25 ? p + 1                                           // and eax
                                   : p[0] == 0x81 && (p[1] & 0xF8) == 0xE0 ? p + 2                   // and r32
                                   : p[0] == 0x41 && p[1] == 0x81 && (p[2] & 0xF8) == 0xE0 ? p + 3   // and r8d-r15d
                                   : nullptr;
                if (imm && IsStockMask(imm) && std::find(masks.begin(), masks.end(), imm) == masks.end())
                    masks.push_back(imm);
            }
        }
        if (!limit || !shift || leas.size() != kLeas || masks.size() != kMasks) {
            LOG_WARNING("Reach: skinning pool not enlarged - allocator {}, {} users, {} offsets", limit && shift ? "found" : "not found",
                        leas.size(), masks.size());
            return;
        }

        s_pool = AllocateAfter(module, nt->OptionalHeader.SizeOfImage);
        if (s_pool == nullptr) {
            LOG_WARNING("Reach: skinning pool not enlarged - no memory near the module");
            return;
        }

        // Each step leaves a working pool: the users reach the new buffer first (as big as any offset), then the
        // handles get room for the wider offset, then the masks read it, and only last does the allocator hand out
        // more than the stock size. The first write that fails stops the install, and Restore takes back what was
        // written, last first - so whatever stays in place is always one of these steps.
        bool ok = true;
        for (auto lea : leas) {
            int disp = (int)(s_pool - (char*)(lea + 7));
            if (!(ok = Poke(lea + 3, &disp, 4))) break;
        }
        ok = ok && Poke(shift, &kPoolShift, 1);
        for (auto imm : masks) {
            if (!ok) break;
            ok = Poke(imm, &kPoolMask, 4);
        }
        unsigned size = kPoolSize;
        ok = ok && Poke(limit, &size, 4);
        if (!ok) {
            LOG_WARNING("Reach: skinning pool not enlarged - a write failed{}", Restore() ? "" : ", and undoing it too");
            return;
        }
        LOG_INFO("Reach: skinning pool 0x{:X} -> 0x{:X} bytes ({} users, {} offsets)", kStockSize, kPoolSize, leas.size(),
                 masks.size());
    }
}
