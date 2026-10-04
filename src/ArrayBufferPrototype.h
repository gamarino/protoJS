#ifndef PROTOJS_ARRAYBUFFERPROTOTYPE_H
#define PROTOJS_ARRAYBUFFERPROTOTYPE_H

#include "ProtoCoreTypes.h"
#include "protoCore.h"

namespace protojs {

void ensureArrayBufferConstructor(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot);

const proto::ProtoObject* createArrayBuffer(proto::ProtoContext* ctx,
                                            proto::proto_ulong byteLength);

void* getArrayBufferRawPtr(proto::ProtoContext* ctx, const proto::ProtoObject* ab);

proto::proto_ulong getArrayBufferByteLength(proto::ProtoContext* ctx, const proto::ProtoObject* ab);

bool isArrayBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* ab);

/**
 * new ArrayBuffer(length) with NewTarget's prototype `proto` (ECMA-262
 * §25.1.4.1): the length goes through ToIndex, so a string or boolean
 * converts and a negative or too-large length is a RangeError.  `proto` that
 * is not an object selects ArrayBuffer.prototype.  Returns PROTO_NONE with
 * the exception signalled (hasCallException) on failure.
 */
const proto::ProtoObject* constructArrayBuffer(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* proto,
                                               const proto::ProtoList* args);

} // namespace protojs

#endif // PROTOJS_ARRAYBUFFERPROTOTYPE_H
