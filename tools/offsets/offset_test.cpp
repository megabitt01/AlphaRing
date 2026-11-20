// offset_test: the lookup the DLL runs on an MCC build it doesn't know (src/offsets), run over files.
//
//   offset_test [--mcc DIR]
//       Every module of the installed MCC, which must be the build the offsets were written for: each offset
//       with patterns has to be found at exactly its written value.
//   offset_test --module NAME --image FILE [--flat] [--expect FILE]
//       One module's image (a PE file, or with --flat one already laid out like a loaded module - what
//       tools/offsets/perturb.py writes). --expect lists "NAME VALUE" per offset, VALUE "-" for one that must
//       not be found; without it the written values are expected. Finding an offset at a wrong address fails.
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "offsets/Tables.h"

using namespace AlphaRing::Offsets;

namespace {
    const char* kDefaultMcc = R"(C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection)";

    // eModule order, as kTables
    const char* kFiles[kModuleCount] = {
        R"(halo1\halo1.dll)", R"(halo2\halo2.dll)", R"(halo3\halo3.dll)", R"(halo4\halo4.dll)",
        R"(groundhog\groundhog.dll)", R"(halo3odst\halo3odst.dll)", R"(haloreach\haloreach.dll)",
        R"(mcc\binaries\win64\MCC-Win64-Shipping.exe)",
    };

    bool ReadFile(const std::string& path, std::vector<unsigned char>& out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        out.resize((size_t)file.tellg());
        file.seekg(0);
        return (bool)file.read((char*)out.data(), (std::streamsize)out.size());
    }

    // A PE file laid out the way the loader maps it (no relocations: patterns leave relocated bytes out).
    bool Map(const std::vector<unsigned char>& file, std::vector<unsigned char>& image) {
        auto nt = file.size() >= 0x1000 ? NtHeaders(file.data()) : nullptr;
        if (nt == nullptr) return false;
        image.assign(nt->OptionalHeader.SizeOfImage, 0);
        memcpy(image.data(), file.data(), nt->OptionalHeader.SizeOfHeaders);
        auto section = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            size_t size = (std::min)(section->SizeOfRawData, section->Misc.VirtualSize);
            if (section->PointerToRawData + size > file.size() || section->VirtualAddress + size > image.size())
                return false;
            memcpy(image.data() + section->VirtualAddress, file.data() + section->PointerToRawData, size);
        }
        return true;
    }

    int ModuleIndex(const std::string& name) {
        for (int i = 0; i < kModuleCount; ++i)
            if (name == kTables[i].name) return i;
        return name == "mcc" ? kModuleMCC : -1;
    }

    // Checks one module; returns the number of failures.
    int Check(int module, const std::vector<unsigned char>& image, const std::map<std::string, __int64>* expect) {
        auto& table = kTables[module];
        ModuleBuild build{};
        ReadBuild(image.data(), build);
        bool known = build == table.build;

        auto start = std::chrono::steady_clock::now();
        auto results = Scan(table, image.data());
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

        int exact = 0, patterned = 0, lost = 0, failures = 0, unpatterned = 0, partly = 0;
        for (size_t i = 0; i < table.count; ++i) {
            auto& entry = table.offsets[i];
            auto& result = results[i];
            if (entry.known != entry.offset->value) {
                printf("  FAIL %s: the table says %#llx, the header %#llx - rerun gen_patterns.py\n",
                       entry.offset->name, entry.known, entry.offset->value);
                ++failures;
            }
            if (result.result == Result::NoPattern) {
                ++unpatterned;
                continue;
            }
            ++patterned;
            __int64 want = entry.known;
            if (expect) {
                auto it = expect->find(entry.offset->name);
                if (it == expect->end()) continue;
                want = it->second;
            }
            bool found = result.result == Result::Found;
            if (found && want >= 0 && result.value == want) {
                ++exact;
                if (result.agreed < result.patterns) {
                    ++partly; // found through its other pattern
                    if (!expect) {
                        printf("  FAIL %s: only %d of its %d patterns match\n", entry.offset->name, result.agreed,
                               result.patterns);
                        ++failures; // the build they were made from: every one must
                    }
                }
            } else if (!found && want < 0) {
                ++lost; // expected: its code was changed
            } else if (!found) {
                ++lost;
                printf("  MISS %s: %s (expected %#llx)\n", entry.offset->name, ResultName(result.result), want);
                if (!expect) ++failures;
            } else {
                if (want < 0) printf("  FAIL %s: found at %#llx, expected not found\n", entry.offset->name, result.value);
                else printf("  FAIL %s: found at %#llx, expected %#llx\n", entry.offset->name, result.value, want);
                ++failures;
            }
        }
        printf("%s (%s build): %d/%d patterned offsets found exactly (%d through only one of their patterns), "
               "%d not found, %d wrong; %d without patterns; scan %lld ms\n", table.name, known ? "known" : "other",
               exact, patterned, partly, lost, failures, unpatterned, (long long)ms);
        return failures;
    }
}

int main(int argc, char** argv) {
    std::string mcc = kDefaultMcc, module_name, image_path, expect_path;
    bool flat = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (arg == "--mcc") mcc = value();
        else if (arg == "--module") module_name = value();
        else if (arg == "--image") image_path = value();
        else if (arg == "--expect") expect_path = value();
        else if (arg == "--flat") flat = true;
        else {
            printf("usage: offset_test [--mcc DIR] | --module NAME --image FILE [--flat] [--expect FILE]\n");
            return 2;
        }
    }

    int failures = 0;
    if (module_name.empty()) {
        for (int module = 0; module < kModuleCount; ++module) {
            std::vector<unsigned char> file, image;
            std::string path = mcc + "\\" + kFiles[module];
            if (!ReadFile(path, file) || !Map(file, image)) {
                printf("%s: can't read %s\n", kTables[module].name, path.c_str());
                ++failures;
                continue;
            }
            failures += Check(module, image, nullptr);
        }
    } else {
        int module = ModuleIndex(module_name);
        std::vector<unsigned char> file, image;
        if (module < 0 || !ReadFile(image_path, file) || !(flat ? (image = std::move(file), true) : Map(file, image))) {
            printf("can't read module %s from %s\n", module_name.c_str(), image_path.c_str());
            return 2;
        }
        std::map<std::string, __int64> expect;
        if (!expect_path.empty()) {
            std::ifstream in(expect_path);
            std::string name, value;
            while (in >> name >> value) expect[name] = value == "-" ? -1 : (__int64)std::stoull(value, nullptr, 0);
        }
        failures += Check(module, image, expect_path.empty() ? nullptr : &expect);
    }

    printf(failures ? "FAILED: %d\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
