#include "CPatchSet.h"

void CPatchSet::clear() {
    for (auto patch : m_patches) patch->setState(false); // (re-applies the rest, so delete afterwards)
    for (auto patch : m_patches) delete patch;
    m_patches.clear();
}

void CPatchSet::apply() {
    for (auto patch : m_embed_patches)
        if (patch->enabled())
            patch->apply();

    for (auto patch : m_patches)
        if (patch->enabled())
            patch->apply();
}

void CPatchSet::update(__int64 hModule, bool known_build) {
    this->hModule = hModule;
    m_known_build = known_build;
    for (auto patch : m_embed_patches) patch->capture();
    for (auto patch : m_patches) patch->capture();
}

void CPatchSet::add(const char *name, const char *desc, __int64 offset, const std::vector<__int8> &src, bool enabled) {
    auto patch = new CPatch(name, desc, offset, src, enabled);
    patch->setParent(this);
    patch->capture();
    for (auto other : m_embed_patches) patch->inheritBackup(*other);
    for (auto other : m_patches) patch->inheritBackup(*other);
    m_patches.push_back(patch);
}
