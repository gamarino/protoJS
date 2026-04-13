#ifndef PROTOJS_PROTOTYPEUTILS_H
#define PROTOJS_PROTOTYPEUTILS_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Install fn as a non-enumerable, configurable, writable method on proto.
 *
 * Creates a mutable wrapper object (child of methodPrototype) carrying:
 *   __native_fn__  → the raw ProtoMethod pointer
 *   length         → argc  (descriptor: writable=false, enumerable=false, configurable=true)
 *   name           → methodName (same descriptor)
 *
 * Then installs the wrapper on proto under key methodName with descriptor:
 *   writable=true, enumerable=false, configurable=true
 *
 * Returns the updated proto pointer (must be captured by caller).
 */
[[nodiscard]] const proto::ProtoObject* installNonEnumerableMethod(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    const char* methodName,
    proto::ProtoMethod fn,
    int argc);

} // namespace protojs

#endif // PROTOJS_PROTOTYPEUTILS_H
