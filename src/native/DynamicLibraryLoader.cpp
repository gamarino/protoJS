#include "DynamicLibraryLoader.h"
#include <dlfcn.h>
#include <iostream>

namespace protojs {

LoadedModule* DynamicLibraryLoader::load(const std::string& filePath) {
    void* handle = openLibrary(filePath);
    if (!handle) {
        std::cerr << "Failed to load library: " << filePath << " - " << dlerror() << std::endl;
        return nullptr;
    }

    void* symbol = getSymbol(handle, "protojs_native_module_info");
    if (!symbol) {
        std::cerr << "Symbol protojs_native_module_info not found in " << filePath << std::endl;
        closeLibrary(handle);
        return nullptr;
    }

    ProtoJSNativeModuleInfo* info = static_cast<ProtoJSNativeModuleInfo*>(symbol);
    if (!validateABI(info)) {
        if (info && info->abiVersion == 1) {
            // v1 addons built their exports with the QuickJS C API, which the
            // protoCore interpreter cannot call.
            std::cerr << "native addon " << filePath
                      << " uses ABI v1 (QuickJS values); rebuild against ABI v"
                      << PROTOJS_ABI_VERSION << std::endl;
        } else if (info) {
            std::cerr << "native addon " << filePath << " declares ABI v"
                      << info->abiVersion << "; this runtime implements v"
                      << PROTOJS_ABI_VERSION << std::endl;
        } else {
            std::cerr << "native addon " << filePath
                      << " has no module information" << std::endl;
        }
        closeLibrary(handle);
        return nullptr;
    }

    auto* module = new LoadedModule();
    module->handle = handle;
    module->info = info;
    module->filePath = filePath;
    return module;
}

void DynamicLibraryLoader::unload(LoadedModule* module) {
    if (module && module->handle) {
        closeLibrary(module->handle);
        delete module;
    }
}

const proto::ProtoObject* DynamicLibraryLoader::initializeModule(
        LoadedModule* module,
        proto::ProtoContext* context,
        const proto::ProtoObject* moduleObject) {
    if (!module || !module->info || !module->info->init) return nullptr;
    if (!context || !moduleObject || moduleObject == PROTO_NONE) return nullptr;

    if (module->info->init(context, moduleObject) != 0) return nullptr;

    return protojs_get_exports(context, moduleObject);
}

std::string DynamicLibraryLoader::getLibraryExtension() {
#ifdef __APPLE__
    return ".dylib";
#elif _WIN32
    return ".dll";
#else
    return ".so";
#endif
}

std::string DynamicLibraryLoader::getLibraryPrefix() {
#ifdef _WIN32
    return "";
#else
    return "lib";
#endif
}

void* DynamicLibraryLoader::openLibrary(const std::string& path) {
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
}

void* DynamicLibraryLoader::getSymbol(void* handle, const char* symbol) {
    return dlsym(handle, symbol);
}

void DynamicLibraryLoader::closeLibrary(void* handle) {
    dlclose(handle);
}

bool DynamicLibraryLoader::validateABI(const ProtoJSNativeModuleInfo* info) {
    if (!info) return false;
    if (!info->name || !info->init) return false;
    return info->abiVersion == PROTOJS_ABI_VERSION;
}

} // namespace protojs
