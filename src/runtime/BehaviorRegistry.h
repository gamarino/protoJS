#ifndef PROTOJS_BEHAVIOR_REGISTRY_H
#define PROTOJS_BEHAVIOR_REGISTRY_H

#include "protoCore.h"
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
 * Maps protoCore marker objects (prototypes) to C++ JSObjectBehavior
 * implementations, one table per protoCore space.
 *
 * Every worker_threads worker has its own space and registers its own
 * typed-array prototypes. The registry used to be one process-wide
 * unordered_map keyed by prototype address: workers inserted into it without a
 * lock while other threads read it (a data race on rehash), and nothing was
 * removed, so after a worker ended a new cell at a recycled address was taken
 * for a typed-array prototype (docs/MEMORY_SAFETY_PROPOSALS.md, section 1).
 *
 * Now each space has its own table. Inserts take a mutex and publish a new
 * immutable copy of the space's table (read-copy-update: inserts happen at
 * start-up only); resolve() reads the current copy with one acquire load and
 * no lock. The copies a space replaced are freed by forgetSpace(), which
 * ~JSContextWrapper calls once no thread of that space runs any more, and
 * which also drops every thread's per-object cache. Behaviours registered
 * here must outlive the process (they are static objects), so a cached
 * pointer to one is never dangling.
 */
class BehaviorRegistry {
public:
    static BehaviorRegistry& instance();

    // Register `behavior` (static storage duration) for `marker` in the
    // registry of ctx's space.
    void registerBehavior(proto::ProtoContext* ctx, const proto::ProtoObject* marker,
                          const JSObjectBehavior* behavior);

    // Register the typed-array behaviour of `elemType` for `proto` in the
    // registry of ctx's space.
    void registerTypedArrayBehavior(proto::ProtoContext* ctx, const proto::ProtoObject* proto,
                                    uint8_t elemType);

    // Resolve the appropriate behavior for an object based on its inheritance chain.
    const JSObjectBehavior* resolve(proto::ProtoContext* ctx, const proto::ProtoObject* obj) const;

    // Drop the per-object behavior cache entry for `obj`. Call after
    // Object.{freeze, seal, preventExtensions} mutates the parent chain
    // — without this the cached "default" behavior from the pre-freeze
    // lookup keeps winning over the freshly attached marker.
    void invalidateObjectCache(const proto::ProtoObject* obj) const;

    // Drop the table of `space` and every thread's per-object cache. Called
    // by ~JSContextWrapper when no thread of that space runs any more.
    static void forgetSpace(const proto::ProtoSpace* space);

    // Get the default behavior (fallback)
    const JSObjectBehavior* getDefault() const { return defaultBehavior.get(); }

private:
    BehaviorRegistry();
    ~BehaviorRegistry() = default;

    std::unique_ptr<JSObjectBehavior> defaultBehavior;
};

} // namespace protojs

#endif // PROTOJS_BEHAVIOR_REGISTRY_H
