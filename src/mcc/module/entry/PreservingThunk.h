#pragma once

#include <Windows.h>

#include <cstring>
#include <initializer_list>
#include <utility>

// Executable code made of `pieces` (bytes, size) laid end to end, or null.
inline void* EmitCode(std::initializer_list<std::pair<const void*, size_t>> pieces) {
    size_t size = 0;
    for (auto& piece : pieces) size += piece.second;
    auto code = (unsigned char*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (code == nullptr) return nullptr;
    auto at = code;
    for (auto& piece : pieces) {
        if (piece.second != 0) memcpy(at, piece.first, piece.second);
        at += piece.second;
    }
    FlushInstructionCache(GetCurrentProcess(), code, size);
    return code;
}

// The detour of a small leaf function whose callers count on it leaving registers alone.
// Compilers keep values in volatile registers across calls to leaves they can see into (Halo 3 keeps the
// screen height in r10 across its title-safe box function, Halo 4 an xmm register across its ultrawide
// helpers), which a C++ detour would clobber. The detour takes the four register arguments and the caller's
// return address: (void* rcx, void* rdx, void* r8, void* r9, __int64 return_address), and returns in rax or
// xmm0 as the leaf does.
using PreservedDetour = __int64 (*)(void* rcx, void* rdx, void* r8, void* r9, __int64 return_address);

// Code to hook in the detour's place: it saves every volatile register but rax and xmm0, calls `detour`,
// restores them and returns what the detour returned.
inline void* PreservingThunk(const void* detour) {
    static const unsigned char kBefore[] = {
        0x51,                                     // push rcx
        0x52,                                     // push rdx
        0x41, 0x50,                               // push r8
        0x41, 0x51,                               // push r9
        0x41, 0x52,                               // push r10
        0x41, 0x53,                               // push r11
        0x48, 0x81, 0xEC, 0x88, 0, 0, 0,          // sub rsp, 0x88     (shadow space, 5th argument, xmm1-5)
        0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,       // movdqu [rsp+0x30], xmm1
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,       // movdqu [rsp+0x40], xmm2
        0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,       // movdqu [rsp+0x50], xmm3
        0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60,       // movdqu [rsp+0x60], xmm4
        0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,       // movdqu [rsp+0x70], xmm5
        0x48, 0x8B, 0x84, 0x24, 0xB8, 0, 0, 0,    // mov rax, [rsp+0xB8] (the return address) ...
        0x48, 0x89, 0x44, 0x24, 0x20,             // mov [rsp+0x20], rax ... as the 5th argument
        0x48, 0xB8,                               // mov rax, detour
    };
    static const unsigned char kAfter[] = {
        0xFF, 0xD0,                               // call rax
        0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,       // movdqu xmm1, [rsp+0x30]
        0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,       // movdqu xmm2, [rsp+0x40]
        0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,       // movdqu xmm3, [rsp+0x50]
        0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60,       // movdqu xmm4, [rsp+0x60]
        0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,       // movdqu xmm5, [rsp+0x70]
        0x48, 0x81, 0xC4, 0x88, 0, 0, 0,          // add rsp, 0x88
        0x41, 0x5B,                               // pop r11
        0x41, 0x5A,                               // pop r10
        0x41, 0x59,                               // pop r9
        0x41, 0x58,                               // pop r8
        0x5A,                                     // pop rdx
        0x59,                                     // pop rcx
        0xC3,                                     // ret
    };
    return EmitCode({{kBefore, sizeof(kBefore)}, {&detour, sizeof(void*)}, {kAfter, sizeof(kAfter)}});
}

// The detour of a point inside a function: code to hook there that saves every volatile register, calls
// `detour` with the function's stack pointer at that point (its locals), restores them, runs `resume_code`
// (e.g. reloading a register from a local the detour rewrote) and continues at *resume -
// the hook's trampoline (Entry::m_pOriginal), which runs the instructions the hook displaced.
inline void* MidFunctionThunk(void (*detour)(char* locals), void* const* resume, const unsigned char* resume_code,
                              size_t resume_size) {
    static const unsigned char kBefore[] = {
        0x50,                                     // push rax
        0x51,                                     // push rcx
        0x52,                                     // push rdx
        0x41, 0x50,                               // push r8
        0x41, 0x51,                               // push r9
        0x41, 0x52,                               // push r10
        0x41, 0x53,                               // push r11
        0x48, 0x81, 0xEC, 0x88, 0, 0, 0,          // sub rsp, 0x88     (shadow space, xmm0-5)
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20,       // movdqu [rsp+0x20], xmm0
        0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,       // movdqu [rsp+0x30], xmm1
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,       // movdqu [rsp+0x40], xmm2
        0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,       // movdqu [rsp+0x50], xmm3
        0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60,       // movdqu [rsp+0x60], xmm4
        0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,       // movdqu [rsp+0x70], xmm5
        0x48, 0x8D, 0x8C, 0x24, 0xC0, 0, 0, 0,    // lea rcx, [rsp+0xC0] (the stack pointer at the hook)
        0x48, 0xB8,                               // mov rax, detour
    };
    static const unsigned char kAfter[] = {
        0xFF, 0xD0,                               // call rax
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20,       // movdqu xmm0, [rsp+0x20]
        0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30,       // movdqu xmm1, [rsp+0x30]
        0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,       // movdqu xmm2, [rsp+0x40]
        0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50,       // movdqu xmm3, [rsp+0x50]
        0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60,       // movdqu xmm4, [rsp+0x60]
        0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,       // movdqu xmm5, [rsp+0x70]
        0x48, 0x81, 0xC4, 0x88, 0, 0, 0,          // add rsp, 0x88
        0x41, 0x5B,                               // pop r11
        0x41, 0x5A,                               // pop r10
        0x41, 0x59,                               // pop r9
        0x41, 0x58,                               // pop r8
        0x5A,                                     // pop rdx
        0x59,                                     // pop rcx
        0x58,                                     // pop rax
    };
    static const unsigned char kJump[] = {
        0x50,                                     // push rax
        0x48, 0xB8,                               // mov rax, resume
    };
    static const unsigned char kJumpAfter[] = {
        0x48, 0x8B, 0x00,                         // mov rax, [rax]
        0x48, 0x87, 0x04, 0x24,                   // xchg [rsp], rax
        0xC3,                                     // ret (to *resume, with rax and rsp as they were)
    };
    return EmitCode({{kBefore, sizeof(kBefore)}, {&detour, sizeof(void*)}, {kAfter, sizeof(kAfter)},
                     {resume_code, resume_size}, {kJump, sizeof(kJump)}, {&resume, sizeof(void*)},
                     {kJumpAfter, sizeof(kJumpAfter)}});
}

// Like the games' Entry macros, for a detour hooked through PreservingThunk: its parameters are
// PreservedDetour's (the register arguments, then the caller's return address).
#define PreservedEntry(name, set, offset, pDetour, ...) \
    __int64 pDetour(__VA_ARGS__); \
    ::Entry name(set, offset, PreservingThunk((const void*)&pDetour), entry_feature); \
    __int64 pDetour(__VA_ARGS__)
