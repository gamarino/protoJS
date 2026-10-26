#ifndef PROTOJS_EVAL_MODULE_CACHE_H
#define PROTOJS_EVAL_MODULE_CACHE_H

// EvalModuleCache -- the compiled modules of eval() (ECMA-262 PerformEval),
// with a bounded cache and lifetimes the collector decides.
//
// WHAT IT REPLACES. JSContextWrapper::loadEvalModule used to keep every module
// an eval compiled for the wrapper's lifetime, pinned with one ProtoRootSet
// handle each, and cached every (caller, scope, source) forever. A program
// that evaluates many distinct strings grew without bound and, under a heap
// ceiling, ran out of memory (tests/cli/eval_bounded_memory.py).
//
// LIFETIME. A module must outlive everything that can still run its code:
//   - the eval in progress (and any eval nested in it): a Lease, held by
//     performEval for the whole activation;
//   - every closure the eval code created (functions, arrows, methods,
//     accessors, classes, generator and async functions): OP_fclosure stamps
//     `__closure_module__` with the module's LIFETIME TOKEN instead of the
//     module's address;
//   - every suspended activation of its code: a generator object keeps the
//     token as `__gen_mod_token__` and an async continuation as
//     kContModuleToken, next to the raw module pointer they resume with (a
//     generator object does not keep its function).
// The token is a ProtoList [external pointer to the Record, the module's
// metadata]: the metadata roots the bytecode, the constant pool and the nested
// functions, as the per-module root-set handle did before, and the external
// pointer's finalizer tells this cache that the token is gone.
// When no closure, cache entry or lease reaches the token, the collector
// finalizes it. The finalizer only flips an atomic state in the Record
// (protoCore/docs/GarbageCollector.md section 7: no allocation, no protoCore
// call, no blocking); the C++ module and its QuickJS bytecode are released
// later, on a mutator thread, by sweep(), which a cache miss runs once the
// number of records has doubled since the last sweep.
//
// CACHE. At most kCapacity entries, keyed by (caller bytecode, scope operand,
// source text), least recently used first out; an entry in use by a lease is
// never evicted. An entry holds the module's token and the caller's token
// (when the caller is itself eval code), so a cached key's caller bytecode
// cannot be freed and its address reused while the entry exists.
//
// ROOTING. One pinned mutable object per wrapper (single-root pinning) holds
// the cache's entries as one ProtoList, replaced with setAttributeIfEqual. The
// C++ map is the authority, under a mutex that is never held across a
// protoCore allocation or call. A new entry is appended to the list; an
// evicted one stays in it until the list reaches 2 * kCapacity entries and is
// rebuilt from the map, so the root holds at most that many.

#include "protoCore.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct JSContext;

namespace protojs {

struct ProtoBytecodeModule;

class EvalModuleCache {
public:
    /** Maximum number of cached compilations. */
    static constexpr std::size_t kCapacity = 64;

    struct Record;

    /** Keeps one eval module alive and in the cache while the eval runs. */
    class Lease {
    public:
        Lease() = default;
        Lease(EvalModuleCache* cache, Record* record) : cache_(cache), record_(record) {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : cache_(other.cache_), record_(other.record_) {
            other.cache_ = nullptr;
            other.record_ = nullptr;
        }
        Lease& operator=(Lease&& other) noexcept;
        ~Lease();
        /** The leased module, or nullptr for an empty lease. */
        const ProtoBytecodeModule* module() const;
        explicit operator bool() const { return record_ != nullptr; }

    private:
        EvalModuleCache* cache_{nullptr};
        Record* record_{nullptr};
    };

    EvalModuleCache() = default;
    EvalModuleCache(const EvalModuleCache&) = delete;
    EvalModuleCache& operator=(const EvalModuleCache&) = delete;
    /** Frees every module (as the wrapper's teardown always did) and hands the
     *  records that are still reachable to their finalizers. Calls neither
     *  protoCore nor QuickJS. */
    ~EvalModuleCache();

    /** The cached module of (callerBytecode, scope, source), leased; an empty
     *  lease on a miss. Allocates nothing in protoCore. */
    Lease find(void* callerBytecode, int scope, const std::string& source);

    /**
     * Adopt a module just compiled and loaded for (callerBytecode, scope,
     * source), give it its lifetime token, cache it and lease it.
     * `quickjsBytecode` is freed through `qctx` when the module is released,
     * and only when `qctx` is `ownQuickJS` (a context that outlives every
     * record); otherwise it is kept, as QuickJS keeps every compiled function.
     * `caller` is the calling module (nullptr for an indirect eval).
     * `ctx` must stay alive until this returns; the token is published before.
     */
    Lease insert(proto::ProtoContext* ctx, proto::ProtoRootSet* rootSet,
                 std::unique_ptr<ProtoBytecodeModule> module,
                 JSContext* qctx, JSContext* ownQuickJS, void* quickjsBytecode,
                 const ProtoBytecodeModule* caller,
                 void* callerBytecode, int scope, const std::string& source);

    /** The module a `__closure_module__` stamp names when the stamp is a
     *  lifetime token; nullptr for anything else. */
    static const ProtoBytecodeModule* moduleOfToken(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* stamp);

private:
    static std::string keyOf(void* callerBytecode, int scope, const std::string& source);
    static void finalizeToken(void* record);
    void release(Record* record);
    /** Frees the records whose tokens the collector finalized. */
    void sweep();
    /** Makes the root hold `entry`: appended to the root's list, or the list
     *  rebuilt from the map once it has 2 * kCapacity entries. */
    void publish(proto::ProtoContext* ctx, const proto::ProtoObject* entry);
    void ensureRoot(proto::ProtoContext* ctx, proto::ProtoRootSet* rootSet);

    std::mutex mutex_;
    std::unordered_map<std::string, Record*> cache_;
    std::vector<Record*> records_;
    std::size_t sweepAt_{2 * kCapacity};
    std::uint64_t clock_{0};
    /** The pinned root (mutable object); written once. */
    std::atomic<const proto::ProtoObject*> root_{nullptr};
};

}  // namespace protojs

#endif
