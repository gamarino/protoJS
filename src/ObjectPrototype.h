#ifndef PROTOJS_OBJECTPROTOTYPE_H
#define PROTOJS_OBJECTPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Install Object.prototype instance methods (hasOwnProperty, toString, valueOf,
 * propertyIsEnumerable, isPrototypeOf) on the given base object.
 *
 * Returns the updated object (protoCore objects are immutable; setAttribute returns a
 * new root). Callers that pass space->objectPrototype must update that pointer with
 * the return value so that all objects created from it inherit the methods.
 */
const proto::ProtoObject* installObjectInstanceMethods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* base);

/**
 * Ensure the Object constructor with static methods is registered in the global root.
 * Idempotent — no-op when "Object" is already present.
 */
void ensureObjectConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

/**
 * Return the explicit JS [[Prototype]] override for obj, as set by
 * Object.setPrototypeOf(). Returns nullptr when no override exists.
 * Used by the for-in walk in ProtoInterpreter to honour setPrototypeOf.
 */
const proto::ProtoObject* getJSProtoOverride(const proto::ProtoObject* obj);

/**
 * Record an explicit JS [[Prototype]] override for obj. Used by OP_define_class
 * so Object.getPrototypeOf(DerivedClass) === ParentClass.
 * Pass nullptr to clear any prior override (equivalent to "no override").
 */
void setJSProtoOverride(const proto::ProtoObject* obj,
                        const proto::ProtoObject* proto);

} // namespace protojs

#endif // PROTOJS_OBJECTPROTOTYPE_H
