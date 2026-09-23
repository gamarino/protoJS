#ifndef PROTOJS_OBJECTPROTOTYPE_H
#define PROTOJS_OBJECTPROTOTYPE_H

#include "protoCore.h"

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
 * Per-instance Symbol identity registry — populated by the Symbol()
 * constructor when it stashes the per-instance __symbol_str_key__.
 * Used by Object.getOwnPropertySymbols / Reflect.ownKeys to translate
 * the internal string-keyed attribute name back to the originating
 * Symbol value.
 */
void registerSymbolByStrKey(const std::string& key, const proto::ProtoObject* sym);
const proto::ProtoObject* lookupSymbolByStrKey(const std::string& key);

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

// ---------------------------------------------------------------------------
// Object integrity level — ECMA-262 §7.3.15 SetIntegrityLevel, §7.3.16
// TestIntegrityLevel and the [[Extensible]] internal slot.
//
// The level belongs to ONE object.  It is never inherited: per §10.1.2 an
// object created with `Object.create(frozenProto)` or `new F()` with a
// frozen `F.prototype`, and an object re-pointed at a frozen prototype with
// `Object.setPrototypeOf`, are all still extensible.
//
// It is therefore stored as an internal OWN attribute ("__integrity__",
// a bitmask) and read with own-attribute probes only — `getOwnAttributeDirect`
// never walks the parent chain, so a frozen prototype cannot leak its level
// to a child.  The key matches the "__name__" internal pattern, so every
// JS-visible enumeration surface (Object.keys / getOwnPropertyNames / for-in
// / JSON.stringify / Object.assign) already filters it out.
//
// This replaces the previous scheme, which recorded the level by attaching
// marker objects to the protoCore parent chain and tested it with
// `hasParent`.  protoCore 2.0.0's `newChild` captures the prototype's
// CURRENT chain by value, so from that release on every child of a frozen
// prototype inherited the markers and came out frozen — and, because the
// markers were prepended, `getPrototypeOf` on a frozen object reported a
// marker instead of its real prototype.
// ---------------------------------------------------------------------------

enum : long long {
    kIntegrityNonExtensible = 0x1,  // Object.preventExtensions
    kIntegritySealed        = 0x2,  // Object.seal   (implies non-extensible)
    kIntegrityFrozen        = 0x4,  // Object.freeze (implies sealed)
};

/** The object's own integrity bitmask; 0 when it carries none. */
long long jsIntegrityBits(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/**
 * OR `bits` into obj's own integrity mask and drop obj's BehaviorRegistry
 * cache entry.  A no-op when obj is not an object cell.  Like the marker
 * scheme it replaces, this records nothing on an immutable receiver: a
 * mutable object is updated in place, whereas an immutable one would have
 * to be rebuilt into a new handle the caller cannot publish.
 */
void jsAddIntegrity(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                    long long bits);

inline bool jsIsNonExtensible(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return (jsIntegrityBits(ctx, obj) & kIntegrityNonExtensible) != 0;
}
inline bool jsIsSealed(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return (jsIntegrityBits(ctx, obj) & kIntegritySealed) != 0;
}
inline bool jsIsFrozen(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return (jsIntegrityBits(ctx, obj) & kIntegrityFrozen) != 0;
}

} // namespace protojs

#endif // PROTOJS_OBJECTPROTOTYPE_H
