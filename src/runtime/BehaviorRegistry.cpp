#include "BehaviorRegistry.h"
#include "ProtoInterpreter.h"
#include "../TypedArrayPrototype.h"
#include "../JSSymbols.h"
#include <string>
#include <algorithm>
#include <vector>
#include <map>
#include <memory>

namespace protojs {

// ---------------------------------------------------------------------------
// TypedArrayBehavior
// ---------------------------------------------------------------------------
class TypedArrayBehavior : public JSObjectBehavior {
    uint8_t m_elemType;
public:
    explicit TypedArrayBehavior(uint8_t elemType) : m_elemType(elemType) {}

    virtual const proto::ProtoObject* getField(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        const proto::ProtoString* key) const override 
    {
        // 1. Try dictionary fast path for normal properties (like length, slice, etc)
        const proto::ProtoObject* val = obj->getAttribute(ctx, key, true);
        if (val && val != PROTO_NONE) {
            return val;
        }

        // 2. Fallback to typed array numeric element access
        std::string keyStr = key->toStdString(ctx);
        bool isNumeric = !keyStr.empty() && std::all_of(keyStr.begin(), keyStr.end(), ::isdigit);
        if (isNumeric) {
            uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
            return typedArrayGetElement(ctx, obj, idx, m_elemType);
        }

        return PROTO_NONE;
    }

    virtual const proto::ProtoObject* getElement(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        uint32_t index) const override 
    {
        return typedArrayGetElement(ctx, obj, index, m_elemType);
    }

    virtual const proto::ProtoObject* putElement(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        uint32_t index,
        const proto::ProtoObject* val) const override 
    {
        return typedArraySetElement(ctx, obj, index, val, m_elemType);
    }

    virtual const proto::ProtoObject* putField(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        const proto::ProtoString* key, 
        const proto::ProtoObject* val) const override 
    {
        std::string keyStr = key->toStdString(ctx);
        bool isNumeric = !keyStr.empty() && std::all_of(keyStr.begin(), keyStr.end(), ::isdigit);
        if (isNumeric) {
            uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
            return typedArraySetElement(ctx, obj, idx, val, m_elemType);
        }
        return obj->setAttribute(ctx, key, val);
    }

    virtual uint8_t getTypedArrayElementType() const override {
        return m_elemType;
    }
};

// ---------------------------------------------------------------------------
// FrozenBehavior
// ---------------------------------------------------------------------------
const proto::ProtoObject* FrozenBehavior::putField(
    proto::ProtoContext* ctx, 
    const proto::ProtoObject* obj, 
    const proto::ProtoString* key, 
    const proto::ProtoObject* val) const 
{
    signalNativeException(makeNativeError(ctx, "TypeError", "Cannot assign to property of frozen object"));
    return PROTO_NONE;
}

const proto::ProtoObject* FrozenBehavior::putElement(
    proto::ProtoContext* ctx, 
    const proto::ProtoObject* obj, 
    uint32_t index, 
    const proto::ProtoObject* val) const 
{
    signalNativeException(makeNativeError(ctx, "TypeError", "Cannot assign to element of frozen object"));
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// NonExtensibleBehavior
// ---------------------------------------------------------------------------
const proto::ProtoObject* NonExtensibleBehavior::putField(
    proto::ProtoContext* ctx, 
    const proto::ProtoObject* obj, 
    const proto::ProtoString* key, 
    const proto::ProtoObject* val) const 
{
    // If property exists (is not nullptr), we can update it.
    if (obj->getAttribute(ctx, key, false) == nullptr) {
        signalNativeException(makeNativeError(ctx, "TypeError", "Object is not extensible"));
        return PROTO_NONE;
    }
    return nullptr; // Pass through to allow standard update
}

const proto::ProtoObject* NonExtensibleBehavior::putElement(
    proto::ProtoContext* ctx, 
    const proto::ProtoObject* obj, 
    uint32_t index, 
    const proto::ProtoObject* val) const 
{
    const proto::ProtoString* key = JSSymbols::indexKey(ctx, index);
    if (obj->getAttribute(ctx, key, false) == nullptr) {
        // For TypedArrays, numeric elements always "exist" if in bounds.
        // But NonExtensibleBehavior usually applies to named properties.
        // If it's a TypedArray, it will handle it in its own putElement.
        // If we reach here for a standard object, we block it.
        signalNativeException(makeNativeError(ctx, "TypeError", "Object is not extensible"));
        return PROTO_NONE;
    }
    return nullptr;
}


BehaviorRegistry& BehaviorRegistry::instance() {
    static BehaviorRegistry _instance;
    return _instance;
}

BehaviorRegistry::BehaviorRegistry() {
    defaultBehavior = std::make_unique<JSObjectBehavior>();
}

void BehaviorRegistry::registerBehavior(const proto::ProtoObject* marker, std::unique_ptr<JSObjectBehavior> behavior) {
    if (marker) {
        registry[marker] = std::move(behavior);
    }
}

void BehaviorRegistry::registerTypedArrayBehavior(const proto::ProtoObject* proto, uint8_t elemType) {
    registerBehavior(proto, std::make_unique<TypedArrayBehavior>(elemType));
}

class CompositeBehavior : public JSObjectBehavior {
    std::vector<const JSObjectBehavior*> m_behaviors;
public:
    explicit CompositeBehavior(std::vector<const JSObjectBehavior*> behaviors) 
        : m_behaviors(std::move(behaviors)) {}

    const proto::ProtoObject* getField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key) const override {
        for (auto b : m_behaviors) {
            const proto::ProtoObject* val = b->getField(ctx, obj, key);
            if (val && val != PROTO_NONE) return val;
        }
        return nullptr;
    }

    const proto::ProtoObject* putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const override {
        for (auto b : m_behaviors) {
            const proto::ProtoObject* res = b->putField(ctx, obj, key, val);
            if (res && res != obj) return res;
        }
        return obj->setAttribute(ctx, key, val);
    }

    const proto::ProtoObject* getElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index) const override {
        for (auto b : m_behaviors) {
            const proto::ProtoObject* val = b->getElement(ctx, obj, index);
            if (val && val != PROTO_NONE) return val;
        }
        return nullptr;
    }

    const proto::ProtoObject* putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const override {
        for (auto b : m_behaviors) {
            const proto::ProtoObject* res = b->putElement(ctx, obj, index, val);
            if (res && res != obj) return res;
        }
        return obj->setAttribute(ctx, JSSymbols::indexKey(ctx, index), val);
    }

    uint8_t getTypedArrayElementType() const override {
        for (auto b : m_behaviors) {
            uint8_t t = b->getTypedArrayElementType();
            if (t != 0xFF) return t;
        }
        return 0xFF;
    }
};

const JSObjectBehavior* BehaviorRegistry::resolve(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const {
    if (!obj || obj == PROTO_NONE) {
        return defaultBehavior.get();
    }

    const proto::ProtoList* parents = obj->getParents(ctx);
    if (!parents || (const proto::ProtoObject*)parents == PROTO_NONE) {
        return defaultBehavior.get();
    }

    static thread_local const proto::ProtoList* t_lastParents = nullptr;
    static thread_local const JSObjectBehavior* t_lastBehavior = nullptr;
    if (parents == t_lastParents && t_lastBehavior) return t_lastBehavior;

    const JSObjectBehavior* behavior = nullptr;
    size_t size = parents->getSize(ctx);
    
    if (size == 0) {
        behavior = defaultBehavior.get();
    } else if (size == 1) {
        const proto::ProtoObject* parent = parents->getAt(ctx, 0);
        auto it = registry.find(parent);
        if (it != registry.end()) behavior = it->second.get();
        else behavior = defaultBehavior.get();
    } else {
        std::vector<const JSObjectBehavior*> found;
        for (int i = static_cast<int>(size) - 1; i >= 0; --i) {
            const proto::ProtoObject* parent = parents->getAt(ctx, i);
            if (parent && parent != PROTO_NONE) {
                auto it = registry.find(parent);
                if (it != registry.end()) {
                    found.push_back(it->second.get());
                }
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

    t_lastParents = parents;
    t_lastBehavior = behavior;
    return behavior;
}

} // namespace protojs
