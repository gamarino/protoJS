#include "Deferred.h"
#include "TypeBridge.h"
#include "JSContext.h"
#include "CPUThreadPool.h"
#include "EventLoop.h"
#include <iostream>
#include <memory>
#include <cstring>
#include <thread>

namespace protojs {

static JSClassID protojs_deferred_class_id;

// Store JSContextWrapper in JSContext opaque data
static const char* JS_CONTEXT_WRAPPER_KEY = "protojs_wrapper";

void Deferred::init(JSContext* ctx, JSContextWrapper* wrapper) {
    // Store wrapper in JSContext opaque
    JS_SetContextOpaque(ctx, wrapper);
    
    JS_NewClassID(&protojs_deferred_class_id);
    JSClassDef class_def = {
        "Deferred",
        finalizer
    };
    JS_NewClass(JS_GetRuntime(ctx), protojs_deferred_class_id, &class_def);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "then", JS_NewCFunction(ctx, then, "then", 1));
    JS_SetPropertyStr(ctx, proto, "catch", JS_NewCFunction(ctx, catch_, "catch", 1));
    JS_SetClassProto(ctx, protojs_deferred_class_id, proto);

    JSValue ctor = JS_NewCFunction2(ctx, constructor, "Deferred", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "Deferred", ctor);
    JS_FreeValue(ctx, global_obj);
}

JSContextWrapper* Deferred::getWrapperFromContext(JSContext* ctx) {
    return static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
}

JSValue Deferred::constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Deferred expects a function");
    }

    JSValue obj = JS_NewObjectClass(ctx, protojs_deferred_class_id);
    if (JS_IsException(obj)) return obj;

    JSContextWrapper* wrapper = getWrapperFromContext(ctx);
    if (!wrapper) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "Deferred: JSContextWrapper not found");
    }

    proto::ProtoSpace* space = wrapper->getProtoSpace();
    JSRuntime* rt = wrapper->getJSRuntime();
    
    // Serialize the function to bytecode
    size_t serializedSize = 0;
    uint8_t* serializedFunc = JS_WriteObject(ctx, &serializedSize, argv[0], JS_WRITE_OBJ_BYTECODE);
    
    if (!serializedFunc || serializedSize == 0) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "Deferred: Function not serializable. Functions with complex closures may not be supported.");
    }
    
    // Create resolve and reject callbacks
    // These will store the result/error and schedule callback execution
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        // This will be called from event loop
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    JSValue reject = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        // This will be called from event loop
        return JS_UNDEFINED;
    }, "reject", 1);

    // Copy serialized buffer to runtime memory (for thread safety)
    uint8_t* copiedBuffer = static_cast<uint8_t*>(js_malloc_rt(rt, serializedSize));
    if (!copiedBuffer) {
        js_free(ctx, serializedFunc);
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "Deferred: Failed to allocate memory for serialized function");
    }
    memcpy(copiedBuffer, serializedFunc, serializedSize);
    
    // Free original buffer (allocated by JS_WriteObject)
    js_free(ctx, serializedFunc);
    
    // Create task with copied buffer
    auto task = std::make_shared<DeferredTask>(
        ctx,
        JS_DupValue(ctx, argv[0]),  // Keep reference to original function
        copiedBuffer,               // Copied serialized bytecode
        serializedSize,             // Size of bytecode
        resolve,
        reject,
        rt,
        space,
        wrapper
    );
    
    // Store task in object (need to allocate on heap since it will be used across threads)
    DeferredTask* taskPtr = new DeferredTask(*task);
    // Note: taskPtr now owns the copiedBuffer, will be freed in finalizer
    JS_SetOpaque(obj, taskPtr);

    // Execute in worker thread
    executeTaskInWorkerThread(task);

    return obj;
}

void Deferred::finalizer(JSRuntime* rt, JSValue val) {
    DeferredTask* task = static_cast<DeferredTask*>(JS_GetOpaque(val, protojs_deferred_class_id));
    if (task) {
        JS_FreeValueRT(rt, task->func);
        JS_FreeValueRT(rt, task->resolve);
        JS_FreeValueRT(rt, task->reject);
        JS_FreeValueRT(rt, task->result);
        JS_FreeValueRT(rt, task->error);
        JS_FreeValueRT(rt, task->thenCallback);
        JS_FreeValueRT(rt, task->catchCallback);
        
        // Free serialized buffers (allocated with js_malloc_rt)
        if (task->serializedFunc) {
            js_free_rt(rt, task->serializedFunc);
            task->serializedFunc = nullptr;
        }
        if (task->serializedResult) {
            js_free_rt(rt, task->serializedResult);
            task->serializedResult = nullptr;
        }
        
        delete task;
    }
}

JSValue Deferred::then(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "then expects a function");
    }
    
    DeferredTask* task = static_cast<DeferredTask*>(JS_GetOpaque(this_val, protojs_deferred_class_id));
    if (!task) {
        return JS_ThrowTypeError(ctx, "Invalid Deferred object");
    }
    
    task->thenCallback = JS_DupValue(ctx, argv[0]);
    
    // If already resolved, call callback immediately
    if (task->isResolved && !JS_IsUndefined(task->result)) {
        JSValue thenArgs[] = { task->result };
        JSValue thenResult = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
        JS_FreeValue(ctx, thenResult);
    }
    
    return JS_DupValue(ctx, this_val); // Return this for chaining
}

JSValue Deferred::catch_(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "catch expects a function");
    }
    
    DeferredTask* task = static_cast<DeferredTask*>(JS_GetOpaque(this_val, protojs_deferred_class_id));
    if (!task) {
        return JS_ThrowTypeError(ctx, "Invalid Deferred object");
    }
    
    task->catchCallback = JS_DupValue(ctx, argv[0]);
    
    // If already rejected, call callback immediately
    if (task->isRejected && !JS_IsUndefined(task->error)) {
        JSValue catchArgs[] = { task->error };
        JSValue catchResult = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
        JS_FreeValue(ctx, catchResult);
    }
    
    return JS_DupValue(ctx, this_val); // Return this for chaining
}

void Deferred::executeTaskInWorkerThread(std::shared_ptr<DeferredTask> task) {
    // Submit task to CPU thread pool for execution in worker thread
    CPUThreadPool& pool = CPUThreadPool::getInstance();
    
    pool.getExecutor().submit([task]() {
        workerThreadExecution(task);
    });
}

void Deferred::workerThreadExecution(std::shared_ptr<DeferredTask> task) {
    // Thread-local JSContext for worker thread
    // Each worker thread gets its own runtime and context for thread safety
    thread_local static JSContext* workerCtx = nullptr;
    thread_local static JSRuntime* workerRt = nullptr;
    
    // Initialize thread-local context if needed
    if (!workerCtx) {
        // Create a new runtime for this worker thread
        // Each worker thread needs its own runtime because QuickJS is not thread-safe
        workerRt = JS_NewRuntime();
        if (!workerRt) {
            // Error: failed to create runtime
            task->hasError = true;
            task->serializedResultSize = 0;
            // Schedule error handling in main thread
            EventLoop::getInstance().enqueueCallback([task]() {
                JSContext* ctx = task->mainJSContext;
                JSValue error = JS_NewString(ctx, "Failed to create worker thread runtime");
                JSValue rejectArgs[] = { error };
                JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                JS_FreeValue(ctx, rejectResult);
                JS_FreeValue(ctx, error);
            });
            return;
        }
        
        workerCtx = JS_NewContext(workerRt);
        if (!workerCtx) {
            JS_FreeRuntime(workerRt);
            workerRt = nullptr;
            task->hasError = true;
            EventLoop::getInstance().enqueueCallback([task]() {
                JSContext* ctx = task->mainJSContext;
                JSValue error = JS_NewString(ctx, "Failed to create worker thread context");
                JSValue rejectArgs[] = { error };
                JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                JS_FreeValue(ctx, rejectResult);
                JS_FreeValue(ctx, error);
            });
            return;
        }
    }
    
    try {
        // Deserialize function from bytecode
        JSValue func = JS_ReadObject(workerCtx, task->serializedFunc, task->serializedFuncSize, JS_READ_OBJ_BYTECODE);
        
        if (JS_IsException(func)) {
            // Deserialization failed - func is the exception
            task->hasError = true;
            const char* errStr = JS_ToCString(workerCtx, func);
            std::string errorMsg = errStr ? errStr : "Failed to deserialize function";
            JS_FreeCString(workerCtx, errStr);
            JS_FreeValue(workerCtx, func);
            
            EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                JSContext* ctx = task->mainJSContext;
                JSValue error = JS_NewString(ctx, errorMsg.c_str());
                JSValue rejectArgs[] = { error };
                JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                JS_FreeValue(ctx, rejectResult);
                JS_FreeValue(ctx, error);
            });
            return;
        }
        
        // If the deserialized object is a bytecode function, we may need to evaluate it
        // However, JS_ReadObject with JS_READ_OBJ_BYTECODE should return a callable function directly
        // So we can call it directly with JS_Call
        
        // Execute the function
        // The function should be called with no arguments
        JSValue result = JS_Call(workerCtx, func, JS_UNDEFINED, 0, nullptr);
        
        if (JS_IsException(result)) {
            // Execution failed - result is the exception
            task->hasError = true;
            
            // Serialize the exception
            size_t errorSize = 0;
            uint8_t* serializedError = JS_WriteObject(workerCtx, &errorSize, result, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(workerCtx, result);
            
            if (serializedError && errorSize > 0) {
                // Copy error buffer to main runtime memory
                uint8_t* copiedError = static_cast<uint8_t*>(js_malloc_rt(task->rt, errorSize));
                if (copiedError) {
                    memcpy(copiedError, serializedError, errorSize);
                    js_free(workerCtx, serializedError);
                    task->serializedResult = copiedError;
                    task->serializedResultSize = errorSize;
                    task->hasError = true;
                } else {
                    // Failed to allocate - fallback to error message
                    js_free(workerCtx, serializedError);
                    const char* errStr = "Function execution failed (serialization error)";
                    std::string errorMsg = errStr;
                    
                    EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                        JSContext* ctx = task->mainJSContext;
                        JSValue error = JS_NewString(ctx, errorMsg.c_str());
                        JSValue rejectArgs[] = { error };
                        JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                        JS_FreeValue(ctx, rejectResult);
                        JS_FreeValue(ctx, error);
                    });
                }
            } else {
                // Fallback: create error message
                const char* errStr = "Function execution failed (serialization error)";
                std::string errorMsg = errStr;
                
                EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                    JSContext* ctx = task->mainJSContext;
                    JSValue error = JS_NewString(ctx, errorMsg.c_str());
                    JSValue rejectArgs[] = { error };
                    JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                    JS_FreeValue(ctx, rejectResult);
                    JS_FreeValue(ctx, error);
                });
            }
        } else {
            // Serialize the result
            size_t resultSize = 0;
            uint8_t* serializedResult = JS_WriteObject(workerCtx, &resultSize, result, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(workerCtx, result);
            
            if (serializedResult && resultSize > 0) {
                // Copy the serialized result to main runtime's memory
                // The buffer from JS_WriteObject is allocated in worker runtime
                // We need to copy it to main runtime's memory for thread safety
                uint8_t* copiedResult = static_cast<uint8_t*>(js_malloc_rt(task->rt, resultSize));
                if (copiedResult) {
                    memcpy(copiedResult, serializedResult, resultSize);
                    // Free original buffer (from worker runtime)
                    js_free(workerCtx, serializedResult);
                    // Store copied buffer
                    task->serializedResult = copiedResult;
                    task->serializedResultSize = resultSize;
                    task->hasError = false;
                } else {
                    // Failed to allocate in main runtime
                    js_free(workerCtx, serializedResult);
                    task->hasError = false;
                    task->serializedResult = nullptr;
                    task->serializedResultSize = 0;
                }
            } else {
                // Serialization failed - fallback to undefined
                task->hasError = false;
                task->serializedResult = nullptr;
                task->serializedResultSize = 0;
            }
        }
        
        JS_FreeValue(workerCtx, func);
        
        // Schedule result handling in main thread
        EventLoop::getInstance().enqueueCallback([task]() {
            JSContext* ctx = task->mainJSContext;
            
            if (task->hasError) {
                // Deserialize and reject
                if (task->serializedResult && task->serializedResultSize > 0) {
                    JSValue error = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                    if (!JS_IsException(error)) {
                        JSValue rejectArgs[] = { error };
                        JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                        JS_FreeValue(ctx, rejectResult);
                        JS_FreeValue(ctx, error);
                    } else {
                        // Fallback error
                        JSValue fallbackError = JS_NewString(ctx, "Function execution failed");
                        JSValue rejectArgs[] = { fallbackError };
                        JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                        JS_FreeValue(ctx, rejectResult);
                        JS_FreeValue(ctx, fallbackError);
                    }
                } else {
                    JSValue error = JS_NewString(ctx, "Function execution failed");
                    JSValue rejectArgs[] = { error };
                    JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                    JS_FreeValue(ctx, rejectResult);
                    JS_FreeValue(ctx, error);
                }
            } else {
                // Deserialize and resolve
                if (task->serializedResult && task->serializedResultSize > 0) {
                    JSValue result = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                    if (!JS_IsException(result)) {
                        JSValue resolveArgs[] = { result };
                        JSValue resolveResult = JS_Call(ctx, task->resolve, JS_UNDEFINED, 1, resolveArgs);
                        JS_FreeValue(ctx, resolveResult);
                        JS_FreeValue(ctx, result);
                    } else {
                        // Fallback: resolve with undefined
                        JSValue undefined = JS_UNDEFINED;
                        JSValue resolveArgs[] = { undefined };
                        JSValue resolveResult = JS_Call(ctx, task->resolve, JS_UNDEFINED, 1, resolveArgs);
                        JS_FreeValue(ctx, resolveResult);
                    }
                } else {
                    // No result - resolve with undefined
                    JSValue undefined = JS_UNDEFINED;
                    JSValue resolveArgs[] = { undefined };
                    JSValue resolveResult = JS_Call(ctx, task->resolve, JS_UNDEFINED, 1, resolveArgs);
                    JS_FreeValue(ctx, resolveResult);
                }
            }
            
            // Free serialized result buffer (allocated in main runtime memory)
            if (task->serializedResult) {
                js_free_rt(task->rt, task->serializedResult);
                task->serializedResult = nullptr;
                task->serializedResultSize = 0;
            }
        });
        
    } catch (const std::exception& e) {
        std::string errorMessage = e.what();
        EventLoop::getInstance().enqueueCallback([task, errorMessage]() {
            JSContext* ctx = task->mainJSContext;
            JSValue error = JS_NewString(ctx, errorMessage.c_str());
            JSValue rejectArgs[] = { error };
            JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
            JS_FreeValue(ctx, rejectResult);
            JS_FreeValue(ctx, error);
        });
    }
}

// Old executeTask removed - using executeTaskInMainThread instead

} // namespace protojs
