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
 * ToPropertyKey(V): coerce any JS value to a ProtoString property key by
 * routing through Symbol.toPrimitive / toString / valueOf as needed.
 * Returns nullptr on coercion failure (and signals a TypeError); abrupt
 * completions from user-supplied coercion methods propagate via
 * hasCallException — callers must check.
 */
const proto::ProtoString* toPropertyKey(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* value);

/**
 * Record an explicit JS [[Prototype]] override for obj. Used by OP_define_class
 * so Object.getPrototypeOf(DerivedClass) === ParentClass.
 * Pass nullptr to clear any prior override (equivalent to "no override").
 */
void setJSProtoOverride(const proto::ProtoObject* obj,
                        const proto::ProtoObject* proto);

/**
 * Same as setJSProtoOverride above, but ALSO rebinds the protoCore parent
 * chain via ProtoObject::setParents when obj is mutable.  Result: the
 * native protoCore walk that backs every getAttribute already sees the
 * new prototype — `resolveFieldOOP`'s extension fallback via
 * `t_jsProtoMap` only ever fires for the immutable edge case.
 *
 * For null sentinel (`Object.setPrototypeOf(o, null)`), only the map is
 * updated — emptying the parent list would expose protoCore's internal
 * default parent and is harder to reverse.  Same for clearing
 * (proto==nullptr).
 *
 * Migration strategy (deferred to a series of one-site-per-commit
 * patches): each call site of the 2-arg form is migrated to this 3-arg
 * form, verified against test262 focal areas, and committed independently.
 */
void setJSProtoOverride(proto::ProtoContext* ctx,
                        const proto::ProtoObject* obj,
                        const proto::ProtoObject* proto);

} // namespace protojs

#endif // PROTOJS_OBJECTPROTOTYPE_H
