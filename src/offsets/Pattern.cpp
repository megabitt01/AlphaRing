#include "Pattern.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace AlphaRing::Offsets {
    namespace {
        struct Section {
            unsigned begin, end;
            bool code;
        };

        bool Sections(const unsigned char* base, std::vector<Section>& out, unsigned& image_size) {
            auto nt = NtHeaders(base);
            if (nt == nullptr) return false;
            image_size = nt->OptionalHeader.SizeOfImage;
            auto section = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
                unsigned begin = section->VirtualAddress, size = section->Misc.VirtualSize;
                if (begin >= image_size) continue;
                if (size > image_size - begin) size = image_size - begin;
                out.push_back({begin, begin + size, (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0});
            }
            return true;
        }

        // A pattern ready to match: its bytes, which of them count, and four literal bytes in a row to find it by.
        struct Compiled {
            std::vector<unsigned char> bytes, mask;
            size_t anchor = 0;
            uint32_t anchor_value = 0;
            size_t entry = 0;
            const Pattern* pattern = nullptr;
            int matches = 0;
            size_t at = 0; // the last match, from the image base
        };

        int Hex(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        }

        bool Parse(const char* text, Compiled& c) {
            for (const char* p = text; *p;) {
                if (*p == ' ') {
                    ++p;
                } else if (*p == '?') {
                    c.bytes.push_back(0);
                    c.mask.push_back(0);
                    p += p[1] == '?' ? 2 : 1;
                } else {
                    int high = Hex(p[0]), low = high < 0 ? -1 : Hex(p[1]);
                    if (low < 0) return false;
                    c.bytes.push_back((unsigned char)(high << 4 | low));
                    c.mask.push_back(1);
                    p += 2;
                }
            }
            for (size_t i = 0; i + 4 <= c.bytes.size(); ++i) {
                if (c.mask[i] && c.mask[i + 1] && c.mask[i + 2] && c.mask[i + 3]) {
                    c.anchor = i;
                    memcpy(&c.anchor_value, &c.bytes[i], 4);
                    return true;
                }
            }
            return false; // nothing to find it by
        }

        bool Matches(const unsigned char* at, const Compiled& c) {
            for (size_t i = 0; i < c.bytes.size(); ++i)
                if (c.mask[i] && at[i] != c.bytes[i]) return false;
            return true;
        }

        unsigned Hash(uint32_t value) { return (value * 2654435761u) >> 16; }
        constexpr unsigned kBuckets = 1u << 16;
    }

    const char* ResultName(Result result) {
        switch (result) {
            case Result::Found: return "found";
            case Result::NoPattern: return "no pattern (known build only)";
            case Result::NotFound: return "not found";
            case Result::Ambiguous: return "ambiguous";
            case Result::Conflict: return "conflicting matches";
        }
        return "?";
    }

    const IMAGE_NT_HEADERS64* NtHeaders(const unsigned char* base) {
        auto dos = (const IMAGE_DOS_HEADER*)base;
        if (base == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        auto nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return nullptr;
        return nt;
    }

    bool ReadBuild(const unsigned char* base, ModuleBuild& build) {
        auto nt = NtHeaders(base);
        if (nt == nullptr) return false;
        build.timestamp = nt->FileHeader.TimeDateStamp;
        build.image_size = nt->OptionalHeader.SizeOfImage;
        return true;
    }

    std::vector<Resolution> Scan(const ModuleTable& table, const unsigned char* base) {
        std::vector<Resolution> out(table.count);
        std::vector<Section> sections;
        unsigned image_size = 0;
        if (!Sections(base, sections, image_size)) return out;

        std::vector<Compiled> compiled;
        for (size_t i = 0; i < table.count; ++i) {
            for (auto& pattern : table.offsets[i].patterns) {
                if (pattern.bytes == nullptr) continue;
                ++out[i].patterns;
                Compiled c;
                c.entry = i;
                c.pattern = &pattern;
                if (Parse(pattern.bytes, c)) compiled.push_back(std::move(c));
            }
        }

        // patterns by their anchor, chained per hash bucket
        std::vector<int> head(kBuckets, -1), next(compiled.size(), -1);
        for (int k = 0; k < (int)compiled.size(); ++k) {
            unsigned h = Hash(compiled[k].anchor_value);
            next[k] = head[h];
            head[h] = k;
        }

        for (auto& section : sections) {
            if (!section.code) continue;
            const unsigned char* code = base + section.begin;
            size_t size = section.end - section.begin;
            for (size_t p = 0; p + 4 <= size; ++p) {
                uint32_t value;
                memcpy(&value, code + p, 4);
                for (int k = head[Hash(value)]; k >= 0; k = next[k]) {
                    auto& c = compiled[k];
                    if (c.anchor_value != value || p < c.anchor || p - c.anchor + c.bytes.size() > size) continue;
                    if (!Matches(code + p - c.anchor, c)) continue;
                    ++c.matches;
                    c.at = section.begin + p - c.anchor;
                }
            }
        }

        auto in_code = [&](__int64 rva) {
            for (auto& section : sections)
                if (section.code && rva >= section.begin && rva < section.end) return true;
            return false;
        };

        std::vector<bool> ambiguous(table.count, false), conflict(table.count, false);
        for (auto& c : compiled) {
            if (c.matches > 1) ambiguous[c.entry] = true;
            if (c.matches != 1) continue;
            __int64 value;
            if (c.pattern->field < 0) {
                value = (__int64)c.at + c.pattern->delta;
            } else {
                int32_t rel;
                memcpy(&rel, base + c.at + c.pattern->field, 4);
                value = (__int64)c.at + c.pattern->next + rel + c.pattern->delta;
            }
            bool code = table.offsets[c.entry].kind == Code;
            auto& r = out[c.entry];
            if (value <= 0 || value >= image_size || in_code(value) != code || (r.agreed && r.value != value)) {
                conflict[c.entry] = true;
                continue;
            }
            r.value = value;
            ++r.agreed;
        }

        for (size_t i = 0; i < table.count; ++i) {
            auto& r = out[i];
            if (r.patterns == 0) r.result = Result::NoPattern;
            else if (conflict[i]) r.result = Result::Conflict;
            else if (r.agreed) r.result = Result::Found;
            else if (ambiguous[i]) r.result = Result::Ambiguous;
            else r.result = Result::NotFound;
            if (r.result != Result::Found) r.value = 0;
        }
        return out;
    }
}
