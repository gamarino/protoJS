#include "EvalModuleCache.h"

#include "ProtoBytecodeModule.h"
#include "../JSSymbols.h"
#include "quickjs.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace protojs {

namespace {
enum TokenState : int {
    kLive = 0,       // the token may still be reachable
    kFinalized = 1,  // the collector finalized the token: nothing runs the module
    kOwnerGone = 2,  // the cache was destroyed first; the finalizer deletes the record
};
}  // namespace

struct EvalModuleCache::Record {
    std::unique_ptr<ProtoBytecodeModule> module;
    /** QuickJS bytecode to free with the module, or nullptr to keep it. */
    JSContext* qctx{nullptr};
    void* quickjsBytecode{nullptr};
    /** The cache key; empty once the entry is evicted. */
    std::string key;
    /** [token, caller token or PROTO_NONE]: what the cache root holds. */
    const proto::ProtoObject* entry{nullptr};
    std::atomic<int> state{kLive};
    // Guarded by the cache's mutex.
    int leases{0};
    std::uint64_t lastUse{0};
};

EvalModuleCache::Lease& EvalModuleCache::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        if (cache_ && record_) cache_->release(record_);
        cache_ = other.cache_;
        record_ = other.record_;
        other.cache_ = nullptr;
        other.record_ = nullptr;
    }
    return *this;
}

EvalModuleCache::Lease::~Lease() {
    if (cache_ && record_) cache_->release(record_);
}

const ProtoBytecodeModule* EvalModuleCache::Lease::module() const {
    return record_ ? record_->module.get() : nullptr;
}

EvalModuleCache::~EvalModuleCache() {
    // The wrapper is going away: its modules go with it, as they always did.
    // A record whose token the collector has not finalized yet stays for the
    // finalizer to delete (the space may outlive a wrapper that borrows it).
    for (Record* r : records_) {
        r->module.reset();
        if (r->state.exchange(kOwnerGone, std::memory_order_acq_rel) == kFinalized) delete r;
    }
    records_.clear();
    cache_.clear();
}

std::string EvalModuleCache::keyOf(void* callerBytecode, int scope, const std::string& source) {
    std::string key;
    key.reserve(sizeof(callerBytecode) + sizeof(scope) + source.size());
    key.append(reinterpret_cast<const char*>(&callerBytecode), sizeof(callerBytecode));
    key.append(reinterpret_cast<const char*>(&scope), sizeof(scope));
    key.append(source);
    return key;
}

EvalModuleCache::Lease EvalModuleCache::find(void* callerBytecode, int scope,
                                             const std::string& source) {
    const std::string key = keyOf(callerBytecode, scope, source);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(key);
    if (it == cache_.end()) return Lease();
    Record* r = it->second;
    r->lastUse = ++clock_;
    ++r->leases;
    return Lease(this, r);
}

void EvalModuleCache::release(Record* record) {
    std::lock_guard<std::mutex> lock(mutex_);
    --record->leases;
}

// The finalizer of a lifetime token's external pointer (GarbageCollector.md
// section 7): one atomic exchange, and a C++ delete when the cache is already
// gone. No protoCore call, no allocation, no lock, no wait.
void EvalModuleCache::finalizeToken(void* record) {
    Record* r = static_cast<Record*>(record);
    if (r->state.exchange(kFinalized, std::memory_order_acq_rel) == kOwnerGone) delete r;
}

void EvalModuleCache::sweep() {
    std::vector<Record*> dead;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto keep = std::partition(records_.begin(), records_.end(), [](Record* r) {
            return r->state.load(std::memory_order_acquire) != kFinalized;
        });
        dead.assign(keep, records_.end());
        records_.erase(keep, records_.end());
        sweepAt_ = std::max<std::size_t>(2 * kCapacity, 2 * records_.size());
    }
    for (Record* r : dead) {
        // A finalized token was reachable from no cache entry (entries are
        // rooted), no lease (a leased record stays cached) and no closure.
        if (r->qctx && r->quickjsBytecode)
            JS_FreeValue(r->qctx, JS_MKPTR(JS_TAG_FUNCTION_BYTECODE, r->quickjsBytecode));
        delete r;
    }
}

void EvalModuleCache::ensureRoot(proto::ProtoContext* ctx, proto::ProtoRootSet* rootSet) {
    if (root_.load(std::memory_order_acquire) || !rootSet) return;
    // Allocate and pin outside the mutex; a thread that loses the race drops
    // its spare handle.
    const proto::ProtoObject* holder = ctx->newObject(true);
    const proto::ProtoRootSet::Handle handle = rootSet->add(holder);
    const proto::ProtoObject* expected = nullptr;
    if (!root_.compare_exchange_strong(expected, holder, std::memory_order_acq_rel))
        rootSet->remove(handle);
    // The winner's handle lives as long as the root set: ~JSContextWrapper
    // destroys the set, which releases it.
}

void EvalModuleCache::publish(proto::ProtoContext* ctx, const proto::ProtoObject* entry) {
    const proto::ProtoObject* root = root_.load(std::memory_order_acquire);
    const proto::ProtoString* key = JSSymbols::evalCacheEntries(ctx);
    if (!root || !key) return;
    for (;;) {
        const proto::ProtoObject* old = root->getOwnAttributeDirect(ctx, key);
        const proto::ProtoList* list = (old && old != PROTO_NONE) ? old->asList(ctx) : nullptr;
        const proto::ProtoList* next = nullptr;
        if (list && list->getSize(ctx) >= 2 * kCapacity) {
            // Compact: exactly the entries the map holds now. `entry` is among
            // them unless it was already evicted, and every one is reachable
            // (from `old`, or from the context of the thread that inserted it,
            // which publishes before that context ends).
            std::vector<const proto::ProtoObject*> items;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                items.reserve(cache_.size());
                for (const auto& kv : cache_) items.push_back(kv.second->entry);
            }
            next = ctx->newList(static_cast<unsigned>(items.size()), items.data());
        } else {
            next = (list ? list : ctx->newList())->appendLast(ctx, entry);
        }
        if (root->setAttributeIfEqual(ctx, key, old, next ? next->asObject(ctx) : PROTO_NONE)) return;
    }
}

EvalModuleCache::Lease EvalModuleCache::insert(proto::ProtoContext* ctx, proto::ProtoRootSet* rootSet,
                                               std::unique_ptr<ProtoBytecodeModule> module,
                                               JSContext* qctx, JSContext* ownQuickJS,
                                               void* quickjsBytecode,
                                               const ProtoBytecodeModule* caller,
                                               void* callerBytecode, int scope,
                                               const std::string& source) {
    bool sweepNow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sweepNow = records_.size() >= sweepAt_;
    }
    if (sweepNow) sweep();
    ensureRoot(ctx, rootSet);

    Record* r = new Record();
    r->module = std::move(module);
    if (qctx && qctx == ownQuickJS) {
        r->qctx = qctx;
        r->quickjsBytecode = quickjsBytecode;
    }
    r->key = keyOf(callerBytecode, scope, source);

    // The lifetime token, stamped on every closure the eval code creates
    // (OP_fclosure), and the cache entry. Allocated in `ctx`, which keeps
    // them alive until the publication below makes the root hold the entry:
    // no safepoint (where protoCore may hand a context's young cells to the
    // collector early) runs in between.
    const proto::ProtoObject* tokenItems[2] = {
        ctx->fromExternalPointer(r, &EvalModuleCache::finalizeToken),
        r->module->metadata ? r->module->metadata : PROTO_NONE,
    };
    const proto::ProtoObject* token = ctx->newList(2, tokenItems)->asObject(ctx);
    r->module->lifetimeToken = token;
    const proto::ProtoObject* callerToken =
        caller ? caller->functionTable()->lifetimeToken : nullptr;
    const proto::ProtoObject* entryItems[2] = {token, callerToken ? callerToken : PROTO_NONE};
    r->entry = ctx->newList(2, entryItems)->asObject(ctx);

    Record* leased = r;
    bool added = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Every record is swept once its token is finalized, cached or not.
        records_.push_back(r);
        auto it = cache_.find(r->key);
        if (it != cache_.end()) {
            // Another thread compiled the same key meanwhile: run its module,
            // which the root holds. Ours is unreachable once `ctx` ends.
            r->key.clear();
            leased = it->second;
        } else {
            cache_.emplace(r->key, r);
            while (cache_.size() > kCapacity) {
                Record* victim = nullptr;
                for (const auto& kv : cache_) {
                    Record* c = kv.second;
                    if (c == r || c->leases > 0) continue;
                    if (!victim || c->lastUse < victim->lastUse) victim = c;
                }
                if (!victim) break;  // every other entry is running: shrink later
                cache_.erase(victim->key);
                victim->key.clear();
            }
            added = true;
        }
        leased->lastUse = ++clock_;
        ++leased->leases;
    }
    if (added) publish(ctx, r->entry);
    return Lease(this, leased);
}

const ProtoBytecodeModule* EvalModuleCache::moduleOfToken(proto::ProtoContext* ctx,
                                                          const proto::ProtoObject* stamp) {
    if (!stamp || stamp == PROTO_NONE) return nullptr;
    const proto::ProtoList* list = stamp->asList(ctx);
    if (!list || list->getSize(ctx) != 2) return nullptr;
    const proto::ProtoObject* ptr = list->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ep = ptr ? ptr->asExternalPointer(ctx) : nullptr;
    if (!ep) return nullptr;
    const Record* r = static_cast<const Record*>(ep->getPointer(ctx));
    return r ? r->module.get() : nullptr;
}

}  // namespace protojs
