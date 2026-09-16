#ifndef PROTOJS_DYNAMICLIBRARYLOADER_H
#define PROTOJS_DYNAMICLIBRARYLOADER_H

#include "NativeModuleABI.h"
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

    /**
     * Run the addon's init function against a protoCore module object and
     * return `module.exports`.
     *
     * `module` must be a mutable object carrying a mutable `exports`.
     * Returns nullptr on failure; when the addon raised an exception through
     * `protojs_throw`, that exception is left pending for the interpreter.
     */
    static const proto::ProtoObject* initializeModule(
        LoadedModule* module,
        proto::ProtoContext* context,
        const proto::ProtoObject* moduleObject);

    static std::string getLibraryExtension();
    static std::string getLibraryPrefix();

    /**
     * True when the addon declares the ABI this build implements and carries
     * the required fields. Public so that it can be unit-tested.
     */
    static bool validateABI(const ProtoJSNativeModuleInfo* info);

private:
    static void* openLibrary(const std::string& path);
    static void* getSymbol(void* handle, const char* symbol);
    static void closeLibrary(void* handle);
};

} // namespace protojs

#endif
