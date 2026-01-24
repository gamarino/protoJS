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
        std::cerr << "Symbol protojs_native_module_info not found" << std::endl;
        closeLibrary(handle);
        return nullptr;
    }
    
    ProtoJSNativeModuleInfo* info = static_cast<ProtoJSNativeModuleInfo*>(symbol);
    if (!validateABI(info)) {
        std::cerr << "ABI version mismatch" << std::endl;
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

bool DynamicLibraryLoader::initializeModule(LoadedModule* module, JSContext* ctx, proto::ProtoContext* pContext) {
    if (!module || !module->info || !module->info->init) return false;
    JSValue moduleObj = JS_NewObject(ctx);
    int result = module->info->init(ctx, pContext, moduleObj);
    JS_FreeValue(ctx, moduleObj);
    return result == 0;
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
    return info && info->abiVersion == PROTOJS_ABI_VERSION && info->name && info->init;
}

} // namespace protojs
