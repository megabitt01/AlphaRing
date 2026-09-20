#include "CPatch.h"
#include "CPatchSet.h"
#include <cstring>
#include <Windows.h>

bool CPatch::apply(void *dst, const void *src, size_t size, void *backup)  {
    bool result = false;
    DWORD oldprotect;

    if (dst == nullptr || src == nullptr || size == 0)
        return result;

    if (VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldprotect)) {
        if (backup != nullptr)
            memcpy(backup, dst, size);
        memcpy(dst, src, size);
        result = true;
    }
    VirtualProtect(dst, size, oldprotect, &oldprotect);

    return result;
}

bool CPatch::setState(bool state) {
    if (m_enabled == state) return false;
    m_enabled = state;
    return apply();
}

bool CPatch::apply()  {
    auto dst = (void*)(m_parent->moduleAddress() + m_offset);
    if (m_enabled) {
        // Idempotent: a redundant apply() while already enabled (e.g.
        // CModule::load_module restoring a saved "on" state via setState(),
        // then CPatchSet::apply() sweeping every enabled patch again) must not
        // recapture m_backup from the bytes this same patch just wrote - that
        // would silently replace a correctly captured stock backup with the
        // patched pattern itself, making a later disable restore nothing.
        if (memcmp(dst, m_data.data(), m_data.size()) == 0)
            return true;
        return apply(dst, m_data.data(), m_data.size(), m_backup.data());
    } else
        return apply(dst, m_backup.data(), m_backup.size());
}
