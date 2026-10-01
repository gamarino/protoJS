/*
 * DynamicLibrary — dlopen and friends on every platform.
 *
 * POSIX has <dlfcn.h>. Windows loads a DLL with LoadLibraryW instead, so there
 * this header supplies the five names the runtime uses, with the same contract:
 * dlopen returns nullptr on failure and dlerror then describes it. RTLD_GLOBAL
 * and RTLD_LOCAL mean nothing on Windows, where every DLL keeps its own symbol
 * namespace and a module imports what it needs by name.
 */
#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#define RTLD_LAZY   0x1
#define RTLD_NOW    0x2
#define RTLD_LOCAL  0x0
#define RTLD_GLOBAL 0x100

namespace protojs::platform::detail {
inline thread_local std::string tl_dlerror;

inline void dlSetError(const std::string& what) {
    const DWORD code = ::GetLastError();
    char* text = nullptr;
    ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
    tl_dlerror = what + ": " + (text ? text : ("error " + std::to_string(code)));
    if (text) ::LocalFree(text);
    while (!tl_dlerror.empty() && (tl_dlerror.back() == '\n' || tl_dlerror.back() == '\r'))
        tl_dlerror.pop_back();
}
} // namespace protojs::platform::detail

inline void* dlopen(const char* path, int /*flags*/) {
    // Paths are UTF-8 in protoJS and UTF-16 for the system.
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wide(n > 0 ? static_cast<std::size_t>(n) : 1u, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), n);
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR needs an absolute path.
    wchar_t full[32768];
    const DWORD len = ::GetFullPathNameW(wide.c_str(), 32768, full, nullptr);
    if (len > 0 && len < 32768) wide.assign(full, len);
    HMODULE module = ::LoadLibraryExW(wide.c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) protojs::platform::detail::dlSetError(path);
    return module;
}

inline void* dlsym(void* handle, const char* name) {
    FARPROC proc = ::GetProcAddress(static_cast<HMODULE>(handle), name);
    if (!proc) protojs::platform::detail::dlSetError(name);
    return reinterpret_cast<void*>(proc);
}

inline int dlclose(void* handle) {
    return ::FreeLibrary(static_cast<HMODULE>(handle)) ? 0 : -1;
}

inline const char* dlerror() {
    return protojs::platform::detail::tl_dlerror.c_str();
}

#else
#include <dlfcn.h>
#endif
