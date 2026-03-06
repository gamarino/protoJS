#include "JSContext.h"
#include "CPUThreadPool.h"
#include "IOThreadPool.h"
#include "EventLoop.h"
#include "GCBridge.h"
#include "ExecutionEngine.h"
#include "debugging/IntegratedDebugger.h"
#include "JSPrototypes.h"
#include "TypeBridge.h"
#include "runtime/ProtoCompileOnly.h"
#include "runtime/ProtoBytecodeModule.h"
#include "runtime/ProtoInterpreter.h"
#include "quickjs.h"
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

const proto::ProtoObject* JSContextWrapper::getNativeGlobal(proto::ProtoContext* frameCtx) {
    if (nativeGlobalRoot_) return nativeGlobalRoot_;
    if (!jsPrototypes_.object || !pContext) return nullptr;
    const proto::ProtoObject* obj = jsPrototypes_.object->newChild(pContext, true);
    if (!obj) return nullptr;
    JSValue globalVal = JS_GetGlobalObject(ctx);
    JSPropertyEnum* props = nullptr;
    uint32_t len = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &len, globalVal, JS_GPN_STRING_MASK) != 0) {
        JS_FreeValue(ctx, globalVal);
        return nullptr;
    }
    // Use pContext (wrapper root) for fromJS so that GCBridge reverse lookup by protoObj hash
    // uses the same context and finds host functions (e.g. console.log).
    proto::ProtoContext* bridgeCtx = pContext;
    for (uint32_t i = 0; i < len; i++) {
        JSAtom atom = props[i].atom;
        const char* name = JS_AtomToCString(ctx, atom);
        if (!name) continue;
        JSValue val = JS_GetProperty(ctx, globalVal, atom);
        const proto::ProtoObject* pv = TypeBridge::fromJS(ctx, val, bridgeCtx);
        JS_FreeValue(ctx, val);
        const proto::ProtoString* key = pContext->fromUTF8String(name)->asString(pContext);
        obj = obj->setAttribute(pContext, key, pv ? pv : PROTO_NONE);
        JS_FreeCString(ctx, name);
    }
    JS_FreePropertyEnum(ctx, props, len);
    JS_FreeValue(ctx, globalVal);
    nativeGlobalRoot_ = obj;
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

    void* bytecode = protojs::compileToBytecode(ctx, code.c_str(), code.size(), filename.c_str(), &val);
    if (!bytecode) {
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
    if (bytecode && bytecode != reinterpret_cast<void*>(1)) {
        protojs::ProtoBytecodeModule module;
        if (!protojs::loadBytecode(ctx, bytecode, &frameCtx, &module)) {
            std::cerr << "[protojs] loadBytecode failed" << std::endl;
            val = JS_EXCEPTION;
        } else {
            const proto::ProtoObject* globalObj = getNativeGlobal(&frameCtx);
            if (!globalObj) {
                JS_ThrowTypeError(ctx, "Failed to build native global (Phase 6)");
                val = JS_GetException(ctx);
            } else {
            std::cerr << "[protojs] about to runBytecode" << std::endl;
            const proto::ProtoObject* result =
                protojs::runBytecode(&frameCtx, &module, globalObj, nullptr, &nativeGlobalRoot_, ctx);
            // Hand the result back to the previous context for GC purposes.
            frameCtx.returnValue = result;

            val = TypeBridge::toJS(ctx, result, &frameCtx);
            }
        }
    }

    std::cerr << "[protojs] eval done: " << filename << std::endl;
    IntegratedDebugger::popFrame();
    pContext->currentFileName = nullptr;
    pContext->currentLineNumber = 0;
    currentScript_.clear();

    // When !bytecode (and not fallback success), val is the exception; report it.
    const bool fallbackSuccess = (bytecode == reinterpret_cast<void*>(1));
    if ((JS_IsException(val) || !bytecode) && !fallbackSuccess) {
        const char* str = JS_ToCString(ctx, val);
        if (!str && JS_IsObject(val)) {
            JSValue msg = JS_GetPropertyStr(ctx, val, "message");
            str = JS_ToCString(ctx, msg);
            JS_FreeValue(ctx, msg);
        }
        if (str) {
            std::cerr << (bytecode ? "Exception" : "Compile failed") << " in " << filename << ": " << str << std::endl;
            JS_FreeCString(ctx, str);
        } else {
            int tag = JS_VALUE_GET_TAG(val);
            std::cerr << (bytecode ? "Exception" : "Compile failed") << " in " << filename << " (tag=" << tag << ", use JS_TAG_* in quickjs.h)" << std::endl;
        }
    }

    return val;
}

} // namespace protojs
