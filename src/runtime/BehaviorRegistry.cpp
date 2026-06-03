#include "BehaviorRegistry.h"
#include <map>
#include <unordered_map>
#include <vector>
#include "../JSSymbols.h"

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
        if (obj->getAttribute(ctx, key, false) != PROTO_NONE) {
            return obj->setAttribute(ctx, key, val);
        }
        return obj;
    }
    const proto::ProtoObject* NonExtensibleBehavior::putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const {
        (void)ctx; (void)index; (void)val;
        return obj;
    }

    // Implementation of TypedArrayBehavior
    class TypedArrayBehavior : public JSObjectBehavior {
        uint8_t elemType;
    public:
        explicit TypedArrayBehavior(uint8_t et) : elemType(et) {}
        uint8_t getTypedArrayElementType() const override { return elemType; }
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

        size_t objCacheIdx(const proto::ProtoObject* obj) {
            return (reinterpret_cast<size_t>(obj)
                  ^ (reinterpret_cast<size_t>(obj) >> 12)) & 255;
        }
    }

    void BehaviorRegistry::invalidateObjectCache(const proto::ProtoObject* obj) const {
        if (!obj) return;
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

        size_t objIdx = objCacheIdx(obj);
        if (t_objCache[objIdx].obj == obj && t_objCache[objIdx].behavior) {
            return t_objCache[objIdx].behavior;
        }

        // Resolve by walking ALL parents and collecting every behavior
        // present in the registry. The pre-fix scheme cached behavior
        // keyed on getFirstParent — but Object.freeze attaches BOTH a
        // FrozenMarker and a NonExtensibleMarker while leaving
        // Object.prototype at the head, so the keyed cache aliased
        // every freshly minted object share-the-same-prototype to one
        // stale behavior. The per-object t_objCache further down still
        // protects the hot path; this loop only runs on misses.
        const proto::ProtoList* parents = obj->getParents(ctx);
        size_t parentCount = parents ? parents->getSize(ctx) : 0;
        const JSObjectBehavior* behavior = defaultBehavior.get();
        if (parentCount == 0) {
            const proto::ProtoObject* p = obj->getFirstParent(ctx);
            auto it = p ? registry.find(p) : registry.end();
            if (it != registry.end()) behavior = it->second.get();
        } else {
            std::vector<const JSObjectBehavior*> found;
            for (size_t i = 0; i < parentCount; i++) {
                const proto::ProtoObject* p = parents->getAt(ctx, i);
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
