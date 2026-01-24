#ifndef PROTOJS_MODULEINTEROP_H
#define PROTOJS_MODULEINTEROP_H

#include "quickjs.h"
#include <string>

namespace protojs {

class ModuleInterop {
public:
    /**
     * @brief Convert CommonJS module to ES module format
     */
    static JSValue cjsToESM(JSValue cjsModule, JSContext* ctx);

    /**
     * @brief Convert ES module to CommonJS format
     */
    static JSValue esmToCJS(JSValue esmModule, JSContext* ctx);

    /**
     * @brief Wrap module with proper interop handling
     */
    static JSValue wrapModuleForInterop(JSValue module, JSContext* ctx, const std::string& format);

private:
    /**
     * @brief Check if a module is CommonJS format
     */
    static bool isCommonJSModule(JSValue module, JSContext* ctx);

    /**
     * @brief Check if a module is ES module namespace
     */
    static bool isESModuleNamespace(JSValue module, JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_MODULEINTEROP_H
