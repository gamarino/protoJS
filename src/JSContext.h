#ifndef PROTOJS_JSCONTEXT_H
#define PROTOJS_JSCONTEXT_H

#include "quickjs.h"
#include "protoCore.h"
#include "JSPrototypes.h"
#include <memory>
#include <string>
#include <map>
#include <atomic>
#include <mutex>
#include <vector>

namespace protojs {

struct ProtoBytecodeModule;  // forward — owned via unique_ptr below
class MicrotaskQueue;        // forward — owned via unique_ptr below
class DeferredPool;          // forward — owned via unique_ptr below


class JSContextWrapper {
public:
    /**
     * @brief Constructs a JSContextWrapper with optional thread pool configuration.
     * @param cpuThreads Number of CPU threads (0 = auto-detect)
     * @param ioThreads Number of I/O threads (0 = auto-calculate)
     * @param ioFactor Factor for I/O threads (used if ioThreads == 0, default: 3.0)
     */
    JSContextWrapper(size_t cpuThreads = 0, size_t ioThreads = 0, double ioFactor = 3.0);

    /**
     * @brief A wrapper that runs in @p owner's ProtoSpace instead of creating one.
     *
     * One ProtoSpace per process (protoCore's recommended model): a
     * `worker_threads` worker runs on a protoCore thread of the main space,
     * shares its heap, and passes values without serialization. What JavaScript
     * requires to be per agent stays per wrapper: the global object (a child of
     * the owner's global object, so the realm's intrinsics are shared and the
     * worker's own top-level bindings and globals are not), the module caches,
     * the job queue, the root set, the bytecode modules and the QuickJS context.
     *
     * @param owner the wrapper that owns the space; it must outlive this one
     *        (~JSContextWrapper releases its workers first).
     * @param threadContext the root context of the calling protoCore thread,
     *        which must be a thread of the owner's space; used by this wrapper
     *        on that thread. The constructing thread must already have adopted
     *        the owner thread's identity objects (runtime/ThreadIdentity.h).
     * @param ownerGlobal the owner's global object, read on the owner's thread
     *        and kept alive by the caller until this wrapper has made its own
     *        global; nullptr reads it from the owner.
     */
    JSContextWrapper(JSContextWrapper& owner, proto::ProtoContext* threadContext,
                     const proto::ProtoObject* ownerGlobal = nullptr);
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
     * Compile + load + run `code` as a script-mode expression, returning
     * the resulting ProtoObject directly without converting to JSValue.
     *
     * Used by the Function constructor (Function(...) / new Function(...))
     * to materialise a new closure from a source-text body without
     * disturbing the caller's rootModule_/rootModuleStorage_. The
     * resulting bytecode module is appended to subEvalModules_ and lives
     * for the lifetime of the wrapper, so any closures the eval produces
     * remain invokable after the call returns.
     *
     * Differs from eval() in two ways:
     *   - rootModule_ / rootModuleStorage_ / rootModuleHandle_ are NOT
     *     overwritten; the parent script's metadata survives.
     *   - The return value is the protoCore object the interpreter
     *     produced, with no toJS/fromJS roundtrip — preserves
     *     __bytecode_id__ and any other identity-bearing markers.
     *
     * Returns PROTO_NONE on any error (with the QuickJS-side exception
     * already printed to stderr).
     */
    const proto::ProtoObject* evalIsolatedToProto(const std::string& code,
                                                  const std::string& filename);

    /**
     * @brief The loaded module of an eval's source (ECMA-262 PerformEval).
     *
     * With `callerBytecode` (a direct eval) the source is compiled against
     * the calling function's QuickJS bytecode at the eval's scope, so its
     * closure variables name the caller's variables; without it (an
     * indirect eval) it is compiled as global code.  `quickjsCtx` is the
     * QuickJS context that compiled the caller.  The module lives for the
     * wrapper's lifetime, and one compilation per distinct (caller, scope,
     * source) is cached, so an eval in a loop compiles once.  Returns
     * nullptr with the SyntaxError's message in `error`.
     */
    const void* loadEvalModule(JSContext* quickjsCtx, void* callerBytecode, int scopeOperand,
                               const std::string& source, std::string& error);

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
     *
     * On a Deferred pool thread (see ThreadView) this is that thread's root
     * context, not the owner thread's: a context belongs to one thread, and a
     * native that allocates through the wrapper's context must allocate on
     * the thread it runs on.
     */
    proto::ProtoContext* getProtoContext() {
        if (const ThreadView* v = t_threadView_) return v->context;
        // A wrapper that borrows its space is also used from the owner's
        // thread (a worker's message listeners run on the main event loop):
        // each thread allocates through its own context.
        if (owner_) {
            if (proto::ProtoContext* c = threadContextForBorrowedWrapper()) return c;
        }
        return pContext;
    }

    /**
     * @brief For a wrapper that borrows its space: the thread it was built on
     * is about to end, and with it the context it was given.  From here on
     * the wrapper uses the calling thread's own context (getProtoContext).
     * Called on that thread, as its last use of the wrapper.
     */
    void releaseThreadContext();

    /** @brief The wrapper whose space this one borrows, or nullptr if it owns its space. */
    JSContextWrapper* spaceOwner() const { return owner_; }

    /**
     * @brief The root set that keeps space-wide objects alive for the life of
     * the space: the owner's when this wrapper borrows its space, this one's
     * otherwise. Built-ins cached per space (runtime/PinnedBuiltin.h) are
     * pinned here, never in a borrowing wrapper's own set, which dies with it.
     */
    proto::ProtoRootSet* spaceRootSet() { return owner_ ? owner_->spaceRootSet() : getRootSet(); }

    /**
     * @brief The per-thread view a Deferred pool thread has of its owner.
     *
     * A Deferred's function runs on a pool thread of the owner's space with
     * the owner wrapper current (see src/DeferredPool.h). Three things of the
     * wrapper are, however, per thread, and the view supplies them:
     *
     *  - `globalSlot`: the root slot the interpreter reads the global through.
     *    The owner's slot (nativeGlobalRoot_) is written by the owner thread's
     *    interpreter; a pool thread works on its own copy of the pointer. The
     *    global object itself is mutable and shared, so global writes still
     *    land on the one global.
     *  - `quickjs`: a QuickJS context of this thread. The owner's QuickJS
     *    runtime is single-threaded; JSON.parse and the regular-expression
     *    engine (libregexp's allocator and stack check) use this one.
     *  - `context`: this thread's protoCore root context.
     *
     * Installed for the life of a pool thread by ThreadViewScope.
     */
    struct ThreadView {
        const proto::ProtoObject** globalSlot = nullptr;
        JSContext* quickjs = nullptr;
        proto::ProtoContext* context = nullptr;
    };

    class ThreadViewScope {
    public:
        explicit ThreadViewScope(const ThreadView* v) : prev_(t_threadView_) { t_threadView_ = v; }
        ~ThreadViewScope() { t_threadView_ = prev_; }
        ThreadViewScope(const ThreadViewScope&) = delete;
        ThreadViewScope& operator=(const ThreadViewScope&) = delete;
    private:
        const ThreadView* prev_;
    };

    /** @brief The calling thread's view, or nullptr on a thread that owns its wrapper. */
    static const ThreadView* threadView() { return t_threadView_; }

    /** @brief True on a Deferred pool thread. */
    static bool onPoolThread() { return t_threadView_ != nullptr; }

    /**
     * @brief The QuickJS context the CALLING thread may use: the pool thread's
     * own on a Deferred pool thread, the current wrapper's otherwise, or nullptr
     * when no wrapper is current.
     */
    static JSContext* quickJSForThisThread();

    /**
     * @brief The Deferred pool of this wrapper's space, started on first use.
     * Owner thread only.
     */
    DeferredPool& deferredPool();

    /**
     * @brief Switch this wrapper's space to protoCore's adaptive heap
     * controller because Deferreds have started, unless the policy says
     * otherwise (src/HeapLimit.h, adaptiveHeapWanted; "Memory policy" in
     * src/JSContext.cpp).  Called by the DeferredPool when it starts its
     * threads; idempotent.  Owner thread only.
     */
    void adoptAdaptiveHeapForDeferreds();

    /**
     * @brief Returns the protoCore space.
     */
    proto::ProtoSpace* getProtoSpace() { return pSpace_; }

    /**
     * @brief Returns the QuickJS runtime.
     */
    JSRuntime* getJSRuntime() { return rt; }

    /**
     * @brief Returns the JS Object prototype (base for plain objects).
     */
    const proto::ProtoObject* getJSObjectPrototype() const { return jsPrototypes_.object; }

    /**
     * @brief Override the JS Object prototype after lazy initialization.
     *
     * BootstrapJSPrototypes seeds Object.prototype as an empty
     * placeholder so TypeBridge has a parent to attach. Later
     * ensureObjectConstructor installs the populated prototype with
     * the .constructor backref and rebinds space->objectPrototype.
     * Without this setter, TypeBridge::fromJS keeps stamping JSON-
     * parsed objects with the stale empty snapshot — their proto !==
     * the user-visible Object.prototype.
     */
    void setJSObjectPrototype(const proto::ProtoObject* p) { jsPrototypes_.object = p; }

    /**
     * @brief Returns the JS Array prototype.
     */
    const proto::ProtoObject* getJSArrayPrototype() const;

    /**
     * @brief Override the JS Array prototype after lazy initialization.
     *
     * BootstrapJSPrototypes creates an empty `Array.prototype` so TypeBridge
     * can attach a parent to JS arrays before the populated prototype is
     * built. ensureArrayPrototype later constructs the real prototype (with
     * join, push, slice, …) and calls this setter so future fromJS lookups
     * and JSON.parse() return arrays that actually inherit the methods.
     */
    void setJSArrayPrototype(const proto::ProtoObject* p) { jsPrototypes_.array = p; }

    /**
     * @brief Returns the JS Arguments prototype.
     */
    const proto::ProtoObject* getJSArgumentsPrototype() const { return jsPrototypes_.arguments; }

    /**
     * @brief Returns the JS RegExp prototype.
     */
    const proto::ProtoObject* getJSRegExpPrototype() const { return jsPrototypes_.regexp; }

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
     * @brief Returns the ECMAScript global object: what `globalThis`, the
     * top-level `this` and the `this` of a sloppy function called without a
     * receiver all denote.
     *
     * Not always getNativeGlobal(): once the root script runs, the root slot
     * holds the script's binding scope, a child of the global object
     * (runBytecode's module-scope split), and this answers its parent.
     * `ctx` is the calling thread's context (a Deferred pool thread reads
     * through its own).
     */
    const proto::ProtoObject* getGlobalObject(proto::ProtoContext* ctx);

    /**
     * @brief Update the native global (called after module init mutates it).
     */
    void updateNativeGlobal(const proto::ProtoObject* g) {
        nativeGlobalRoot_ = g;
        rootNativeGlobal();
    }

    /**
     * @brief Keep the object nativeGlobalRoot_ currently points at reachable
     * by the collector, for the lifetime of the wrapper.
     *
     * nativeGlobalRoot_ is a C++ member, which the GC does not scan. What it
     * points at is not always reachable by any other path: after the
     * interpreter's module-scope split (runBytecode) it is the module scope, a
     * mutable child of the global that holds every top-level binding and that
     * nothing else references -- a parent does not reference its children.
     * Unpinned, its handle was swept as soon as the eval frame's young
     * generation was handed to the collector, its entry in the mutables table
     * was released, and the top-level bindings read back as undefined.
     *
     * Pins the current object in the wrapper's root set and releases the pin
     * on the previous one; a no-op when the pointer has not changed. Called
     * wherever the pointer is re-bound: getNativeGlobal, updateNativeGlobal,
     * the module-scope split, and after each top-level run as a backstop.
     */
    void rootNativeGlobal();

    /**
     * @brief Pointer to a pointer of the native global, suitable for passing
     * as `pGlobalRoot` to runBytecode / callBytecodeFromEventLoop.  The
     * inner pointer can be re-bound by the interpreter when top-level
     * `var` writes to the global.
     */
    const proto::ProtoObject** getNativeGlobalRootPtr() {
        if (const ThreadView* v = t_threadView_) return v->globalSlot;
        return &nativeGlobalRoot_;
    }

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
     * @brief This wrapper's ECMAScript job queue (promise reactions, await
     * continuations, queueMicrotask callbacks).  See MicrotaskQueue.h.
     */
    MicrotaskQueue& microtasks() { return *microtasks_; }

    /**
     * @brief Get the per-context CommonJS module cache.
     */
    std::map<std::string, JSValue>& getCJSCache() { return cjsCache_; }
    std::mutex& getCJSCacheMutex() { return cjsCacheMutex_; }

private:
    /** The calling thread's view (Deferred pool threads); see ThreadView. */
    static inline thread_local const ThreadView* t_threadView_ = nullptr;

    /** The Deferred pool, started on the first `new Deferred`; joined first
     *  thing in the destructor. */
    std::unique_ptr<DeferredPool> deferredPool_;
    /** adoptAdaptiveHeapForDeferreds has run (owner thread only). */
    bool adaptiveHeapConsidered_{false};

    /** Phase 2: Per-context CommonJS module cache. Ties JSValue lifetime to the wrapper's runtime. */
    std::map<std::string, JSValue> cjsCache_;
    std::mutex cjsCacheMutex_;

    /** Phase 6: ProtoCore-native global root; built lazily, updated when interpreter mutates global. */
    mutable const proto::ProtoObject* nativeGlobalRoot_{nullptr};
    /** The object rootNativeGlobal last pinned, and its handle in rootSet_. */
    const proto::ProtoObject* nativeGlobalPinned_{nullptr};
    proto::ProtoRootSet::Handle nativeGlobalHandle_{0};
    /** Most-recent top-level bytecode module — used by async callbacks
     * (setImmediate, Deferred, worker-thread completion) to look up
     * function bcIds after eval has returned.  Type-erased to keep the
     * runtime/ headers out of the public JSContext API. */
    const void* rootModule_{nullptr};
    /** Owning storage for the same module so it survives past eval(). */
    std::unique_ptr<protojs::ProtoBytecodeModule> rootModuleStorage_;
    /** Handle for pinning the root module's metadata in rootSet_. */
    proto::ProtoRootSet::Handle rootModuleHandle_{0};
    /** Owning storage for bytecode modules produced by evalIsolatedToProto
     *  (Function constructor, future direct-eval). Each entry survives
     *  for the wrapper lifetime so closures it produced stay invokable. */
    std::vector<std::unique_ptr<protojs::ProtoBytecodeModule>> subEvalModules_;
    /** Pinning handles for the sub-eval modules' metadata, parallel to
     *  subEvalModules_. Released alongside the modules in ~JSContextWrapper. */
    std::vector<proto::ProtoRootSet::Handle> subEvalHandles_;
    /** loadEvalModule's cache: (caller bytecode, scope operand, source) to
     *  the module, owned by subEvalModules_. */
    std::map<std::pair<std::pair<void*, int>, std::string>, const void*> evalModuleCache_;
    /** ProtoCore-side root set for pinning async-callback receivers
     *  across event-loop hops; lazy-init, destroyed on wrapper destruction. */
    proto::ProtoRootSet* rootSet_{nullptr};
    /** The job queue (microtasks) of this agent; created in the constructor,
     *  destroyed before the root set its holder is pinned in. */
    std::unique_ptr<MicrotaskQueue> microtasks_;
    JSRuntime* rt;
    JSContext* ctx;
    /** The space this wrapper created (null when it borrows its owner's). */
    std::unique_ptr<proto::ProtoSpace> ownedSpace_;
    /** The space this wrapper runs in: ownedSpace_, or the owner's. */
    proto::ProtoSpace* pSpace_;
    /** The wrapper whose space this one borrows (null when it owns its space). */
    JSContextWrapper* owner_{nullptr};
    /** The space's root context, or the borrowing thread's root context. */
    proto::ProtoContext* pContext;
    /** The owner's global object, the parent of this wrapper's (borrowing only). */
    const proto::ProtoObject* ownerGlobal_{nullptr};
    /** The calling thread's own context, for a wrapper that borrows its space. */
    static proto::ProtoContext* threadContextForBorrowedWrapper();
    JSPrototypes jsPrototypes_;
    /** Current script path for ProtoContext::currentFileName (keeps pointer valid during eval). */
    std::string currentScript_;
    /** Always true: eval uses protoCore path (compile-only + loader + interpreter). */
    bool useProtoEval_{true};

    /** How many JSContextWrapper instances are alive, and therefore how many
     *  hold a claim on the PROCESS-WIDE CPU and I/O thread pools.
     *
     *  `CPUThreadPool` and `IOThreadPool` are singletons: `initialize()` shuts the
     *  existing pool down and replaces it, and `shutdown()` destroys it for
     *  everybody.  They are not per-wrapper state, so a wrapper must not treat
     *  them as its own -- a `worker_threads` worker builds its own wrapper, and
     *  before this counter existed that worker's constructor replaced the main
     *  thread's pool and its destructor took the replacement away again, leaving
     *  the main thread's pool silently gone.  Ownership is collective instead:
     *  the FIRST live wrapper sizes the pools and the LAST one to be destroyed
     *  shuts them down.
     *
     *  Atomic rather than mutex-guarded on purpose.  A wrapper is destroyed from
     *  `freeWorkerState`, i.e. from a GC callback, while the main thread may be
     *  inside its own destructor blocking on `ThreadPoolExecutor::shutdown`'s
     *  joins; a lock held across that join would be a lock the collector waits
     *  for while a pool worker waits for the collector.  A counter needs no lock.
     */
    static std::atomic<size_t> poolOwners_;

public:
    /** Wrappers alive in the process (diagnostics and tests). */
    static size_t liveWrapperCount() { return poolOwners_.load(); }
};

} // namespace protojs

#endif // PROTOJS_JSCONTEXT_H
