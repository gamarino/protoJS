#include "BehaviorRegistry.h"
#include <map>
#include <unordered_map>
#include <vector>
#include "../JSSymbols.h"

namespace protojs {

    // Implementation of FrozenBehavior
    const proto::ProtoObject* FrozenBehavior::putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const {
        // ES spec: setting a property on a frozen object is a TypeError in strict mode.
        // In ProtoJS we currently follow a "strict by default" approach for frozen objects.
        return nullptr; // Indicates failure/exception
    }
    const proto::ProtoObject* FrozenBehavior::putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const {
        return nullptr;
    }

    // Implementation of NonExtensibleBehavior
    const proto::ProtoObject* NonExtensibleBehavior::putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const {
        if (!obj || !key) return nullptr;
        // If property exists, allow update. If not, reject.
        if (obj->getAttribute(ctx, key, false) != PROTO_NONE) {
            return obj->setAttribute(ctx, key, val);
        }
        return nullptr;
    }
    const proto::ProtoObject* NonExtensibleBehavior::putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const {
        // Elements are always considered "new" if they are not in the fast-path array storage?
        // Actually, NonExtensible only prevents new properties.
        return nullptr; 
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

    const JSObjectBehavior* BehaviorRegistry::resolve(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const {
        if (!obj || (const proto::ProtoObject*)obj == (const proto::ProtoObject*)0) {
            return defaultBehavior.get();
        }

        // Per-object behavior cache. Ultimate fast-path for hot loops.
        // Prevents redundant snapshotting of mutable objects via getFirstParent().
        static thread_local struct {
            const proto::ProtoObject* obj;
            const JSObjectBehavior* behavior;
        } t_objCache[256];
        
        size_t objIdx = (reinterpret_cast<size_t>(obj) ^ (reinterpret_cast<size_t>(obj) >> 12)) & 255;
        if (t_objCache[objIdx].obj == obj && t_objCache[objIdx].behavior) {
            return t_objCache[objIdx].behavior;
        }

        const proto::ProtoObject* parent = obj->getFirstParent(ctx);

        static thread_local struct {
            const proto::ProtoObject* parent;
            const JSObjectBehavior* behavior;
        } t_behaviorCache[256];
        
        size_t cacheIdx = (reinterpret_cast<size_t>(parent) ^ (reinterpret_cast<size_t>(parent) >> 12)) & 255;
        const JSObjectBehavior* behavior = nullptr;

        if (t_behaviorCache[cacheIdx].parent == parent && t_behaviorCache[cacheIdx].behavior) {
            behavior = t_behaviorCache[cacheIdx].behavior;
        } else {
            if (!parent || parent == (const proto::ProtoObject*)0) {
                behavior = defaultBehavior.get();
            } else {
                auto it = registry.find(parent);
                if (it != registry.end()) {
                    behavior = it->second.get();
                } else {
                    // Walk parents manually via getFirstParent to avoid list allocation
                    // if possible, but for multiple inheritance we still need getParents.
                    const proto::ProtoList* parents = obj->getParents(ctx);
                    size_t size = parents ? parents->getSize(ctx) : 0;
                    if (size <= 1) {
                        behavior = defaultBehavior.get();
                    } else {
                        std::vector<const JSObjectBehavior*> found;
                        for (size_t i = 0; i < size; i++) {
                            const proto::ProtoObject* p = parents->getAt(ctx, i);
                            auto it2 = registry.find(p);
                            if (it2 != registry.end()) {
                                found.push_back(it2->second.get());
                            }
                        }
                        if (found.empty()) {
                            behavior = defaultBehavior.get();
                        } else if (found.size() == 1) {
                            behavior = found[0];
                        } else {
                            static thread_local std::map<std::vector<const JSObjectBehavior*>, std::unique_ptr<CompositeBehavior>> t_compositeCache;
                            auto& composite = t_compositeCache[found];
                            if (!composite) {
                                composite = std::make_unique<CompositeBehavior>(found);
                            }
                            behavior = composite.get();
                        }
                    }
                }
            }
            t_behaviorCache[cacheIdx].parent = parent;
            t_behaviorCache[cacheIdx].behavior = behavior;
        }

        t_objCache[objIdx].obj = obj;
        t_objCache[objIdx].behavior = behavior;
        return behavior;
    }

} // namespace protojs
