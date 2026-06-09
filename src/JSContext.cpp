#include "JSContext.h"
#include "CPUThreadPool.h"
#include "IOThreadPool.h"
#include "EventLoop.h"
#include "GCBridge.h"
#include "ExecutionEngine.h"
#include "JSONBuiltin.h"

#include "debugging/IntegratedDebugger.h"
#include "JSPrototypes.h"
#include "TypeBridge.h"
#include "JSSymbols.h"
#include "runtime/ProtoCompileOnly.h"
#include "runtime/ProtoBytecodeModule.h"
#include "runtime/ProtoInterpreter.h"
#include "quickjs.h"
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>

namespace protojs {

/**
 * Filesystem module normalizer: resolves a module specifier relative to the
 * importing module's path. Returns a js_malloc'd absolute path string.
 */
static char* protojs_normalize_module(JSContext* ctx, const char* base_name,
                                      const char* name, void* /*opaque*/) {
    if (name[0] == '/') {
        // Already absolute — canonicalize in place.
        char buf[PATH_MAX];
        if (realpath(name, buf)) return js_strdup(ctx, buf);
        return js_strdup(ctx, name);
    }
    std::string base(base_name ? base_name : "");
    std::string joined;
    size_t slash = base.rfind('/');
    if (slash != std::string::npos) {
        joined = base.substr(0, slash + 1) + name;
    } else {
        joined = name;
    }
    // Canonicalize: resolve any "." or ".." components so that
    // "./foo.js" imported from "/path/to/foo.js" maps to the same
    // identity key "/path/to/foo.js" that QuickJS already has cached.
    char buf[PATH_MAX];
    if (realpath(joined.c_str(), buf)) return js_strdup(ctx, buf);
    // File may not exist yet (e.g., synthetic modules); return the joined path.
    return js_strdup(ctx, joined.c_str());
}

/**
 * Filesystem module loader: reads a module file from disk and compiles it.
 * Returns the JSModuleDef* on success, nullptr on failure (exception set).
 */
static JSModuleDef* protojs_load_module(JSContext* ctx, const char* module_name,
                                        void* /*opaque*/) {
    std::ifstream file(module_name);
    if (!file.is_open()) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", module_name);
        return nullptr;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string src = ss.str();

    JSValue val = JS_Eval(ctx, src.c_str(), src.size(), module_name,
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(val)) {
        return nullptr;
    }
    return static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(val));
}

static thread_local JSContextWrapper* t_currentWrapper = nullptr;

JSContextWrapper* JSContextWrapper::current() {
    return t_currentWrapper;
}

JSContextWrapper::CurrentScope::CurrentScope(JSContextWrapper* w)
    : prev_(t_currentWrapper) {
    t_currentWrapper = w;
}

JSContextWrapper::CurrentScope::~CurrentScope() {
    t_currentWrapper = prev_;
}

// See JSContext.h for semantics.
JSContextWrapper::JSContextWrapper(size_t cpuThreads, size_t ioThreads, double ioFactor) : pSpace() {
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    
    // Initialize protoCore root context
    pContext = pSpace.rootContext;
    
    // Bootstrap JS Object and derived prototypes (array, arguments, regexp)
    BootstrapJSPrototypes(&pSpace, pContext, &jsPrototypes_);
    
    // Initialize GCBridge for this context
    GCBridge::initialize(ctx);
    
    // ExecutionEngine stub: getProtoContext only (no legacy hooks; single protoCore path).
    ExecutionEngine::initialize(ctx, pContext);

    // Eagerly initialize the null sentinel so TypeBridge null round-trips work
    // before the first script execution. The script bootstrap path also initializes
    protojs::initializeNullSentinel(pContext);
    protojs::initializeUndefinedSentinel(pContext);
    
    // Store pointer to this wrapper in JSContext opaque for GCBridge access
    JS_SetContextOpaque(ctx, this);
    
    // Initialize thread pools
    if (cpuThreads > 0) {
        CPUThreadPool::initialize(cpuThreads);
    } else {
        CPUThreadPool::initialize(); // Use default (CPU count)
    }
    
    if (ioThreads > 0) {
        IOThreadPool::initialize(ioThreads);
    } else {
        IOThreadPool::initialize(0, ioFactor); // Use default with factor
    }
    
    // Event loop is initialized on first access (singleton)
}

const proto::ProtoObject* JSContextWrapper::getNativeGlobal() {
    if (nativeGlobalRoot_) return nativeGlobalRoot_;
    if (!jsPrototypes_.object || !pContext) return nullptr;
    /* Build a blank global object; converted modules register onto it explicitly. */
    nativeGlobalRoot_ = jsPrototypes_.object->newChild(pContext, true);
    
    // Inject missing ES5 globals
    const proto::ProtoString* infinityStr = proto::ProtoString::createSymbol(pContext, "Infinity");
    const proto::ProtoString* nanStr = proto::ProtoString::createSymbol(pContext, "NaN");
    const proto::ProtoString* undefinedStr = proto::ProtoString::createSymbol(pContext, "undefined");
    const proto::ProtoString* symbolStr = proto::ProtoString::createSymbol(pContext, "Symbol");

    nativeGlobalRoot_ = nativeGlobalRoot_->setAttribute(pContext, infinityStr, pContext->fromDouble(INFINITY));
    nativeGlobalRoot_ = nativeGlobalRoot_->setAttribute(pContext, nanStr, pContext->fromDouble(NAN));
    
    const proto::ProtoObject* undefSentinel = protojs::getUndefinedSentinel();
    if (undefSentinel) {
        nativeGlobalRoot_ = nativeGlobalRoot_->setAttribute(pContext, undefinedStr, undefSentinel);
    }
    
    return nativeGlobalRoot_;
}

proto::ProtoRootSet* JSContextWrapper::getRootSet() {
    if (rootSet_) return rootSet_;
    rootSet_ = pSpace.createRootSet("protojs-async");
    return rootSet_;
}

JSContextWrapper::~JSContextWrapper() {
    // Release the async-callback root set before tearing down the
    // protoCore space (the space owns the set and will free orphans
    // in its destructor, but explicit cleanup is cheaper and clearer).
    if (rootSet_) {
        pSpace.destroyRootSet(rootSet_);
        rootSet_ = nullptr;
    }

    // Cleanup CommonJS module cache
    for (auto& pair : cjsCache_) {
        JS_FreeValue(ctx, pair.second);
    }
    cjsCache_.clear();

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

JSValue JSContextWrapper::eval(const std::string& code, const std::string& filename, bool isModule) {
    currentScript_ = filename;
    pContext->currentFileName = currentScript_.empty() ? nullptr : &currentScript_[0];
    pContext->currentLineNumber = 1;

    IntegratedDebugger::pushFrame(pContext);

    if (IntegratedDebugger::checkBreakpoint(filename, 1)) {
        IntegratedDebugger::pauseExecution();
    }

    // Clear any pending exception from earlier so compile sees a clean context.
    if (JS_HasException(ctx)) {
        JSValue stale = JS_GetException(ctx);
        JS_FreeValue(ctx, stale);
    }

    // Module mode: QuickJS handles modules natively (protoCore does not implement
    // module semantics — import/export linking, namespace objects, TLA, etc.).
    // Follow the qjs.c pattern: compile-only first, then JS_EvalFunction, then
    // drain pending microjobs and inspect the Promise result.
    if (isModule) {
        JS_SetModuleLoaderFunc(rt, protojs_normalize_module, protojs_load_module, nullptr);

        // Step 1: compile
        JSValue compiled = JS_Eval(ctx, code.c_str(), code.size(), filename.c_str(),
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            // Compile/parse error (SyntaxError etc.)
            goto module_error;
        }

        {
            // Step 2: evaluate (returns a Promise)
            JSValue promise = JS_EvalFunction(ctx, compiled);

            if (JS_IsException(promise)) {
                goto module_error;
            }

            // Step 3: drain pending microjobs so synchronous promise settlement completes
            {
                JSContext* jobCtx = nullptr;
                while (JS_ExecutePendingJob(rt, &jobCtx) > 0) {}
            }

            // Step 4: inspect Promise state
            const int state = JS_PromiseState(ctx, promise);
            if (state == JS_PROMISE_REJECTED) {
                JSValue reason = JS_PromiseResult(ctx, promise);
                JS_FreeValue(ctx, promise);
                JS_Throw(ctx, reason); // set as pending exception
                goto module_error;
            }

            JS_FreeValue(ctx, promise);
        }

        IntegratedDebugger::popFrame();
        pContext->currentFileName = nullptr;
        pContext->currentLineNumber = 0;
        currentScript_.clear();
        return JS_UNDEFINED;

    module_error:
        {
            JSValue exc = JS_GetException(ctx);
            std::string errStr;
            JSValue nameVal = JS_GetPropertyStr(ctx, exc, "name");
            JSValue msgVal  = JS_GetPropertyStr(ctx, exc, "message");
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
                const char* str = JS_ToCString(ctx, exc);
                if (str) { errStr = str; JS_FreeCString(ctx, str); }
            }
            if (!errStr.empty()) {
                std::cerr << "Exception in " << filename << ": " << errStr << std::endl;
            }
            JS_FreeValue(ctx, exc);
        }
        IntegratedDebugger::popFrame();
        pContext->currentFileName = nullptr;
        pContext->currentLineNumber = 0;
        currentScript_.clear();
        return JS_EXCEPTION;
    }

    JSValue val;
    struct WrapperScope {
        JSContextWrapper* prev;
        WrapperScope(JSContextWrapper* w) : prev(t_currentWrapper) { t_currentWrapper = w; }
        ~WrapperScope() { t_currentWrapper = prev; }
    } _wscope(this);

    // Single path: compile → load → run (protoCore interpreter). No legacy JS_Eval.
    proto::ProtoContext frameCtx(&pSpace, pContext, nullptr, nullptr, nullptr, nullptr);
    frameCtx.currentFileName = pContext->currentFileName;
    frameCtx.currentLineNumber = pContext->currentLineNumber;

    bool hadError = false;
    const int compileFlags = JS_EVAL_TYPE_GLOBAL;
    void* bytecode = protojs::compileToBytecodeWithFlags(ctx, code.c_str(), code.size(), filename.c_str(), compileFlags, nullptr);
    if (!bytecode) {
        val = JS_GetException(ctx);
        const char* noFallback = std::getenv("PROTOJS_NO_FALLBACK");
        if (noFallback && (noFallback[0] == '1' || noFallback[0] == 't' || noFallback[0] == 'T')) {
            std::cerr << "[protojs] compile failed (no fallback)" << std::endl;
            hadError = true;
        } else {
            std::cerr << "[protojs] compile failed, fallback to QuickJS eval" << std::endl;
            JS_FreeValue(ctx, val);
            JSValue runVal = JS_Eval(ctx, code.c_str(), code.size(), filename.c_str(), compileFlags);
            if (JS_IsException(runVal)) {
                val = JS_GetException(ctx);
            } else {
                val = runVal;
                bytecode = reinterpret_cast<void*>(1); /* success: do not report as compile failed */
            }
        }
    }
    if (bytecode && bytecode != reinterpret_cast<void*>(1)) {
        // Heap-allocate the bytecode module and keep it alive on the
        // wrapper so async callbacks (setImmediate, Deferred .then,
        // worker completion) can still resolve bcIds against it after
        // eval has returned.  The previous version made `module` a
        // stack local; once eval popped, callbacks dispatched through
        // the event loop had no way to look up bytecode functions.
        auto modulePtr = std::make_unique<protojs::ProtoBytecodeModule>();
        protojs::ProtoBytecodeModule* modPtr = modulePtr.get();
        if (!protojs::loadBytecode(ctx, bytecode, &frameCtx, modPtr)) {
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
                // Pin the root module's metadata in the root set so it's not collected.
                // This deep-roots the entire constant pool and nested functions via metadata links.
                proto::ProtoRootSet* rs = getRootSet();
                if (rs) {
                    if (rootModuleHandle_) rs->remove(rootModuleHandle_);
                    rootModuleHandle_ = rs->add(modPtr->metadata);
                }

                rootModule_ = modPtr;
                rootModuleStorage_ = std::move(modulePtr);
                const proto::ProtoObject* exception = PROTO_NONE;
                const proto::ProtoObject* result =
                    protojs::runBytecode(&frameCtx, modPtr,
                                         globalObj, nullptr,
                                         &nativeGlobalRoot_, &exception);
                frameCtx.returnValue = result;

                if (exception && exception != PROTO_NONE) {
                    // Format exception from ProtoObject for error reporting.
                    // Always prefer "Name: message" so Test262 can match negative.type.
                    std::string errStr;
                    const proto::ProtoString* nameKey =
                        JSSymbols::name(&frameCtx);
                    const proto::ProtoString* msgKey =
                        JSSymbols::message(&frameCtx);

                    // 1) Try exception.name (searching prototype chain).
                    if (nameKey) {
                        const proto::ProtoObject* nv =
                            exception->getAttribute(&frameCtx, nameKey, true);
                        if (nv && nv != PROTO_NONE && nv->isString(&frameCtx)) {
                            std::string tmp;
                            nv->asString(&frameCtx)->toUTF8String(&frameCtx, tmp);
                            if (!tmp.empty()) errStr = tmp; // e.g. "SyntaxError"
                        }
                    }

                    // 2) Append ": message" if message exists.
                    if (msgKey) {
                        const proto::ProtoObject* mv =
                            exception->getAttribute(&frameCtx, msgKey, true);
                        if (mv && mv != PROTO_NONE && mv->isString(&frameCtx)) {
                            std::string tmp;
                            mv->asString(&frameCtx)->toUTF8String(&frameCtx, tmp);
                            if (!tmp.empty()) {
                                if (!errStr.empty()) errStr += ": ";
                                errStr += tmp;
                            }
                        }
                    }

                    // 3) Fallbacks: plain thrown string or generic.
                    if (errStr.empty() && exception->isString(&frameCtx)) {
                        const proto::ProtoString* s = exception->asString(&frameCtx);
                        if (s) s->toUTF8String(&frameCtx, errStr);
                    }
                    if (errStr.empty()) {
                        errStr = "Error";
                    }

                    std::cerr << "Exception in " << filename << ": " << errStr << std::endl;
                    hadError = true;
                    val = JS_EXCEPTION;
                } else {
                    val = TypeBridge::toJS(ctx, result, &frameCtx);
                }
            }
        }
    }

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

const proto::ProtoObject* JSContextWrapper::evalIsolatedToProto(
    const std::string& code, const std::string& filename) {
    // Mirror of the script-mode path in eval() with two adjustments:
    //   (1) the new bytecode module is appended to subEvalModules_ instead
    //       of replacing rootModule_/rootModuleStorage_, so the caller's
    //       active script keeps its atom cache and nested-function table;
    //   (2) the interpreter's raw ProtoObject result is returned directly
    //       — no TypeBridge::toJS roundtrip — so callable closures keep
    //       their __bytecode_id__ identity.
    //
    // Used by the Function constructor; in the future, direct eval() will
    // route through here as well.

    // Publish this wrapper as the current one for the duration of the
    // compile + run; lambdas that depend on JSContextWrapper::current()
    // (notably TypeBridge plumbing and EventLoop callbacks) need it.
    struct WrapperScope {
        JSContextWrapper* prev;
        WrapperScope(JSContextWrapper* w) : prev(t_currentWrapper) { t_currentWrapper = w; }
        ~WrapperScope() { t_currentWrapper = prev; }
    } _wscope(this);

    proto::ProtoContext frameCtx(&pSpace, pContext, nullptr, nullptr, nullptr, nullptr);
    frameCtx.currentFileName = pContext->currentFileName;
    frameCtx.currentLineNumber = pContext->currentLineNumber;

    // Clear any stale pending exception so compile sees a clean slate.
    if (JS_HasException(ctx)) {
        JSValue stale = JS_GetException(ctx);
        JS_FreeValue(ctx, stale);
    }

    const int compileFlags = JS_EVAL_TYPE_GLOBAL;
    void* bytecode = protojs::compileToBytecodeWithFlags(
        ctx, code.c_str(), code.size(), filename.c_str(), compileFlags, nullptr);
    if (!bytecode) {
        // Compile failed; drain the exception so it doesn't bleed into
        // the caller's runtime state. The caller (Function ctor handler)
        // surfaces this as a generic failure path.
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
        return PROTO_NONE;
    }

    auto modulePtr = std::make_unique<protojs::ProtoBytecodeModule>();
    protojs::ProtoBytecodeModule* modPtr = modulePtr.get();
    if (!protojs::loadBytecode(ctx, bytecode, &frameCtx, modPtr)) {
        return PROTO_NONE;
    }

    const proto::ProtoObject* globalObj = getNativeGlobal();
    if (!globalObj) return PROTO_NONE;

    // Pin metadata in the root set so its constant pool / nested funcs
    // stay alive after this call. A separate handle (not rootModuleHandle_)
    // tracks the pin so the parent's pin remains valid.
    proto::ProtoRootSet* rs = getRootSet();
    proto::ProtoRootSet::Handle pinHandle = 0;
    if (rs) {
        pinHandle = rs->add(modPtr->metadata);
    }
    const proto::ProtoObject* exception = PROTO_NONE;
    const proto::ProtoObject* result =
        protojs::runBytecode(&frameCtx, modPtr,
                             globalObj, nullptr,
                             &nativeGlobalRoot_, &exception);

    // Keep the module alive for the wrapper's lifetime so any closure
    // the eval produced (e.g. a Function-ctor result) can resolve its
    // bcId at call time. The vector grows monotonically; expected
    // entries per session are modest (Function ctor calls are rare).
    subEvalModules_.push_back(std::move(modulePtr));
    if (pinHandle) subEvalHandles_.push_back(pinHandle);

    if (exception && exception != PROTO_NONE) {
        // Exception during run: don't propagate further here; the
        // caller can fall back to PROTO_NONE the way the legacy path
        // did. A richer propagation (signalling a protoJS-side throw)
        // is a follow-up.
        return PROTO_NONE;
    }

    // If the result is a callable closure created in this sub-module,
    // stamp __closure_module__ so the dispatch path (L_OP_call) can
    // resolve its __bytecode_id__ against the right nestedFunctions[]
    // array. Without this, the calling script's current/root module
    // would be consulted instead and the bcId would either miss or
    // (worse) collide with an unrelated nested function.
    if (result && result != PROTO_NONE) {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(&frameCtx);
        if (bcKey) {
            const proto::ProtoObject* bcVal =
                result->getAttribute(&frameCtx, bcKey, false);
            if (bcVal && bcVal != PROTO_NONE && bcVal->isInteger(&frameCtx)) {
                const proto::ProtoString* cmKey =
                    JSSymbols::closureModule(&frameCtx);
                if (cmKey) {
                    const long long modAsLong =
                        static_cast<long long>(reinterpret_cast<uintptr_t>(modPtr));
                    result = result->setAttribute(&frameCtx, cmKey,
                        frameCtx.fromInteger(modAsLong));
                }
            }
        }
    }

    return result;
}

JSValue JSContextWrapper::evalPreload(const std::string& code, const std::string& filename) {
    // Evaluate as script (not module) so top-level declarations become globals.
    JSValue result = JS_Eval(ctx, code.c_str(), code.size(), filename.c_str(),
                             JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx);
        const char* str = JS_ToCString(ctx, exc);
        if (str) {
            std::cerr << "[protojs] preload error in " << filename << ": " << str << std::endl;
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, exc);
        return JS_EXCEPTION;
    }
    return result;
}

} // namespace protojs
