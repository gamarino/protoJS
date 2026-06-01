#ifndef PROTOJS_JSCONTEXT_H
#define PROTOJS_JSCONTEXT_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include "JSPrototypes.h"
#include <memory>
#include <string>
#include <map>
#include <mutex>

namespace protojs {

struct ProtoBytecodeModule;  // forward — owned via unique_ptr below


class JSContextWrapper {
public:
    /**
     * @brief Constructs a JSContextWrapper with optional thread pool configuration.
     * @param cpuThreads Number of CPU threads (0 = auto-detect)
     * @param ioThreads Number of I/O threads (0 = auto-calculate)
     * @param ioFactor Factor for I/O threads (used if ioThreads == 0, default: 3.0)
     */
    JSContextWrapper(size_t cpuThreads = 0, size_t ioThreads = 0, double ioFactor = 3.0);
    ~JSContextWrapper();

    /**
     * @brief Evaluates JavaScript code.
     * Uses compile-only + ProtoBytecodeLoader + ProtoInterpreter (single path; no QuickJS runtime execution).
     * @param isModule When true, compiles with JS_EVAL_TYPE_MODULE (ES module semantics).
     */
    JSValue eval(const std::string& code, const std::string& filename = "eval", bool isModule = false);

    /**
     * Evaluate a script file in script mode (not module mode).
     * Used to inject harness globals (assert, Test262Error, etc.) before
     * running a module test. Returns JS_UNDEFINED on success, JS_EXCEPTION
     * on failure (and prints the error to stderr).
     */
    JSValue evalPreload(const std::string& code, const std::string& filename);

    /**
     * @brief Use protoCore interpreter path for eval (compile -> load -> run).
     * Always true; single execution path.
     */
    void setUseProtoEval(bool use) { useProtoEval_ = use; }
    bool useProtoEval() const { return useProtoEval_; }

    /**
     * @brief Returns the QuickJS context.
     */
    JSContext* getJSContext() { return ctx; }

    /**
     * @brief Returns the protoCore context.
     */
    proto::ProtoContext* getProtoContext() { return pContext; }

    /**
     * @brief Returns the protoCore space.
     */
    proto::ProtoSpace* getProtoSpace() { return &pSpace; }

    /**
     * @brief Returns the QuickJS runtime.
     */
    JSRuntime* getJSRuntime() { return rt; }

    /**
     * @brief Returns the JS Object prototype (base for plain objects).
     */
    const proto::ProtoObject* getJSObjectPrototype() const { return jsPrototypes_.object; }

    /**
     * @brief Returns the JS Array prototype.
     */
    const proto::ProtoObject* getJSArrayPrototype() const { return jsPrototypes_.array; }

    /**
     * @brief Returns the JS Arguments prototype.
     */
    const proto::ProtoObject* getJSArgumentsPrototype() const { return jsPrototypes_.arguments; }

    /**
     * @brief Returns the JS RegExp prototype.
     */
    const proto::ProtoObject* getJSRegExpPrototype() const { return jsPrototypes_.regexp; }
    const proto::ProtoObject* getFrozenMarker() const { return jsPrototypes_.frozenMarker; }
    const proto::ProtoObject* getNonExtensibleMarker() const { return jsPrototypes_.nonExtensibleMarker; }
    const proto::ProtoObject* getSealedMarker() const { return jsPrototypes_.sealedMarker; }

    /**
     * @brief Get the currently executing wrapper on this thread.
     */
    static JSContextWrapper* current();

    /**
     * @brief RAII guard that publishes a wrapper as `current()` for the
     * lifetime of the guard.  Used by the event loop / async callback paths
     * to temporarily restore the wrapper context — `current()` is otherwise
     * only set during eval().
     */
    class CurrentScope {
    public:
        explicit CurrentScope(JSContextWrapper* w);
        ~CurrentScope();
        CurrentScope(const CurrentScope&) = delete;
        CurrentScope& operator=(const CurrentScope&) = delete;
    private:
        JSContextWrapper* prev_;
    };

    /**
     * @brief Returns the ProtoCore-native global object.
     * Built on first use as a blank object; converted modules register their
     * exports on it via their new init() signatures.
     */
    const proto::ProtoObject* getNativeGlobal();

    /**
     * @brief Update the native global (called after module init mutates it).
     */
    void updateNativeGlobal(const proto::ProtoObject* g) { nativeGlobalRoot_ = g; }

    /**
     * @brief Pointer to a pointer of the native global, suitable for passing
     * as `pGlobalRoot` to runBytecode / callBytecodeFromEventLoop.  The
     * inner pointer can be re-bound by the interpreter when top-level
     * `var` writes to the global.
     */
    const proto::ProtoObject** getNativeGlobalRootPtr() { return &nativeGlobalRoot_; }

    /**
     * @brief The bytecode module used by the most recent top-level eval.
     * Async callbacks scheduled during eval (setImmediate, Deferred .then,
     * worker-thread completion) need to look up bytecode functions
     * relative to this module after eval has returned.  Set by eval()
     * before running, never cleared (each new eval replaces it).
     */
    const void* getRootModule() const { return rootModule_; }
    void setRootModule(const void* m) { rootModule_ = m; }

    /**
     * @brief Returns the protoCore root set used by protojs to pin
     *        async-callback receivers (Deferred .then handlers,
     *        setImmediate callbacks, runInThread deferreds, etc.).
     *        The set is created lazily on first call and lives for the
     *        lifetime of the wrapper.  Owned by the protoCore space —
     *        the wrapper destructor calls destroyRootSet on shutdown.
     */
    proto::ProtoRootSet* getRootSet();

    /**
     * @brief Get the per-context CommonJS module cache.
     */
    std::map<std::string, JSValue>& getCJSCache() { return cjsCache_; }
    std::mutex& getCJSCacheMutex() { return cjsCacheMutex_; }

private:
    /** Phase 2: Per-context CommonJS module cache. Ties JSValue lifetime to the wrapper's runtime. */
    std::map<std::string, JSValue> cjsCache_;
    std::mutex cjsCacheMutex_;

    /** Phase 6: ProtoCore-native global root; built lazily, updated when interpreter mutates global. */
    mutable const proto::ProtoObject* nativeGlobalRoot_{nullptr};
    /** Most-recent top-level bytecode module — used by async callbacks
     * (setImmediate, Deferred, worker-thread completion) to look up
     * function bcIds after eval has returned.  Type-erased to keep the
     * runtime/ headers out of the public JSContext API. */
    const void* rootModule_{nullptr};
    /** Owning storage for the same module so it survives past eval(). */
    std::unique_ptr<protojs::ProtoBytecodeModule> rootModuleStorage_;
    /** Handle for pinning the root module's metadata in rootSet_. */
    proto::ProtoRootSet::Handle rootModuleHandle_{0};
    /** ProtoCore-side root set for pinning async-callback receivers
     *  across event-loop hops; lazy-init, destroyed on wrapper destruction. */
    proto::ProtoRootSet* rootSet_{nullptr};
    JSRuntime* rt;
    JSContext* ctx;
    proto::ProtoSpace pSpace;
    proto::ProtoContext* pContext; // rootContext owned by ProtoSpace
    JSPrototypes jsPrototypes_;
    /** Current script path for ProtoContext::currentFileName (keeps pointer valid during eval). */
    std::string currentScript_;
    /** Always true: eval uses protoCore path (compile-only + loader + interpreter). */
    bool useProtoEval_{true};
};

} // namespace protojs

#endif // PROTOJS_JSCONTEXT_H
