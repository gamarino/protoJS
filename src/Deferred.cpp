#include "Deferred.h"
#include "TypeBridge.h"
#include "JSContext.h"
#include "CPUThreadPool.h"
#include "EventLoop.h"
#include <iostream>
#include <memory>
#include <cstring>
#include <cstdlib>
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

    // Copy serialized buffer to heap (malloc) so destructor can free it without touching runtime at process exit
    uint8_t* copiedBuffer = static_cast<uint8_t*>(malloc(serializedSize));
    if (!copiedBuffer) {
        js_free(ctx, serializedFunc);
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "Deferred: Failed to allocate memory for serialized function");
    }
    memcpy(copiedBuffer, serializedFunc, serializedSize);

    js_free(ctx, serializedFunc);

    // Get function source for execution in worker (bytecode is not portable across runtimes)
    std::string functionSource;
    JSValue toStringVal = JS_GetPropertyStr(ctx, argv[0], "toString");
    if (JS_IsFunction(ctx, toStringVal)) {
        JSValue sourceVal = JS_Call(ctx, toStringVal, argv[0], 0, nullptr);
        JS_FreeValue(ctx, toStringVal);
        if (!JS_IsException(sourceVal)) {
            const char* src = JS_ToCString(ctx, sourceVal);
            if (src) {
                functionSource.assign(src);
                JS_FreeCString(ctx, src);
            }
            JS_FreeValue(ctx, sourceVal);
        }
    }
    
    // Create task with copied buffer; hold ref to Deferred object so it is not GC'd before completion
    auto task = std::make_shared<DeferredTask>(
        ctx,
        JS_DupValue(ctx, argv[0]),  // Keep reference to original function
        copiedBuffer,                // Copied serialized bytecode (fallback)
        serializedSize,
        resolve,
        reject,
        rt,
        space,
        wrapper,
        JS_DupValue(ctx, obj),      // Keep Deferred alive until completion (keeps then/catch callbacks valid)
        std::move(functionSource)
    );
    
    // Store same shared_ptr in opaque so then/catch see the same task the worker completes
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = new OpaqueHolder{ task };
    JS_SetOpaque(obj, holder);

    executeTaskInWorkerThread(task);

    return obj;
}

void Deferred::finalizer(JSRuntime* rt, JSValue val) {
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = static_cast<OpaqueHolder*>(JS_GetOpaque(val, protojs_deferred_class_id));
    if (holder) {
        std::shared_ptr<DeferredTask> task = holder->task;
        delete holder;
        JS_SetOpaque(val, nullptr);
        if (task) {
            JS_FreeValueRT(rt, task->func);
            JS_FreeValueRT(rt, task->resolve);
            JS_FreeValueRT(rt, task->reject);
            JS_FreeValueRT(rt, task->result);
            JS_FreeValueRT(rt, task->error);
            JS_FreeValueRT(rt, task->thenCallback);
            JS_FreeValueRT(rt, task->catchCallback);
            JS_FreeValueRT(rt, task->deferredObj);
        }
    }
}

JSValue Deferred::then(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "then expects a function");
    }
    
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = static_cast<OpaqueHolder*>(JS_GetOpaque(this_val, protojs_deferred_class_id));
    if (!holder || !holder->task) {
        return JS_ThrowTypeError(ctx, "Invalid Deferred object");
    }
    DeferredTask* task = holder->task.get();
    task->thenCallback = JS_DupValue(ctx, argv[0]);

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
    
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = static_cast<OpaqueHolder*>(JS_GetOpaque(this_val, protojs_deferred_class_id));
    if (!holder || !holder->task) {
        return JS_ThrowTypeError(ctx, "Invalid Deferred object");
    }
    DeferredTask* task = holder->task.get();
    task->catchCallback = JS_DupValue(ctx, argv[0]);

    if (task->isRejected && !JS_IsUndefined(task->error)) {
        JSValue catchArgs[] = { task->error };
        JSValue catchResult = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
        JS_FreeValue(ctx, catchResult);
    }
    
    return JS_DupValue(ctx, this_val); // Return this for chaining
}

static std::atomic<int> active_deferred_count{0};

Deferred::DeferredTask::DeferredTask(JSContext* ctx, JSValue f, uint8_t* serialized, size_t serializedSize,
                                     JSValue res, JSValue rej, JSRuntime* runtime, proto::ProtoSpace* s, JSContextWrapper* w,
                                     JSValue deferred_obj, std::string fn_source)
    : func(f), serializedFunc(serialized), serializedFuncSize(serializedSize),
      resolve(res), reject(rej), mainJSContext(ctx), rt(runtime), space(s), wrapper(w),
      deferredObj(deferred_obj), functionSource(std::move(fn_source)) {}

Deferred::DeferredTask::~DeferredTask() {
    if (serializedFunc) free(serializedFunc);
    if (serializedResult) free(serializedResult);
}

int Deferred::getActiveDeferredCount() {
    return active_deferred_count.load();
}

std::pair<JSValue, Deferred::TaskHandle> Deferred::createPending(JSContext* ctx, JSContextWrapper* wrapper) {
    proto::ProtoSpace* space = wrapper->getProtoSpace();
    JSRuntime* rt = wrapper->getJSRuntime();
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }, "resolve", 1);
    JSValue reject = JS_NewCFunction(ctx, [](JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }, "reject", 1);
    JSValue obj = JS_NewObjectClass(ctx, protojs_deferred_class_id);
    if (JS_IsException(obj)) return { obj, nullptr };
    auto task = std::make_shared<DeferredTask>(
        ctx, JS_UNDEFINED, nullptr, 0, resolve, reject, rt, space, wrapper,
        JS_DupValue(ctx, obj), std::string());
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = new OpaqueHolder{ task };
    JS_SetOpaque(obj, holder);
    return std::make_pair(obj, task);
}

void Deferred::incrementActiveCount() {
    active_deferred_count++;
}

void Deferred::resolveTaskFromNative(TaskHandle task, JSValue resultValue) {
    active_deferred_count--;
    JSContext* ctx = task->mainJSContext;
    task->isResolved = true;
    task->result = JS_DupValue(ctx, resultValue);
    if (!JS_IsUndefined(task->thenCallback)) {
        JSValue thenArgs[] = { task->result };
        JSValue r = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
        JS_FreeValue(ctx, r);
    }
    if (!JS_IsUndefined(task->deferredObj)) {
        JS_FreeValue(ctx, task->deferredObj);
        task->deferredObj = JS_UNDEFINED;
    }
}

void Deferred::executeTaskInWorkerThread(std::shared_ptr<DeferredTask> task) {
    active_deferred_count++;
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
                active_deferred_count--;
                JSContext* ctx = task->mainJSContext;
                task->isRejected = true;
                task->error = JS_NewString(ctx, "Failed to create worker thread runtime");
                if (!JS_IsUndefined(task->catchCallback)) {
                    JSValue catchArgs[] = { task->error };
                    JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                    JS_FreeValue(ctx, r);
                }
                if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
            });
            return;
        }

        workerCtx = JS_NewContext(workerRt);
        if (!workerCtx) {
            JS_FreeRuntime(workerRt);
            workerRt = nullptr;
            task->hasError = true;
            EventLoop::getInstance().enqueueCallback([task]() {
                active_deferred_count--;
                JSContext* ctx = task->mainJSContext;
                task->isRejected = true;
                task->error = JS_NewString(ctx, "Failed to create worker thread context");
                if (!JS_IsUndefined(task->catchCallback)) {
                    JSValue catchArgs[] = { task->error };
                    JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                    JS_FreeValue(ctx, r);
                }
                if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
            });
            return;
        }
    }
    
    try {
        // Prefer function source in worker (bytecode is not portable across runtimes)
        if (!task->functionSource.empty()) {
            std::string evalCode = "var __deferred_fn = " + task->functionSource + "; __deferred_fn();";
            JSValue result = JS_Eval(workerCtx, evalCode.c_str(), evalCode.size(), "(deferred)", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result)) {
                task->hasError = true;
                std::string errorMsg("Unknown error");
                JSValue ex = result;
                JSValue msgVal = JS_GetPropertyStr(workerCtx, ex, "message");
                if (!JS_IsUndefined(msgVal) && !JS_IsException(msgVal)) {
                    const char* errStr = JS_ToCString(workerCtx, msgVal);
                    if (errStr) { errorMsg = errStr; JS_FreeCString(workerCtx, errStr); }
                    JS_FreeValue(workerCtx, msgVal);
                }
                if (errorMsg == "Unknown error") {
                    JSValue strVal = JS_ToString(workerCtx, ex);
                    if (!JS_IsException(strVal)) {
                        const char* errStr = JS_ToCString(workerCtx, strVal);
                        if (errStr) { errorMsg = errStr; JS_FreeCString(workerCtx, errStr); }
                        JS_FreeValue(workerCtx, strVal);
                    }
                }
                JS_FreeValue(workerCtx, result);
                JSValue errorVal = JS_NewString(workerCtx, errorMsg.c_str());
                size_t errorSize = 0;
                uint8_t* serializedError = JS_WriteObject(workerCtx, &errorSize, errorVal, JS_WRITE_OBJ_BYTECODE);
                JS_FreeValue(workerCtx, errorVal);
                if (serializedError && errorSize > 0) {
                    uint8_t* copiedError = static_cast<uint8_t*>(malloc(errorSize));
                    if (copiedError) {
                        memcpy(copiedError, serializedError, errorSize);
                        js_free(workerCtx, serializedError);
                        task->serializedResult = copiedError;
                        task->serializedResultSize = errorSize;
                        EventLoop::getInstance().enqueueCallback([task]() {
                            active_deferred_count--;
                            JSContext* ctx = task->mainJSContext;
                            task->isRejected = true;
                            if (task->serializedResult && task->serializedResultSize > 0) {
                                task->error = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                                if (JS_IsException(task->error)) {
                                    JS_FreeValue(ctx, task->error);
                                    task->error = JS_NewString(ctx, "Function execution failed");
                                }
                            } else {
                                task->error = JS_NewString(ctx, "Function execution failed");
                            }
                            if (!JS_IsUndefined(task->catchCallback)) {
                                JSValue catchArgs[] = { task->error };
                                JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                                JS_FreeValue(ctx, r);
                            }
                            if (task->serializedResult) {
                                free(task->serializedResult);
                                task->serializedResult = nullptr;
                                task->serializedResultSize = 0;
                            }
                            if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
                        });
                    } else {
                        js_free(workerCtx, serializedError);
                        EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                            active_deferred_count--;
                            JSContext* ctx = task->mainJSContext;
                            task->isRejected = true;
                            task->error = JS_NewString(ctx, errorMsg.c_str());
                            if (!JS_IsUndefined(task->catchCallback)) {
                                JSValue catchArgs[] = { task->error };
                                JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                                JS_FreeValue(ctx, r);
                            }
                            if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
                        });
                    }
                } else {
                    EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                        active_deferred_count--;
                        JSContext* ctx = task->mainJSContext;
                        task->isRejected = true;
                        task->error = JS_NewString(ctx, errorMsg.c_str());
                        if (!JS_IsUndefined(task->catchCallback)) {
                            JSValue catchArgs[] = { task->error };
                            JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                            JS_FreeValue(ctx, r);
                        }
                        if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
                    });
                }
                return;
            }
            // Success: serialize result and schedule main-thread callback
            size_t resultSize = 0;
            uint8_t* serializedResult = JS_WriteObject(workerCtx, &resultSize, result, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(workerCtx, result);
            if (serializedResult && resultSize > 0) {
                uint8_t* copiedResult = static_cast<uint8_t*>(malloc(resultSize));
                if (copiedResult) {
                    memcpy(copiedResult, serializedResult, resultSize);
                    js_free(workerCtx, serializedResult);
                    task->serializedResult = copiedResult;
                    task->serializedResultSize = resultSize;
                    task->hasError = false;
                } else {
                    js_free(workerCtx, serializedResult);
                    task->hasError = false;
                    task->serializedResult = nullptr;
                    task->serializedResultSize = 0;
                }
            } else {
                task->hasError = false;
                task->serializedResult = nullptr;
                task->serializedResultSize = 0;
            }
            EventLoop::getInstance().enqueueCallback([task]() {
                active_deferred_count--;
                JSContext* ctx = task->mainJSContext;
                task->isResolved = true;
                if (task->serializedResult && task->serializedResultSize > 0) {
                    task->result = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(task->result)) {
                        JS_FreeValue(ctx, task->result);
                        task->result = JS_UNDEFINED;
                    }
                } else {
                    task->result = JS_UNDEFINED;
                }
                if (!JS_IsUndefined(task->thenCallback)) {
                    JSValue thenArgs[] = { task->result };
                    JSValue thenResult = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
                    JS_FreeValue(ctx, thenResult);
                }
                if (task->serializedResult) {
                    free(task->serializedResult);
                    task->serializedResult = nullptr;
                    task->serializedResultSize = 0;
                }
                if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
            });
            return;
        }

        // Deserialize function from bytecode (fallback)
        JSValue func = JS_ReadObject(workerCtx, task->serializedFunc, task->serializedFuncSize, JS_READ_OBJ_BYTECODE);
        
        if (JS_IsException(func)) {
            // Deserialization failed - func is the exception
            task->hasError = true;
            const char* errStr = JS_ToCString(workerCtx, func);
            std::string errorMsg = errStr ? errStr : "Failed to deserialize function";
            JS_FreeCString(workerCtx, errStr);
            JS_FreeValue(workerCtx, func);
            
            EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                active_deferred_count--;
                JSContext* ctx = task->mainJSContext;
                task->isRejected = true;
                task->error = JS_NewString(ctx, errorMsg.c_str());
                if (!JS_IsUndefined(task->catchCallback)) {
                    JSValue catchArgs[] = { task->error };
                    JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                    JS_FreeValue(ctx, r);
                }
                if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
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
            task->hasError = true;
            std::string errorMsg("Unknown error");
            JSValue ex = result;
            JSValue msgVal = JS_GetPropertyStr(workerCtx, ex, "message");
            if (!JS_IsUndefined(msgVal) && !JS_IsException(msgVal)) {
                const char* errStr = JS_ToCString(workerCtx, msgVal);
                if (errStr) {
                    errorMsg = errStr;
                    JS_FreeCString(workerCtx, errStr);
                }
                JS_FreeValue(workerCtx, msgVal);
            }
            if (errorMsg == "Unknown error") {
                JSValue strVal = JS_ToString(workerCtx, ex);
                if (!JS_IsException(strVal)) {
                    const char* errStr = JS_ToCString(workerCtx, strVal);
                    if (errStr) {
                        errorMsg = errStr;
                        JS_FreeCString(workerCtx, errStr);
                    }
                    JS_FreeValue(workerCtx, strVal);
                }
            }
            JS_FreeValue(workerCtx, result);
            JSValue errorVal = JS_NewString(workerCtx, errorMsg.c_str());
            size_t errorSize = 0;
            uint8_t* serializedError = JS_WriteObject(workerCtx, &errorSize, errorVal, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(workerCtx, errorVal);
            if (serializedError && errorSize > 0) {
                uint8_t* copiedError = static_cast<uint8_t*>(malloc(errorSize));
                if (copiedError) {
                    memcpy(copiedError, serializedError, errorSize);
                    js_free(workerCtx, serializedError);
                    task->serializedResult = copiedError;
                    task->serializedResultSize = errorSize;
                    EventLoop::getInstance().enqueueCallback([task]() {
                        active_deferred_count--;
                        JSContext* ctx = task->mainJSContext;
                        task->isRejected = true;
                        if (task->serializedResult && task->serializedResultSize > 0) {
                            task->error = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                            if (JS_IsException(task->error)) {
                                JS_FreeValue(ctx, task->error);
                                task->error = JS_NewString(ctx, "Function execution failed");
                            }
                        } else {
                            task->error = JS_NewString(ctx, "Function execution failed");
                        }
                        if (!JS_IsUndefined(task->catchCallback)) {
                            JSValue catchArgs[] = { task->error };
                            JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                            JS_FreeValue(ctx, r);
                        }
                        if (task->serializedResult) {
                            free(task->serializedResult);
                            task->serializedResult = nullptr;
                            task->serializedResultSize = 0;
                        }
                        if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
                    });
                    JS_FreeValue(workerCtx, func);
                    return;
                } else {
                    // Failed to allocate - fallback to error message
                    js_free(workerCtx, serializedError);
                    const char* errStr = "Function execution failed (serialization error)";
                    std::string errorMsg = errStr;
                    
                    EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                        active_deferred_count--;
                        JSContext* ctx = task->mainJSContext;
                        task->isRejected = true;
                        task->error = JS_NewString(ctx, errorMsg.c_str());
                        if (!JS_IsUndefined(task->catchCallback)) {
                            JSValue catchArgs[] = { task->error };
                            JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                            JS_FreeValue(ctx, r);
                        }
                        if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
                    });
                }
                JS_FreeValue(workerCtx, func);
                return;
            } else {
                std::string errorMsg("Function execution failed (serialization error)");
                EventLoop::getInstance().enqueueCallback([task, errorMsg]() {
                    active_deferred_count--;
                    JSContext* ctx = task->mainJSContext;
                    task->isRejected = true;
                    task->error = JS_NewString(ctx, errorMsg.c_str());
                    if (!JS_IsUndefined(task->catchCallback)) {
                        JSValue catchArgs[] = { task->error };
                        JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                        JS_FreeValue(ctx, r);
                    }
                    if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
                });
                JS_FreeValue(workerCtx, func);
                return;
            }
        } else {
            // Serialize the result
            size_t resultSize = 0;
            uint8_t* serializedResult = JS_WriteObject(workerCtx, &resultSize, result, JS_WRITE_OBJ_BYTECODE);
            JS_FreeValue(workerCtx, result);
            
            if (serializedResult && resultSize > 0) {
                // Copy the serialized result to main runtime's memory
                uint8_t* copiedResult = static_cast<uint8_t*>(malloc(resultSize));
                if (copiedResult) {
                    memcpy(copiedResult, serializedResult, resultSize);
                    js_free(workerCtx, serializedResult);
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
            active_deferred_count--;
            JSContext* ctx = task->mainJSContext;

            if (task->hasError) {
                task->isRejected = true;
                if (task->serializedResult && task->serializedResultSize > 0) {
                    task->error = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(task->error)) {
                        JS_FreeValue(ctx, task->error);
                        task->error = JS_NewString(ctx, "Function execution failed");
                    }
                } else {
                    task->error = JS_NewString(ctx, "Function execution failed");
                }
                if (!JS_IsUndefined(task->catchCallback)) {
                    JSValue catchArgs[] = { task->error };
                    JSValue catchResult = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                    JS_FreeValue(ctx, catchResult);
                }
            } else {
                task->isResolved = true;
                if (task->serializedResult && task->serializedResultSize > 0) {
                    task->result = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
                    if (JS_IsException(task->result)) {
                        JS_FreeValue(ctx, task->result);
                        task->result = JS_UNDEFINED;
                    }
                } else {
                    task->result = JS_UNDEFINED;
                }
                if (!JS_IsUndefined(task->thenCallback)) {
                    JSValue thenArgs[] = { task->result };
                    JSValue thenResult = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
                    JS_FreeValue(ctx, thenResult);
                }
            }
            
            if (task->serializedResult) {
                free(task->serializedResult);
                task->serializedResult = nullptr;
                task->serializedResultSize = 0;
            }
            if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
        });
        
    } catch (const std::exception& e) {
        std::string errorMessage = e.what();
        EventLoop::getInstance().enqueueCallback([task, errorMessage]() {
            active_deferred_count--;
            JSContext* ctx = task->mainJSContext;
            task->isRejected = true;
            task->error = JS_NewString(ctx, errorMessage.c_str());
            if (!JS_IsUndefined(task->catchCallback)) {
                JSValue catchArgs[] = { task->error };
                JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                JS_FreeValue(ctx, r);
            }
            if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
        });
    }
}

// Old executeTask removed - using executeTaskInMainThread instead

} // namespace protojs
