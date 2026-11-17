#pragma once

#include <functional>
#include <initializer_list>
#include <vector>

namespace AlphaRing {
    // An address in a game module or MCC's executable, from the module's base (the offset headers,
    // lib/game/inc/<version>/offset_*.h). It holds the address in the build the headers were written for;
    // when a module of another build loads, src/offsets/Offsets.cpp looks it up again from byte patterns
    // (patterns_*.inc) and replaces it, or marks it not found. Reads as the number: module + OFFSET_X.
    struct Offset {
        const char* name;
        __int64 value; // 0: not in this build, so what uses it stays off

        constexpr Offset(const char* name, __int64 value) : name(name), value(value) {}
        Offset(const Offset&) = delete; // a copy wouldn't see the lookup
        Offset& operator=(const Offset&) = delete;

        bool found() const { return value != 0; }
        operator __int64() const { return value; }
    };

    // Every one of these offsets is in this build (always, in the build the headers were written for).
    inline bool Found(std::initializer_list<std::reference_wrapper<const Offset>> offsets) {
        for (const Offset& offset : offsets)
            if (!offset.found()) return false;
        return true;
    }

    // Offsets that only work together: the hooks of one feature and what their detours reach into, or
    // what code outside a hook uses. On a build where one of them isn't found, the feature stays off.
    class Feature {
    public:
        Feature(const char* name, std::initializer_list<std::reference_wrapper<const Offset>> offsets)
                : m_name(name) {
            for (const Offset& offset : offsets) m_offsets.push_back(&offset);
        }

        void Add(const Offset& offset) { m_offsets.push_back(&offset); }

        // A feature that only works together with this one: when one of its offsets is missing, so is this.
        void Requires(const Feature& other) { m_requires.push_back(&other); }

        const char* Name() const { return m_name; }

        // The first of its offsets this build lacks, or nullptr when it's all there.
        const Offset* Missing() const {
            for (auto offset : m_offsets)
                if (!offset->found()) return offset;
            for (auto feature : m_requires)
                if (auto missing = feature->Missing()) return missing;
            return nullptr;
        }

        bool Available() const { return Missing() == nullptr; }

    private:
        const char* m_name;
        std::vector<const Offset*> m_offsets;
        std::vector<const Feature*> m_requires;
    };
}

// DefOffset(OFFSET_NAME, 0x1234): an offset in the header's build (lib/game/inc/<version>/offset_*.h).
#define DefOffset(name, value) inline ::AlphaRing::Offset name{#name, value};
