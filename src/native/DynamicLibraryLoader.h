#ifndef PROTOJS_DYNAMICLIBRARYLOADER_H
#define PROTOJS_DYNAMICLIBRARYLOADER_H

#include "NativeModuleABI.h"
#include "quickjs.h"
#include <string>

namespace protojs {

struct LoadedModule {
    void* handle;
    ProtoJSNativeModuleInfo* info;
    std::string filePath;
    LoadedModule() : handle(nullptr), info(nullptr) {}
};

class DynamicLibraryLoader {
public:
    static LoadedModule* load(const std::string& filePath);
    static void unload(LoadedModule* module);
    /** Call native init with caller-provided moduleObject (with "exports"); returns JS_DupValue of moduleObject.exports, or JS_EXCEPTION on error. */
    static JSValue initializeModule(LoadedModule* module, JSContext* ctx, proto::ProtoContext* pContext, JSValue moduleObject);
    static std::string getLibraryExtension();
    static std::string getLibraryPrefix();

private:
    static void* openLibrary(const std::string& path);
    static void* getSymbol(void* handle, const char* symbol);
    static void closeLibrary(void* handle);
    static bool validateABI(const ProtoJSNativeModuleInfo* info);
};

} // namespace protojs

#endif
