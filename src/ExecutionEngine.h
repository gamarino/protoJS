#ifndef PROTOJS_EXECUTIONENGINE_H
#define PROTOJS_EXECUTIONENGINE_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include "TypeBridge.h"
#include "GCBridge.h"

namespace protojs {

/**
 * @brief ExecutionEngine intercepts QuickJS operations and redirects them to protoCore
 * 
 * This engine allows QuickJS to parse and compile JavaScript, but executes
 * operations using protoCore's runtime for better performance and immutability.
 */
class ExecutionEngine {
public:
    /**
     * @brief Initialize ExecutionEngine for a JSContext
     */
    static void initialize(JSContext* ctx, proto::ProtoContext* pContext);

    /**
     * @brief Cleanup ExecutionEngine for a JSContext
     */
    static void cleanup(JSContext* ctx);

    /**
     * @brief Intercept property get operation
     */
    static JSValue opGetProperty(JSContext* ctx, JSValue obj, JSAtom prop);

    /**
     * @brief Intercept property set operation
     */
    static int opSetProperty(JSContext* ctx, JSValue obj, JSAtom prop, JSValue val, int flags);

    /**
     * @brief Intercept function call operation
     */
    static JSValue opCall(JSContext* ctx, JSValue func, JSValue this_val, int argc, JSValueConst* argv);

    /**
     * @brief Intercept object creation
     */
    static JSValue opNewObject(JSContext* ctx, JSValueConst proto);

    /**
     * @brief Intercept array creation
     */
    static JSValue opNewArray(JSContext* ctx);

    /**
     * @brief Intercept arithmetic operations
     */
    static JSValue opAdd(JSContext* ctx, JSValue a, JSValue b);
    static JSValue opSub(JSContext* ctx, JSValue a, JSValue b);
    static JSValue opMul(JSContext* ctx, JSValue a, JSValue b);
    static JSValue opDiv(JSContext* ctx, JSValue a, JSValue b);

    /**
     * @brief Intercept comparison operations
     */
    static int opCompare(JSContext* ctx, JSValue a, JSValue b);

private:
    /**
     * @brief Get ProtoContext from JSContext
     * Uses JSContextWrapper stored in JSContext opaque
     */
    static proto::ProtoContext* getProtoContext(JSContext* ctx);

    /**
     * @brief Check if an object should be handled by protoCore
     */
    static bool shouldUseProtoCore(JSContext* ctx, JSValue obj);
};

} // namespace protojs

#endif // PROTOJS_EXECUTIONENGINE_H
