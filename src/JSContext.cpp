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
#include <iostream>

namespace protojs {

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
    
    // Initialize ExecutionEngine
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
    JSValue val;
    if (useProtoEval_) {
        void* bytecode = protojs::compileToBytecode(ctx, code.c_str(), code.size(), filename.c_str());
        if (!bytecode) {
            val = JS_GetException(ctx);
        } else {
            protojs::ProtoBytecodeModule module;
            if (!protojs::loadBytecode(ctx, bytecode, pContext, &module)) {
                val = JS_EXCEPTION;
            } else {
                JSValue globalVal = JS_GetGlobalObject(ctx);
                const proto::ProtoObject* globalObj = TypeBridge::fromJS(ctx, globalVal, pContext);
                JS_FreeValue(ctx, globalVal);
                const proto::ProtoObject* result = protojs::runBytecode(pContext, &module, globalObj, ctx);
                val = TypeBridge::toJS(ctx, result, pContext);
            }
        }
    } else {
        val = JS_Eval(ctx, code.c_str(), code.length(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    }

    std::cerr << "[protojs] eval done: " << filename << std::endl;
    IntegratedDebugger::popFrame();
    pContext->currentFileName = nullptr;
    pContext->currentLineNumber = 0;
    currentScript_.clear();

    if (JS_IsException(val)) {
        JSValue exception = JS_GetException(ctx);
        const char* str = JS_ToCString(ctx, exception);
        if (str) {
            std::cerr << "Exception in " << filename << ": " << str << std::endl;
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, exception);
    }

    return val;
}

} // namespace protojs
