#ifndef PROTOJS_ITERATORPROTOTYPE_H
#define PROTOJS_ITERATORPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Returns the shared %IteratorPrototype% per ECMA-262 §27.1.2.
 *
 * It carries:
 *   - [@@iterator]() that returns `this` — required by GetIterator so
 *     Array.from / for-of can accept an iterator instance.
 *   - [@@toStringTag] = "Iterator" with descriptor 0x2 (configurable
 *     but non-writable, non-enumerable) per ES2024+ §27.1.2.2.
 *
 * Concrete iterator prototypes (Array Iterator, Map Iterator, Set
 * Iterator, String Iterator, RegExp String Iterator, %GeneratorPrototype%)
 * should chain to this object so that `Object.prototype.toString.call(it)`
 * returns "[object Array Iterator]" but `Object.prototype.toString.call(
 * Object.getPrototypeOf(<arr iter>).__proto__)` returns "[object Iterator]"
 * (built-ins/IteratorPrototype/Symbol.toStringTag).
 *
 * Lazily created on first use.  Singleton per process.
 */
const proto::ProtoObject* getIteratorPrototype(proto::ProtoContext* ctx);

/**
 * Installs the ES2024 Iterator static methods (Iterator.from / .concat /
 * .zip / .zipKeyed) onto the supplied stub constructor cell.  Returns
 * the new (possibly-rebuilt) stub since setAttribute returns a new
 * pointer on every call.
 */
const proto::ProtoObject* installIteratorStatics(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* stub);

} // namespace protojs

#endif // PROTOJS_ITERATORPROTOTYPE_H
