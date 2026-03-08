#include "JSContext.h"
#include "CPUThreadPool.h"
#include "IOThreadPool.h"
#include "EventLoop.h"
#include "GCBridge.h"
#include "ExecutionEngine.h"
#include "debugging/IntegratedDebugger.h"
#include "JSPrototypes.h"
#include "TypeBridge.h"
#include "ProtoJSStringCache.h"
#include "runtime/ProtoCompileOnly.h"
#include "runtime/ProtoBytecodeModule.h"
#include "runtime/ProtoInterpreter.h"
#include "quickjs.h"
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace protojs {

// See JSContext.h for semantics.
JSContextWrapper::JSContextWrapper(size_t cpuThreads, size_t ioThreads, double ioFactor) : pSpace() {
    std::cerr << "[protojs] JSContextWrapper: creating QuickJS runtime/context" << std::endl;
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    std::cerr << "[protojs] JSContextWrapper: QuickJS context ready" << std::endl;
    
    // Initialize protoCore root context
    pContext = pSpace.rootContext;
    std::cerr << "[protojs] JSContextWrapper: protoCore context ready" << std::endl;
    
    // Bootstrap JS Object and derived prototypes (array, arguments, regexp)
    BootstrapJSPrototypes(&pSpace, pContext, &jsPrototypes_);
    std::cerr << "[protojs] JSContextWrapper: JS prototypes bootstrapped" << std::endl;
    
    // Initialize GCBridge for this context
    GCBridge::initialize(ctx);
    std::cerr << "[protojs] JSContextWrapper: GCBridge initialized" << std::endl;
    
    // ExecutionEngine stub: getProtoContext only (no legacy hooks; single protoCore path).
    ExecutionEngine::initialize(ctx, pContext);
    std::cerr << "[protojs] JSContextWrapper: ExecutionEngine initialized" << std::endl;
    
    // Store pointer to this wrapper in JSContext opaque for GCBridge access
    JS_SetContextOpaque(ctx, this);
    std::cerr << "[protojs] JSContextWrapper: context opaque set" << std::endl;
    
    // Initialize thread pools
    if (cpuThreads > 0) {
        CPUThreadPool::initialize(cpuThreads);
    } else {
        CPUThreadPool::initialize(); // Use default (CPU count)
    }
    std::cerr << "[protojs] JSContextWrapper: CPUThreadPool initialized" << std::endl;
    
    if (ioThreads > 0) {
        IOThreadPool::initialize(ioThreads);
    } else {
        IOThreadPool::initialize(0, ioFactor); // Use default with factor
    }
    std::cerr << "[protojs] JSContextWrapper: IOThreadPool initialized" << std::endl;
    
    // Event loop is initialized on first access (singleton)
    std::cerr << "[protojs] JSContextWrapper: constructor done" << std::endl;
}

const proto::ProtoObject* JSContextWrapper::getNativeGlobal() {
    if (nativeGlobalRoot_) return nativeGlobalRoot_;
    if (!jsPrototypes_.object || !pContext) return nullptr;
    /* Build a blank global object; converted modules register onto it explicitly. */
    nativeGlobalRoot_ = jsPrototypes_.object->newChild(pContext, true);
    return nativeGlobalRoot_;
}

JSContextWrapper::~JSContextWrapper() {
    // Cleanup ExecutionEngine
    ExecutionEngine::cleanup(ctx);
    
    // Cleanup GCBridge mappings
    GCBridge::cleanup(ctx);
    
    // Shutdown thread pools
    CPUThreadPool::shutdown();
    IOThreadPool::shutdown();
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

JSValue JSContextWrapper::eval(const std::string& code, const std::string& filename) {
    currentScript_ = filename;
    pContext->currentFileName = currentScript_.empty() ? nullptr : &currentScript_[0];
    pContext->currentLineNumber = 1;

    IntegratedDebugger::pushFrame(ctx);
    if (IntegratedDebugger::checkBreakpoint(filename, 1)) {
        IntegratedDebugger::pauseExecution();
    }

    std::cerr << "[protojs] eval start: " << filename << std::endl;
    // Clear any pending exception from earlier (e.g. module init) so compile sees a clean context.
    if (JS_HasException(ctx)) {
        JSValue stale = JS_GetException(ctx);
        JS_FreeValue(ctx, stale);
    }
    JSValue val;
    // Single path: compile → load → run (protoCore interpreter). No legacy JS_Eval.
    proto::ProtoContext frameCtx(&pSpace, pContext, nullptr, nullptr, nullptr, nullptr);
    frameCtx.currentFileName = pContext->currentFileName;
    frameCtx.currentLineNumber = pContext->currentLineNumber;

    bool hadError = false;
    void* bytecode = protojs::compileToBytecode(ctx, code.c_str(), code.size(), filename.c_str(), nullptr);
    if (!bytecode) {
        val = JS_GetException(ctx);
        const char* noFallback = std::getenv("PROTOJS_NO_FALLBACK");
        if (noFallback && (noFallback[0] == '1' || noFallback[0] == 't' || noFallback[0] == 'T')) {
            std::cerr << "[protojs] compile failed (no fallback)" << std::endl;
            hadError = true;
        } else {
            std::cerr << "[protojs] compile failed, fallback to QuickJS eval" << std::endl;
            JS_FreeValue(ctx, val);
            JSValue runVal = JS_Eval(ctx, code.c_str(), code.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(runVal)) {
                val = JS_GetException(ctx);
            } else {
                val = runVal;
                bytecode = reinterpret_cast<void*>(1); /* success: do not report as compile failed */
            }
        }
    }
    if (bytecode && bytecode != reinterpret_cast<void*>(1)) {
        protojs::ProtoBytecodeModule module;
        if (!protojs::loadBytecode(ctx, bytecode, &frameCtx, &module)) {
            std::cerr << "[protojs] loadBytecode failed" << std::endl;
            hadError = true;
            val = JS_EXCEPTION;
        } else {
            const proto::ProtoObject* globalObj = getNativeGlobal();
            if (!globalObj) {
                std::cerr << "[protojs] getNativeGlobal failed" << std::endl;
                hadError = true;
                val = JS_EXCEPTION;
            } else {
                std::cerr << "[protojs] about to runBytecode" << std::endl;
                const proto::ProtoObject* exception = PROTO_NONE;
                const proto::ProtoObject* result =
                    protojs::runBytecode(&frameCtx, &module, globalObj, nullptr,
                                         &nativeGlobalRoot_, &exception);
                frameCtx.returnValue = result;

                if (exception && exception != PROTO_NONE) {
                    /* Format exception from ProtoObject for error reporting. */
                    std::string errStr;
                    const proto::ProtoString* nameKey =
                        ProtoJSStringCache::getKey(&frameCtx, "name");
                    const proto::ProtoString* msgKey =
                        ProtoJSStringCache::getKey(&frameCtx, "message");
                    if (nameKey) {
                        const proto::ProtoObject* nv =
                            exception->getAttribute(&frameCtx, nameKey, false);
                        if (nv && nv != PROTO_NONE && nv->isString(&frameCtx)) {
                            std::string tmp;
                            nv->asString(&frameCtx)->toUTF8String(&frameCtx, tmp);
                            errStr = tmp;
                        }
                    }
                    if (msgKey) {
                        const proto::ProtoObject* mv =
                            exception->getAttribute(&frameCtx, msgKey, false);
                        if (mv && mv != PROTO_NONE && mv->isString(&frameCtx)) {
                            std::string tmp;
                            mv->asString(&frameCtx)->toUTF8String(&frameCtx, tmp);
                            if (!errStr.empty()) errStr += ": ";
                            errStr += tmp;
                        }
                    }
                    if (errStr.empty() && exception->isString(&frameCtx)) {
                        const proto::ProtoString* s = exception->asString(&frameCtx);
                        if (s) s->toUTF8String(&frameCtx, errStr);
                    }
                    if (!errStr.empty())
                        std::cerr << "Exception in " << filename << ": " << errStr << std::endl;
                    else
                        std::cerr << "Exception in " << filename << " (ProtoObject)" << std::endl;
                    hadError = true;
                    val = JS_EXCEPTION;
                } else {
                    val = TypeBridge::toJS(ctx, result, &frameCtx);
                }
            }
        }
    }

    std::cerr << "[protojs] eval done: " << filename << std::endl;
    IntegratedDebugger::popFrame();
    pContext->currentFileName = nullptr;
    pContext->currentLineNumber = 0;
    currentScript_.clear();

    const bool fallbackSuccess = (bytecode == reinterpret_cast<void*>(1));
    if (hadError || ((JS_IsException(val) || !bytecode) && !fallbackSuccess)) {
        /* If val is the JS_EXCEPTION sentinel (proto runtime exception, already printed),
         * skip JS property access to avoid undefined behaviour on the sentinel JSValue. */
        const bool valIsActualJSException = !JS_IsException(val) ||
                                            JS_HasException(ctx);
        if (valIsActualJSException) {
            std::string errStr;
            /* Prefer name + message so Test262 sees "ReferenceError: ..." in stderr. */
            JSValue nameVal = JS_GetPropertyStr(ctx, val, "name");
            JSValue msgVal  = JS_GetPropertyStr(ctx, val, "message");
            const char* name = (!JS_IsUndefined(nameVal) && !JS_IsException(nameVal))
                               ? JS_ToCString(ctx, nameVal) : nullptr;
            const char* msg  = (!JS_IsUndefined(msgVal)  && !JS_IsException(msgVal))
                               ? JS_ToCString(ctx, msgVal) : nullptr;
            if (name || msg) {
                errStr = name ? name : "";
                if (msg) errStr += (errStr.empty() ? "" : ": ") + std::string(msg);
            }
            if (name) JS_FreeCString(ctx, name);
            if (msg)  JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, nameVal);
            JS_FreeValue(ctx, msgVal);
            if (errStr.empty()) {
                const char* str = JS_ToCString(ctx, val);
                if (str) { errStr = str; JS_FreeCString(ctx, str); }
            }
            if (!errStr.empty()) {
                std::cerr << (bytecode ? "Exception" : "Compile failed")
                          << " in " << filename << ": " << errStr << std::endl;
            } else if (!hadError) {
                int tag = JS_VALUE_GET_TAG(val);
                std::cerr << (bytecode ? "Exception" : "Compile failed")
                          << " in " << filename << " (tag=" << tag << ")" << std::endl;
            }
            if (!JS_IsException(val)) JS_FreeValue(ctx, val);
        }
        return JS_EXCEPTION;
    }

    return val;
}

} // namespace protojs
