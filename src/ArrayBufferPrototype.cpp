#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

namespace protojs {

void ensureArrayBufferConstructor(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot) {}

const proto::ProtoObject* createArrayBuffer(proto::ProtoContext* ctx,
                                            unsigned long byteLength) {
    return PROTO_NONE;
}

void* getArrayBufferRawPtr(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    return nullptr;
}

unsigned long getArrayBufferByteLength(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    return 0;
}

bool isArrayBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    return false;
}

} // namespace protojs
