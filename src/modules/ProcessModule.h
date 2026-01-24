#ifndef PROTOJS_PROCESSMODULE_H
#define PROTOJS_PROCESSMODULE_H

#include "quickjs.h"

namespace protojs {

/**
 * @brief Process module providing Node.js-compatible process information.
 */
class ProcessModule {
public:
    /**
     * @brief Initialize the process module and register it in the global scope.
     * @param ctx QuickJS context
     * @param argc Command line argument count
     * @param argv Command line arguments
     */
    static void init(JSContext* ctx, int argc, char** argv);

private:
    // Process properties
    static JSValue GetArgv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue GetEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue GetCwd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue GetPlatform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue GetArch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Process methods
    static JSValue Exit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_PROCESSMODULE_H
