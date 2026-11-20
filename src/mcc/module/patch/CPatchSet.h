#pragma once

#include "CPatch.h"

class CPatchSet {
public:
    CPatchSet() = default;
    ~CPatchSet() = default;

    CPatchSet(std::initializer_list<CPatch> embed_patches) : m_embed_patches(), m_patches() {
        for (const auto& patch : embed_patches) {
            auto new_patch = new CPatch(patch);
            new_patch->setParent(this);
            m_embed_patches.push_back(new_patch);
        }
    }

    void add(const char* name, const char* desc, __int64 offset, const std::vector<__int8>& src, bool enabled = false);
    void clear();

    void apply();
    // known_build: the module is the build the offsets were written for, where patch.xml's fixed offsets hold.
    void update(__int64 hModule, bool known_build = true);

    inline __int64 moduleAddress() {return hModule;}
    inline bool knownBuild() const {return m_known_build;}
    inline std::vector<CPatch*>& embed_patches() {return m_embed_patches;}
    inline std::vector<CPatch*>& patches() {return m_patches;}

private:
    __int64 hModule = 0;
    bool m_known_build = true;
    std::vector<CPatch*> m_embed_patches;
    std::vector<CPatch*> m_patches;
};