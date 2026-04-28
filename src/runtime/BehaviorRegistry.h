#ifndef PROTOJS_BEHAVIOR_REGISTRY_H
#define PROTOJS_BEHAVIOR_REGISTRY_H

#include "headers/protoCore.h"
#include <unordered_map>
#include <memory>

namespace protojs {

/**
 * JSObjectBehavior
 * 
 * Base class for polymorphic object behavior. Default implementation
 * delegates directly to protoCore's getAttribute/setAttribute methods.
 */
class JSObjectBehavior {
public:
    virtual ~JSObjectBehavior() = default;

    virtual const proto::ProtoObject* getField(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        const proto::ProtoString* key) const 
    {
        return obj ? obj->getAttribute(ctx, key, true) : PROTO_NONE;
    }

    virtual const proto::ProtoObject* putField(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        const proto::ProtoString* key, 
        const proto::ProtoObject* val) const 
    {
        return nullptr;
    }

    virtual const proto::ProtoObject* getElement(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        uint32_t index) const 
    {
        return nullptr;
    }

    virtual const proto::ProtoObject* putElement(
        proto::ProtoContext* ctx, 
        const proto::ProtoObject* obj, 
        uint32_t index,
        const proto::ProtoObject* val) const 
    {
        return nullptr;
    }

    // Identifies if this behavior represents a TypedArray and returns its element type.
    // Returns 0xFF if not a TypedArray.
    virtual uint8_t getTypedArrayElementType() const {
        return 0xFF;
    }
};

/**
 * FrozenBehavior
 * Rejects all writes with a TypeError.
 */
class FrozenBehavior : public JSObjectBehavior {
public:
    const proto::ProtoObject* putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const override;
    const proto::ProtoObject* putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const override;
};

/**
 * NonExtensibleBehavior
 * Rejects creation of new properties.
 */
class NonExtensibleBehavior : public JSObjectBehavior {
public:
    const proto::ProtoObject* putField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) const override;
    const proto::ProtoObject* putElement(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) const override;
};


/**
 * BehaviorRegistry
 * 
 * Maps protoCore marker objects (Prototypes or special Parent markers)
 * to C++ JSObjectBehavior implementations.
 */
class BehaviorRegistry {
public:
    static BehaviorRegistry& instance();

    // Register a behavior mapping for a specific marker object.
    void registerBehavior(const proto::ProtoObject* marker, std::unique_ptr<JSObjectBehavior> behavior);

    // Register typed array behaviors based on their prototype
    void registerTypedArrayBehavior(const proto::ProtoObject* proto, uint8_t elemType);

    // Resolve the appropriate behavior for an object based on its inheritance chain.
    const JSObjectBehavior* resolve(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const;

    // Get the default behavior (fallback)
    const JSObjectBehavior* getDefault() const { return defaultBehavior.get(); }

private:
    BehaviorRegistry();
    ~BehaviorRegistry() = default;

    std::unique_ptr<JSObjectBehavior> defaultBehavior;
    std::unordered_map<const proto::ProtoObject*, std::unique_ptr<JSObjectBehavior>> registry;
};

} // namespace protojs

#endif // PROTOJS_BEHAVIOR_REGISTRY_H
