#include "StreamModule.h"
#include "../events/EventsModule.h"
#include <vector>
#include <string>
#include <queue>

namespace protojs {

static JSClassID readable_stream_class_id;
static JSClassID writable_stream_class_id;
static JSClassID duplex_stream_class_id;
static JSClassID transform_stream_class_id;

// Stream data structures
struct ReadableStreamData {
    std::queue<std::string> buffer;
    bool ended;
    bool paused;
    size_t highWaterMark;
    JSRuntime* rt;
    JSValue eventEmitter; // Reference to EventEmitter for events
    
    ReadableStreamData(JSRuntime* r) : ended(false), paused(false), highWaterMark(16384), rt(r), eventEmitter(JS_UNDEFINED) {}
    ~ReadableStreamData() {
        if (!JS_IsUndefined(eventEmitter)) {
            JS_FreeValueRT(rt, eventEmitter);
        }
    }
};

struct WritableStreamData {
    std::queue<std::string> buffer;
    bool ended;
    bool corked;
    size_t highWaterMark;
    JSRuntime* rt;
    JSValue eventEmitter;
    
    WritableStreamData(JSRuntime* r) : ended(false), corked(false), highWaterMark(16384), rt(r), eventEmitter(JS_UNDEFINED) {}
    ~WritableStreamData() {
        if (!JS_IsUndefined(eventEmitter)) {
            JS_FreeValueRT(rt, eventEmitter);
        }
    }
};

struct DuplexStreamData {
    ReadableStreamData* readable;
    WritableStreamData* writable;
    
    DuplexStreamData(JSRuntime* rt) {
        readable = new ReadableStreamData(rt);
        writable = new WritableStreamData(rt);
    }
    ~DuplexStreamData() {
        delete readable;
        delete writable;
    }
};

struct TransformStreamData {
    DuplexStreamData* duplex;
    JSValue transformFunc;
    JSValue flushFunc;
    
    TransformStreamData(JSRuntime* rt) {
        duplex = new DuplexStreamData(rt);
        transformFunc = JS_UNDEFINED;
        flushFunc = JS_UNDEFINED;
    }
    ~TransformStreamData() {
        if (!JS_IsUndefined(transformFunc)) {
            JS_FreeValueRT(duplex->readable->rt, transformFunc);
        }
        if (!JS_IsUndefined(flushFunc)) {
            JS_FreeValueRT(duplex->readable->rt, flushFunc);
        }
        delete duplex;
    }
};

void StreamModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register ReadableStream class
    JS_NewClassID(&readable_stream_class_id);
    JSClassDef readableClassDef = {
        "ReadableStream",
        ReadableStreamFinalizer
    };
    JS_NewClass(rt, readable_stream_class_id, &readableClassDef);
    
    JSValue readableProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, readableProto, "read", JS_NewCFunction(ctx, readableRead, "read", 1));
    JS_SetPropertyStr(ctx, readableProto, "pipe", JS_NewCFunction(ctx, readablePipe, "pipe", 1));
    JS_SetClassProto(ctx, readable_stream_class_id, readableProto);
    
    JSValue readableCtor = JS_NewCFunction2(ctx, ReadableStreamConstructor, "ReadableStream", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, readableCtor, readableProto);
    
    // Register WritableStream class
    JS_NewClassID(&writable_stream_class_id);
    JSClassDef writableClassDef = {
        "WritableStream",
        WritableStreamFinalizer
    };
    JS_NewClass(rt, writable_stream_class_id, &writableClassDef);
    
    JSValue writableProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, writableProto, "write", JS_NewCFunction(ctx, writableWrite, "write", 1));
    JS_SetPropertyStr(ctx, writableProto, "end", JS_NewCFunction(ctx, writableEnd, "end", 1));
    JS_SetClassProto(ctx, writable_stream_class_id, writableProto);
    
    JSValue writableCtor = JS_NewCFunction2(ctx, WritableStreamConstructor, "WritableStream", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, writableCtor, writableProto);
    
    // Register DuplexStream class
    JS_NewClassID(&duplex_stream_class_id);
    JSClassDef duplexClassDef = {
        "DuplexStream",
        DuplexStreamFinalizer
    };
    JS_NewClass(rt, duplex_stream_class_id, &duplexClassDef);
    
    JSValue duplexProto = JS_NewObject(ctx);
    // Inherit from both Readable and Writable
    JS_SetPropertyStr(ctx, duplexProto, "read", JS_NewCFunction(ctx, readableRead, "read", 1));
    JS_SetPropertyStr(ctx, duplexProto, "write", JS_NewCFunction(ctx, writableWrite, "write", 1));
    JS_SetClassProto(ctx, duplex_stream_class_id, duplexProto);
    
    JSValue duplexCtor = JS_NewCFunction2(ctx, DuplexStreamConstructor, "DuplexStream", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, duplexCtor, duplexProto);
    
    // Register TransformStream class
    JS_NewClassID(&transform_stream_class_id);
    JSClassDef transformClassDef = {
        "TransformStream",
        TransformStreamFinalizer
    };
    JS_NewClass(rt, transform_stream_class_id, &transformClassDef);
    
    JSValue transformProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, transformProto, "read", JS_NewCFunction(ctx, readableRead, "read", 1));
    JS_SetPropertyStr(ctx, transformProto, "write", JS_NewCFunction(ctx, writableWrite, "write", 1));
    JS_SetClassProto(ctx, transform_stream_class_id, transformProto);
    
    JSValue transformCtor = JS_NewCFunction2(ctx, TransformStreamConstructor, "TransformStream", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, transformCtor, transformProto);
    
    // Create stream module object
    JSValue streamModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, streamModule, "Readable", readableCtor);
    JS_SetPropertyStr(ctx, streamModule, "Writable", writableCtor);
    JS_SetPropertyStr(ctx, streamModule, "Duplex", duplexCtor);
    JS_SetPropertyStr(ctx, streamModule, "Transform", transformCtor);
    JS_SetPropertyStr(ctx, streamModule, "PassThrough", JS_NewCFunction2(ctx, PassThroughConstructor, "PassThrough", 0, JS_CFUNC_constructor, 0));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "stream", streamModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue StreamModule::ReadableStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, readable_stream_class_id);
    if (JS_IsException(obj)) return obj;
    
    ReadableStreamData* data = new ReadableStreamData(JS_GetRuntime(ctx));
    
    // Create EventEmitter for this stream
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            data->eventEmitter = emitter;
            JS_SetPropertyStr(ctx, obj, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    JS_SetOpaque(obj, data);
    return obj;
}

void StreamModule::ReadableStreamFinalizer(JSRuntime* rt, JSValue val) {
    ReadableStreamData* data = static_cast<ReadableStreamData*>(JS_GetOpaque(val, readable_stream_class_id));
    if (data) delete data;
}

JSValue StreamModule::readableRead(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ReadableStreamData* data = static_cast<ReadableStreamData*>(JS_GetOpaque(this_val, readable_stream_class_id));
    if (data) {
        if (!data->buffer.empty()) {
            std::string chunk = data->buffer.front();
            data->buffer.pop();
            return JS_NewString(ctx, chunk.c_str());
        } else if (data->ended) {
            return JS_NULL;
        }
    }
    return JS_UNDEFINED;
}

JSValue StreamModule::readablePipe(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "pipe requires a destination stream");
    }
    
    // Basic pipe implementation
    // In a full implementation, this would set up data flow between streams
    // For Phase 2, return the destination stream for chaining
    return JS_DupValue(ctx, argv[0]);
}

JSValue StreamModule::WritableStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, writable_stream_class_id);
    if (JS_IsException(obj)) return obj;
    
    WritableStreamData* data = new WritableStreamData(JS_GetRuntime(ctx));
    
    // Create EventEmitter
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            data->eventEmitter = emitter;
            JS_SetPropertyStr(ctx, obj, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    JS_SetOpaque(obj, data);
    return obj;
}

void StreamModule::WritableStreamFinalizer(JSRuntime* rt, JSValue val) {
    WritableStreamData* data = static_cast<WritableStreamData*>(JS_GetOpaque(val, writable_stream_class_id));
    if (data) delete data;
}

JSValue StreamModule::writableWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "write requires data");
    }
    
    WritableStreamData* data = static_cast<WritableStreamData*>(JS_GetOpaque(this_val, writable_stream_class_id));
    if (data && !data->ended) {
        const char* str = JS_ToCString(ctx, argv[0]);
        if (str) {
            data->buffer.push(std::string(str));
            JS_FreeCString(ctx, str);
            
            // Emit 'drain' if buffer is below highWaterMark
            if (data->buffer.size() < data->highWaterMark && !JS_IsUndefined(data->eventEmitter)) {
                JSValue emitFunc = JS_GetPropertyStr(ctx, data->eventEmitter, "emit");
                if (JS_IsFunction(ctx, emitFunc)) {
                    JSValue args[] = { JS_NewString(ctx, "drain") };
                    JS_Call(ctx, emitFunc, data->eventEmitter, 1, args);
                    JS_FreeValue(ctx, args[0]);
                }
                JS_FreeValue(ctx, emitFunc);
            }
            
            return JS_NewBool(ctx, data->buffer.size() < data->highWaterMark);
        }
    }
    return JS_NewBool(ctx, false);
}

JSValue StreamModule::writableEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    WritableStreamData* data = static_cast<WritableStreamData*>(JS_GetOpaque(this_val, writable_stream_class_id));
    if (data) {
        if (argc > 0) {
            // Write final chunk
            writableWrite(ctx, this_val, argc, argv);
        }
        data->ended = true;
        
        // Emit 'finish' event
        if (!JS_IsUndefined(data->eventEmitter)) {
            JSValue emitFunc = JS_GetPropertyStr(ctx, data->eventEmitter, "emit");
            if (JS_IsFunction(ctx, emitFunc)) {
                JSValue args[] = { JS_NewString(ctx, "finish") };
                JS_Call(ctx, emitFunc, data->eventEmitter, 1, args);
                JS_FreeValue(ctx, args[0]);
            }
            JS_FreeValue(ctx, emitFunc);
        }
    }
    return JS_DupValue(ctx, this_val);
}

JSValue StreamModule::DuplexStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, duplex_stream_class_id);
    if (JS_IsException(obj)) return obj;
    
    DuplexStreamData* data = new DuplexStreamData(JS_GetRuntime(ctx));
    JS_SetOpaque(obj, data);
    return obj;
}

void StreamModule::DuplexStreamFinalizer(JSRuntime* rt, JSValue val) {
    DuplexStreamData* data = static_cast<DuplexStreamData*>(JS_GetOpaque(val, duplex_stream_class_id));
    if (data) delete data;
}

JSValue StreamModule::TransformStreamConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, transform_stream_class_id);
    if (JS_IsException(obj)) return obj;
    
    TransformStreamData* data = new TransformStreamData(JS_GetRuntime(ctx));
    
    // Store transform and flush functions if provided
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        data->transformFunc = JS_DupValue(ctx, argv[0]);
    }
    if (argc > 1 && JS_IsFunction(ctx, argv[1])) {
        data->flushFunc = JS_DupValue(ctx, argv[1]);
    }
    
    JS_SetOpaque(obj, data);
    return obj;
}

void StreamModule::TransformStreamFinalizer(JSRuntime* rt, JSValue val) {
    TransformStreamData* data = static_cast<TransformStreamData*>(JS_GetOpaque(val, transform_stream_class_id));
    if (data) delete data;
}

JSValue StreamModule::PassThroughConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    // PassThrough is a Transform that passes data through unchanged
    return TransformStreamConstructor(ctx, new_target, 0, nullptr);
}

} // namespace protojs
