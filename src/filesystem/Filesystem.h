#pragma once

#include <string>

#define MAX_PATH 260

namespace AlphaRing::Filesystem {
    bool Init();
    bool Shutdown();

    // `name` in this DLL's own directory (MCC/Binaries/Win64). Used instead of
    // a relative path because the game changes the process's working directory
    // after startup, so "./" resolves to different places over a session.
    std::string ModuleFile(const char* name);

    void GetDir(const char* path_in, wchar_t path_out[MAX_PATH]);
    void GetDir(const wchar_t* path_in, wchar_t path_out[MAX_PATH]);
    void GetResource(const char* path_in, wchar_t path_out[MAX_PATH]);

    bool Exist(const char* path);
    bool Exist(const wchar_t * path);

    bool Save(const char* file_name, const char* data, size_t size);
}