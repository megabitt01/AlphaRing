#include "CPatch.h"
#include "CPatchSet.h"
#include <cstring>
#include <algorithm>
#include <Windows.h>

bool CPatch::apply(void *dst, const void *src, size_t size)  {
    bool result = false;
    DWORD oldprotect;

    if (dst == nullptr || src == nullptr || size == 0)
        return result;

    if (VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldprotect)) {
        memcpy(dst, src, size);
        result = true;
    }
    VirtualProtect(dst, size, oldprotect, &oldprotect);
    if (result) FlushInstructionCache(GetCurrentProcess(), dst, size); // the bytes may be code

    return result;
}

// Copies unless the source can't be read (a patch.xml offset outside the module).
static bool ReadBytes(void* dst, const void* src, size_t size) {
    __try {
        memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CPatch::usable() const {
    return (m_base ? m_base->found() : m_parent->knownBuild()) && (m_needs == nullptr || m_needs());
}

void CPatch::capture() {
    m_captured = m_parent->moduleAddress() != 0 && usable() &&
                 ReadBytes(m_backup.data(), (const void*)(m_parent->moduleAddress() + address()), m_backup.size());
}

void CPatch::inheritBackup(const CPatch& other) {
    if (&other == this || !m_captured || !other.m_captured || !other.m_enabled) return;
    __int64 lo = (std::max)(address(), other.address());
    __int64 hi = (std::min)(address() + (__int64)m_backup.size(), other.address() + (__int64)other.m_backup.size());
    for (__int64 at = lo; at < hi; ++at)
        m_backup[at - address()] = other.m_backup[at - other.address()];
}

bool CPatch::setState(bool state) {
    if (m_enabled == state) return false;
    m_enabled = state;
    bool result = apply();
    // Restoring the module's bytes also undid any other enabled patch overlapping this one.
    if (!state) m_parent->apply();
    return result;
}

bool CPatch::apply()  {
    if (!m_captured || m_parent->moduleAddress() == 0) return false; // module not loaded (applied on load), the patch is outside it or not in its build
    // m_backup holds the module's own bytes, captured once when it loaded (CPatchSet::update),
    // so enabling and disabling can each be repeated safely in any order - e.g. a saved state
    // restored before the first apply, then CPatchSet::apply() sweeping every enabled patch.
  
    auto dst = (void*)(m_parent->moduleAddress() + address());
    return m_enabled ? apply(dst, m_data.data(), m_data.size()) : apply(dst, m_backup.data(), m_backup.size());
}
