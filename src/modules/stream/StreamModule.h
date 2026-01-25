#ifndef PROTOJS_STREAMMODULE_H
#define PROTOJS_STREAMMODULE_H

#include "quickjs.h"

namespace protojs {

class StreamModule {
public:
    static void init(JSContext* ctx);

private:
    // ReadableStream
    static JSValue ReadableStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue readableRead(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue readablePipe(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void ReadableStreamFinalizer(JSRuntime* rt, JSValue val);
    
    // WritableStream
    static JSValue WritableStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue writableWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue writableEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void WritableStreamFinalizer(JSRuntime* rt, JSValue val);
    
    // DuplexStream
    static JSValue DuplexStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void DuplexStreamFinalizer(JSRuntime* rt, JSValue val);
    
    // TransformStream
    static JSValue TransformStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void TransformStreamFinalizer(JSRuntime* rt, JSValue val);
    
    // PassThrough (simple transform)
    static JSValue PassThroughConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_STREAMMODULE_H
