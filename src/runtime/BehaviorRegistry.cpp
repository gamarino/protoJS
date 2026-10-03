#include "BehaviorRegistry.h"
#include <atomic>
#include <cstdint>
#include "GcScopedCache.h"
#include <map>
#include <unordered_map>
#include <vector>
#include "../JSSymbols.h"
#include "../ObjectPrototype.h"
#include "../TypedArrayPrototype.h"

namespace protojs {

    // FrozenBehavior — reject all writes. Returning `obj` unchanged
    // signals to the caller (resolvePutFieldOOP / resolveElementOOP)
    // that the write was handled with no change; returning nullptr
    // would (incorrectly) make the caller fall back to setAttribute
    // and overwrite anyway. Matches sloppy-mode spec semantics where
    // writes to frozen properties silently no-op.
    const proto::ProtoObject* FrozenBehavior::putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const {
        (void)ctx; (void)key; (void)val;
        return obj;
    }
    const proto::ProtoObject* FrozenBehavior::putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const {
        (void)ctx; (void)index; (void)val;
        return obj;
    }

    // NonExtensibleBehavior — allow updates to existing properties,
    // reject creation of new ones. Same "return obj on rejection"
    // convention as FrozenBehavior.
    const proto::ProtoObject* NonExtensibleBehavior::putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const {
        if (!obj || !key) return obj;
        if (obj->hasAttribute(ctx, key) == PROTO_TRUE) {
            return obj->setAttribute(ctx, key, val);
        }
        return obj;
    }
    const proto::ProtoObject* NonExtensibleBehavior::putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const {
        (void)ctx; (void)index; (void)val;
        return obj;
    }

    // Implementation of TypedArrayBehavior: an integer-indexed exotic
    // object (§10.4.5). Integer indices read and write the bytes of the
    // underlying ArrayBuffer, never ordinary attributes; an index out of
    // bounds reads undefined and a write to it is ignored (both are
    // handled by typedArrayGetElement / typedArraySetElement).
    class TypedArrayBehavior : public JSObjectBehavior {
        uint8_t elemType;
    public:
        explicit TypedArrayBehavior(uint8_t et) : elemType(et) {}
        uint8_t getTypedArrayElementType() const override { return elemType; }

        const proto::ProtoObject* getElement(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* obj,
                                             uint32_t index) const override {
            return typedArrayGetElement(ctx, obj, index, elemType);
        }

        // Returns the receiver itself (writes happen in place in the
        // buffer), which tells resolvePutElementOOP the write was handled.
        const proto::ProtoObject* putElement(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* obj,
                                             uint32_t index,
                                             const proto::ProtoObject* val) const override {
            return typedArraySetElement(ctx, obj, index, val, elemType);
        }
    };

    // Helper: Composite behavior for multiple inheritance markers
    class CompositeBehavior : public JSObjectBehavior {
        std::vector<const JSObjectBehavior*> behaviors;
    public:
        explicit CompositeBehavior(const std::vector<const JSObjectBehavior*>& b) : behaviors(b) {}

        const proto::ProtoObject* getElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index) const override {
            for (auto b : behaviors) {
                const proto::ProtoObject* res = b->getElement(ctx, obj, index);
                if (res && res != (const proto::ProtoObject*)0) return res;
            }
            return nullptr;
        }

        const proto::ProtoObject* putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const override {
            for (auto b : behaviors) {
                const proto::ProtoObject* res = b->putElement(ctx, obj, index, val);
                if (res) return res;
            }
            return nullptr;
        }

        // Object.freeze attaches BOTH the FrozenMarker and the
        // NonExtensibleMarker, so the resolved behavior is a composite.
        // Pre-fix this class did not override putField, so the composite
        // fell through to the default impl (returns nullptr) and the
        // caller wrote anyway. Forward the call to each child behavior
        // and propagate the strictest result.
        const proto::ProtoObject* putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const override {
            for (auto b : behaviors) {
                const proto::ProtoObject* res = b->putField(ctx, obj, key, val);
                if (res) return res;
            }
            return nullptr;
        }

        const proto::ProtoObject* getField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key) const override {
            // Default getField returns obj->getAttribute. Use the first
            // child that returns a non-null result; fall through to the
            // chain walk via the default impl when all decline.
            for (auto b : behaviors) {
                const proto::ProtoObject* res = b->getField(ctx, obj, key);
                if (res && res != PROTO_NONE) return res;
            }
            return obj ? obj->getAttribute(ctx, key, true) : PROTO_NONE;
        }

        uint8_t getTypedArrayElementType() const override {
            for (auto b : behaviors) {
                uint8_t et = b->getTypedArrayElementType();
                if (et != 0xFF) return et;
            }
            return 0xFF;
        }
    };

    BehaviorRegistry& BehaviorRegistry::instance() {
        static BehaviorRegistry instance;
        return instance;
    }

    BehaviorRegistry::BehaviorRegistry() : defaultBehavior(std::make_unique<JSObjectBehavior>()) {}

    void BehaviorRegistry::registerBehavior(const proto::ProtoObject* marker, std::unique_ptr<JSObjectBehavior> behavior) {
        if (!marker || marker == (const proto::ProtoObject*)0) return;
        registry[marker] = std::move(behavior);
    }

    void BehaviorRegistry::registerTypedArrayBehavior(const proto::ProtoObject* proto, uint8_t elemType) {
        registerBehavior(proto, std::make_unique<TypedArrayBehavior>(elemType));
    }

    // Shared per-object cache definition. resolve() reads and writes it
    // on the fast path; invalidateObjectCache() drops the entry after
    // Object.{freeze, seal, preventExtensions} mutates parents in place.
    namespace {
        struct ObjCacheSlot { const proto::ProtoObject* obj; const JSObjectBehavior* behavior; };
        thread_local ObjCacheSlot t_objCache[256];
        // The slots are keyed by object address. Once an object is collected
        // its cell is reused, and a fresh ordinary object at that address
        // would inherit the dead one's behaviour (a frozen object's, which
        // silently drops writes). The slots are therefore emptied whenever a
        // collection has started since the last resolve (GcScopedCache.h).
        thread_local GcCycleWatch t_objCacheCycle;
        // Object.{freeze, seal, preventExtensions} on ANY thread invalidates
        // the slot of that object in EVERY thread's cache: the Deferred pool
        // (src/DeferredPool.h) runs JavaScript on several threads that share
        // objects, and a thread that kept the pre-freeze behaviour would keep
        // writing to a frozen object. Each invalidation bumps this epoch; a
        // thread whose cache predates it empties the cache on its next
        // resolve(). Integrity changes are rare, so this costs one relaxed
        // load per resolve().
        std::atomic<std::uint64_t> g_integrityEpoch{0};
        thread_local std::uint64_t t_objCacheEpoch = 0;

        // Integrity behaviours are not keyed on a prototype: the level is
        // per-object own state (see ObjectPrototype.h), so resolve() reads
        // it from the receiver and picks one of these singletons.
        const FrozenBehavior        s_frozenBehavior{};
        const NonExtensibleBehavior s_nonExtensibleBehavior{};

        const JSObjectBehavior* integrityBehavior(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* obj) {
            long long bits = jsIntegrityBits(ctx, obj);
            if (bits & kIntegrityFrozen) return &s_frozenBehavior;
            if (bits & (kIntegritySealed | kIntegrityNonExtensible))
                return &s_nonExtensibleBehavior;
            return nullptr;
        }

        size_t objCacheIdx(const proto::ProtoObject* obj) {
            return (reinterpret_cast<size_t>(obj)
                  ^ (reinterpret_cast<size_t>(obj) >> 12)) & 255;
        }
    }

    void BehaviorRegistry::invalidateObjectCache(const proto::ProtoObject* obj) const {
        if (!obj) return;
        g_integrityEpoch.fetch_add(1, std::memory_order_release);
        size_t idx = objCacheIdx(obj);
        if (t_objCache[idx].obj == obj) {
            t_objCache[idx].obj = nullptr;
            t_objCache[idx].behavior = nullptr;
        }
    }

    const JSObjectBehavior* BehaviorRegistry::resolve(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const {
        if (!obj || (const proto::ProtoObject*)obj == (const proto::ProtoObject*)0) {
            return defaultBehavior.get();
        }

        const std::uint64_t epoch = g_integrityEpoch.load(std::memory_order_acquire);
        if (t_objCacheCycle.changed(ctx) || epoch != t_objCacheEpoch) {
            for (auto& slot : t_objCache) slot = ObjCacheSlot{nullptr, nullptr};
            t_objCacheEpoch = epoch;
        }
        size_t objIdx = objCacheIdx(obj);
        if (t_objCache[objIdx].obj == obj && t_objCache[objIdx].behavior) {
            return t_objCache[objIdx].behavior;
        }

        // Resolve by walking ALL parents and collecting every behavior
        // present in the registry (TypedArray element types and the like
        // are keyed on a prototype, so they DO live on the chain), then
        // prepending the receiver's own integrity behaviour if it has
        // one.  The integrity behaviour goes first: CompositeBehavior
        // stops at the first child whose putField returns non-null, and
        // a frozen receiver must get the first say on every write.
        //
        // The per-object t_objCache above protects the hot path; this
        // loop only runs on misses, and jsAddIntegrity drops the entry
        // whenever Object.{freeze,seal,preventExtensions} changes the
        // level.
        const JSObjectBehavior* integrity = integrityBehavior(ctx, obj);
        const proto::ProtoList* parents = obj->getParents(ctx);
        size_t parentCount = parents ? parents->getSize(ctx) : 0;
        const JSObjectBehavior* behavior = defaultBehavior.get();
        if (parentCount == 0) {
            const proto::ProtoObject* p = obj->getFirstParent(ctx);
            auto it = p ? registry.find(p) : registry.end();
            if (integrity && it == registry.end()) {
                behavior = integrity;
            } else if (it != registry.end()) {
                if (integrity) {
                    static thread_local std::map<std::vector<const JSObjectBehavior*>,
                                                 std::unique_ptr<CompositeBehavior>> t_pairCache;
                    std::vector<const JSObjectBehavior*> pair{integrity, it->second.get()};
                    auto& composite = t_pairCache[pair];
                    if (!composite) composite = std::make_unique<CompositeBehavior>(pair);
                    behavior = composite.get();
                } else {
                    behavior = it->second.get();
                }
            }
        } else {
            std::vector<const JSObjectBehavior*> found;
            if (integrity) found.push_back(integrity);
            for (size_t i = 0; i < parentCount; i++) {
                const proto::ProtoObject* p = parents->getAt(ctx, static_cast<int>(i));
                auto it = p ? registry.find(p) : registry.end();
                if (it != registry.end()) found.push_back(it->second.get());
            }
            if (found.size() == 1) {
                behavior = found[0];
            } else if (found.size() > 1) {
                static thread_local std::map<std::vector<const JSObjectBehavior*>, std::unique_ptr<CompositeBehavior>> t_compositeCache;
                auto& composite = t_compositeCache[found];
                if (!composite) composite = std::make_unique<CompositeBehavior>(found);
                behavior = composite.get();
            }
        }

        t_objCache[objIdx].obj = obj;
        t_objCache[objIdx].behavior = behavior;
        return behavior;
    }

} // namespace protojs
