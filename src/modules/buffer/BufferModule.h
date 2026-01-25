#ifndef PROTOJS_BUFFERMODULE_H
#define PROTOJS_BUFFERMODULE_H

#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

class BufferModule {
public:
    static void init(JSContext* ctx);

private:
    // Static methods
    static JSValue bufferFrom(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferAlloc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferConcat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferIsBuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Constructor
    static JSValue BufferConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void BufferFinalizer(JSRuntime* rt, JSValue val);
    
    // Instance methods
    static JSValue bufferToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferSlice(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferCopy(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferFill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferIndexOf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue bufferIncludes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper functions
    static const proto::ProtoByteBuffer* getBufferData(JSContext* ctx, JSValueConst val);
};

} // namespace protojs

#endif // PROTOJS_BUFFERMODULE_H
