#pragma once

// The lazily created `prototype` of an ordinary function.
//
// An ordinary `function` has a `prototype` property whose object points back
// at the function through `constructor` (ECMA-262 §10.2.5 MakeConstructor).
// Both are mutable, so the pair is a cycle among mutables, which protoCore
// never collects (protoCore docs/MemoryModel.md §7): every closure that had the
// pair was retained for the life of the process, and a loop creating ordinary
// closures exhausted memory. Like QuickJS, protoJS therefore creates the object
// on first need. A closure is born with the property's descriptor
// (__pd_prototype__: writable, not enumerable, not configurable) and the own
// marker __lazy_prototype__, but without the property itself; a function that
// never touches `prototype` forms no cycle and is collected.
//
// The property must exist for every observer, so every place that can observe
// it materialises it first:
//   - an access that names the key `prototype` -- get, set, define, has, in,
//     delete, getOwnPropertyDescriptor -- through materializeLazyPrototypeForKey,
//     which also covers an inherited lookup (Object.create(f).prototype);
//   - a use of the constructor's prototype -- new, Reflect.construct,
//     instanceof, class extends, super() -- through materializeLazyPrototype;
//   - an enumeration of the function's own keys, and the integrity operations
//     that walk them (freeze, seal, preventExtensions), through
//     materializeLazyPrototype.
// Once created, the object is an ordinary own property: its identity is stable
// and the marker is cleared.

#include "../JSSymbols.h"
#include <protoCore.h>

namespace protojs {

// Creates fn.prototype (and its `constructor` back-reference) if fn is an
// ordinary function whose prototype has not been created yet. Idempotent and
// cheap otherwise: one own-attribute probe.
void materializeLazyPrototype(proto::ProtoContext* ctx, const proto::ProtoObject* fn);

// materializeLazyPrototype on obj and up its prototype chain until an object
// with an own `prototype` is reached.
void materializeLazyPrototypeOnChain(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

// True if key, a heap string that is not interned, spells "prototype".
bool keyNamesPrototype(proto::ProtoContext* ctx, const proto::ProtoString* key);

// For property accesses: does the work only when key is "prototype". Interned
// keys (every key in bytecode, every key a put interns) compare by pointer, so
// every other access pays one comparison and a tag test; a computed key that
// was not interned is compared by content.
inline void materializeLazyPrototypeForKey(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* obj,
                                           const proto::ProtoString* key) {
    if (!key || !obj) return;
    if (key == JSSymbols::prototype(ctx)
        || (!key->isSymbol() && keyNamesPrototype(ctx, key)))
        materializeLazyPrototypeOnChain(ctx, obj);
}

}  // namespace protojs
