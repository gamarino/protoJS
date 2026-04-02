#ifndef PROTOJS_JSCONTEXT_H
#define PROTOJS_JSCONTEXT_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include "JSPrototypes.h"
#include <string>
#include <map>
#include <mutex>

namespace protojs {

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

    /**
     * @brief Get the currently executing wrapper on this thread.
     */
    static JSContextWrapper* current();

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
