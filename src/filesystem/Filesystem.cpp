#include "Filesystem.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>

std::string AlphaRing::Filesystem::ModuleFile(const char *name) {
    char module_path[MAX_PATH]{};
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&ModuleFile), &hModule) ||
        GetModuleFileNameA(hModule, module_path, MAX_PATH) == 0)
        return name;
    return (std::filesystem::path(module_path).parent_path() / name).string();
}

bool AlphaRing::Filesystem::Init() {
    const char* home_dir = "../../../alpha_ring";

    if (!std::filesystem::exists(home_dir)) {
        std::filesystem::create_directories(home_dir);
    }

    return true;
}

bool AlphaRing::Filesystem::Shutdown() {
    return true;
}

void AlphaRing::Filesystem::GetDir(const wchar_t *path_in, wchar_t *path_out) {
    std::wcscpy(path_out, std::filesystem::absolute(path_in).c_str());
}

void AlphaRing::Filesystem::GetDir(const char *path_in, wchar_t *path_out) {
    std::wcscpy(path_out, std::filesystem::absolute(path_in).c_str());
}

void AlphaRing::Filesystem::GetResource(const char *path_in, wchar_t *path_out) {
    std::wcscpy(path_out,
                std::filesystem::absolute(std::filesystem::path("./alpha_ring").append(path_in)).c_str());
}

bool AlphaRing::Filesystem::Exist(const char *path) {
    return std::filesystem::exists(std::filesystem::path(path));
}

bool AlphaRing::Filesystem::Exist(const wchar_t *path) {
    return std::filesystem::exists(std::filesystem::path(path));
}

bool AlphaRing::Filesystem::Save(const char *file_name, const char *data, size_t size) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(data, size);
    file.close();

    return true;
}
