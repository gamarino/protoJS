#ifndef PROTOJS_NATIVEMODULE_WRAPPER_H
#define PROTOJS_NATIVEMODULE_WRAPPER_H

#include "quickjs.h"
#include "NativeModuleABI.h"
#include "headers/protoCore.h"

namespace protojs {

class JSContextWrapper;

class NativeModuleWrapper {
public:
    /**
     * @brief Wrap a native C/C++ function for use in JavaScript
     * 
     * @param func The native function to wrap
     * @param name The name to expose in JavaScript
     * @param ctx The JavaScript context
     * @param pContext The ProtoCore context
     * @return JSValue A callable JavaScript function
     */
    static JSValue wrapNativeFunction(
        ProtoJSNativeFunction func,
        const char* name,
        JSContext* ctx,
        proto::ProtoContext* pContext
    );

private:
    /**
     * @brief Call a native function with JS arguments
     */
    static JSValue callNativeFunction(
        ProtoJSNativeFunction func,
        JSContext* ctx,
        proto::ProtoContext* pContext,
        JSValueConst this_val,
        int argc,
        JSValueConst* argv
    );

    /**
     * @brief Handle exceptions from native functions
     */
    static JSValue handleNativeException(
        const std::exception& e,
        JSContext* ctx
    );

    /**
     * @brief Convert JS arguments to ProtoList
     */
    static const proto::ProtoList* convertArgumentsToProtoList(
        JSContext* ctx,
        proto::ProtoContext* pContext,
        int argc,
        JSValueConst* argv
    );

    /**
     * @brief Convert JS object to keyword parameters
     */
    static const proto::ProtoSparseList* convertArgumentsToKeywordParams(
        JSContext* ctx,
        proto::ProtoContext* pContext,
        int argc,
        JSValueConst* argv
    );
};

} // namespace protojs

#endif // PROTOJS_NATIVEMODULE_WRAPPER_H
