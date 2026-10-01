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

} // namespace protojs

#endif // PROTOJS_ARRAYBUFFERPROTOTYPE_H
