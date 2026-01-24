#include "Deferred.h"
#include "TypeBridge.h"
#include "JSContext.h"
#include <iostream>
#include <memory>

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

    // Opción B: Función JS se ejecuta en thread principal
    // El trabajo pesado se delega a protoCore en worker thread
    // Para Fase 1, simplificamos: siempre ejecutar función en main thread
    // y si detecta trabajo pesado, delegarlo a protoCore
    
    // Create task - función se ejecutará en main thread
    auto task = std::make_shared<DeferredTask>(
        ctx,
        JS_DupValue(ctx, argv[0]),  // Función JS
        std::string(),  // funcCode no usado en Opción B
        resolve,
        reject,
        rt,
        space,
        wrapper
    );
    
    // Store task in object
    DeferredTask* taskPtr = new DeferredTask(*task);
    JS_SetOpaque(obj, taskPtr);

    // Opción B: Ejecutar función en main thread primero
    // Si detecta trabajo pesado, delegarlo a worker thread
    // Por ahora, siempre ejecutamos en main thread y delegamos trabajo a protoCore
    executeTaskInMainThread(task);

    return obj;
}

void Deferred::finalizer(JSRuntime* rt, JSValue val) {
    DeferredTask* task = static_cast<DeferredTask*>(JS_GetOpaque(val, protojs_deferred_class_id));
    if (task) {
        JS_FreeValueRT(rt, task->func);
        JS_FreeValueRT(rt, task->resolve);
        JS_FreeValueRT(rt, task->reject);
        delete task;
    }
}

void Deferred::executeTaskInMainThread(std::shared_ptr<DeferredTask> task) {
    // Opción B: Ejecutar función JS en main thread
    // Si detecta trabajo CPU-intensivo, delegarlo a protoCore en worker thread
    
    try {
        JSContext* ctx = task->mainJSContext;
        
        // Create resolve and reject callbacks that will handle results
        struct TaskResult {
            JSValue value = JS_UNDEFINED;
            bool isError = false;
            std::string errorMessage;
        };
        
        TaskResult* taskResult = new TaskResult();
        
        JSValue resolveFunc = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
            TaskResult* result = static_cast<TaskResult*>(JS_GetContextOpaque(ctx));
            if (result && argc > 0) {
                result->value = JS_DupValue(ctx, argv[0]);
                result->isError = false;
            }
            return JS_UNDEFINED;
        }, "resolve", 1);
        
        JSValue rejectFunc = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
            TaskResult* result = static_cast<TaskResult*>(JS_GetContextOpaque(ctx));
            if (result && argc > 0) {
                const char* errStr = JS_ToCString(ctx, argv[0]);
                if (errStr) {
                    result->errorMessage = errStr;
                    JS_FreeCString(ctx, errStr);
                }
                result->isError = true;
            }
            return JS_UNDEFINED;
        }, "reject", 1);
        
        JS_SetContextOpaque(ctx, taskResult);
        
        // Call the user's function with resolve and reject
        JSValue funcArgs[] = { resolveFunc, rejectFunc };
        JSValue callResult = JS_Call(ctx, task->func, JS_UNDEFINED, 2, funcArgs);
        
        // Check for exceptions
        if (JS_IsException(callResult)) {
            JSValue exception = JS_GetException(ctx);
            const char* errStr = JS_ToCString(ctx, exception);
            if (errStr) {
                taskResult->errorMessage = errStr;
                JS_FreeCString(ctx, errStr);
            }
            JS_FreeValue(ctx, exception);
            taskResult->isError = true;
        }
        
        JS_FreeValue(ctx, callResult);
        JS_FreeValue(ctx, resolveFunc);
        JS_FreeValue(ctx, rejectFunc);
        
        // If function returned a value (synchronous), handle it
        // If function called resolve/reject, taskResult will have the value
        if (!taskResult->isError && JS_IsUndefined(taskResult->value)) {
            // Function didn't call resolve/reject yet - might be async
            // For now, assume it will call resolve/reject
            // In full implementation, we'd wait or handle async case
        } else {
            // Function completed (called resolve or reject)
            if (taskResult->isError) {
                // Call reject callback
                JSValue error = JS_NewString(ctx, taskResult->errorMessage.c_str());
                JSValue rejectArgs[] = { error };
                JSValue rejectResult = JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
                JS_FreeValue(ctx, rejectResult);
                JS_FreeValue(ctx, error);
            } else {
                // Call resolve callback
                JSValue resolveArgs[] = { taskResult->value };
                JSValue resolveResult = JS_Call(ctx, task->resolve, JS_UNDEFINED, 1, resolveArgs);
                JS_FreeValue(ctx, resolveResult);
                JS_FreeValue(ctx, taskResult->value);
            }
        }
        
        delete taskResult;
        
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

void Deferred::executeWorkInWorkerThread(std::shared_ptr<DeferredTask> task, 
                                         const proto::ProtoObject* workFunction) {
    // Execute heavy work in protoCore on worker thread
    // This would be called when the JS function detects CPU-intensive work
    // For Fase 1, this is a placeholder
    try {
        proto::ProtoContext* taskContext = new proto::ProtoContext(
            task->space,
            nullptr, nullptr, nullptr, nullptr, nullptr
        );
        
        // Execute workFunction in protoCore
        // This would be a ProtoMethod that does the heavy computation
        // For now, placeholder
        
        delete taskContext;
    } catch (const std::exception& e) {
        // Handle error
    }
}

// Old executeTask removed - using executeTaskInMainThread instead

} // namespace protojs
