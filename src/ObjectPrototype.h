#ifndef PROTOJS_OBJECTPROTOTYPE_H
#define PROTOJS_OBJECTPROTOTYPE_H

#include "protoCore.h"
#include <string>
#include <vector>

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
 * Object.create(null) / Object.setPrototypeOf() / class definitions.
 * Returns nullptr when no override exists. The override is the object's own
 * internal state (see ObjectPrototype.cpp, "JS [[Prototype]] overrides"), so it
 * lives and dies with the object.
 */
const proto::ProtoObject* getJSProtoOverride(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* obj);

/**
 * Drop the immutable-object [[Prototype]] side table of `space`. Called by
 * ~JSContextWrapper once no thread of that space runs any more.
 */
void forgetSpaceProtoOverrides(const proto::ProtoSpace* space);

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
void registerSymbolByStrKey(proto::ProtoContext* ctx, const std::string& key,
                            const proto::ProtoObject* sym);
const proto::ProtoObject* lookupSymbolByStrKey(proto::ProtoContext* ctx, const std::string& key);

/**
 * Symbol.for's registry (ECMA-262 GlobalSymbolRegistry), one per wrapper (so
 * one per worker), retained for the wrapper's life like the map above.
 */
const proto::ProtoObject* symbolForRegistryGet(proto::ProtoContext* ctx, const std::string& key);
/**
 * Register `sym` under `key` unless a symbol is already registered there, and
 * return the registered symbol. First-wins: two threads of one space (the
 * Deferred pool) calling Symbol.for(key) at once both receive the same symbol.
 */
const proto::ProtoObject* symbolForRegistrySet(proto::ProtoContext* ctx, const std::string& key,
                                               const proto::ProtoObject* sym);

/**
 * Symbol-keyed properties are stored under an interned attribute name
 * "@@sym#<n>", where n is the symbol's creation number (makeSymbolStorageKey,
 * called by the Symbol() constructor).
 *
 * protoCore orders an object's attributes by the address of the interned key,
 * so the order in which a walk of the attributes meets two symbol keys is the
 * order in which the allocator happened to place their names -- the same on
 * Linux run after run, different on macOS and Windows. Every place that
 * reports symbol keys therefore orders them by creation number instead,
 * which is the chronological order of OrdinaryOwnPropertyKeys whenever the
 * properties were added in the order their symbols were created, and is the
 * same on every platform.
 */
std::string makeSymbolStorageKey();
bool isSymbolStorageKey(const std::string& key);
/** The creation number in a "@@sym#<n>" key; 0 for any other key. */
unsigned long long symbolStorageKeySequence(const std::string& key);
/** The creation number of a Symbol value (its __symbol_str_key__); 0 if none. */
unsigned long long symbolSequence(proto::ProtoContext* ctx, const proto::ProtoObject* sym);
/**
 * Orders storage keys as [[OwnPropertyKeys]] reports them: canonical array
 * indices in ascending numeric order, then the other string keys in the order
 * given, then symbol storage keys by creation number.
 */
void orderOwnPropertyKeys(std::vector<std::string>& keys);
/** Stable-sorts Symbol values by creation number. */
void sortSymbolsByCreation(proto::ProtoContext* ctx, std::vector<const proto::ProtoObject*>& syms);

/**
 * Record an explicit JS [[Prototype]] override for obj without touching its
 * protoCore parent chain. Pass nullptr to clear any prior override.
 */
void setJSProtoOverrideOnly(proto::ProtoContext* ctx,
                            const proto::ProtoObject* obj,
                            const proto::ProtoObject* proto);

/**
 * Same as setJSProtoOverride above, but ALSO rebinds the protoCore parent
 * chain via ProtoObject::setParents when obj is mutable.  Result: the
 * native protoCore walk that backs every getAttribute already sees the
 * new prototype — `resolveFieldOOP`'s extension fallback via
 * the recorded override only ever matters for the immutable edge case.
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
