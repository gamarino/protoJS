#include "TypedArrayPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

namespace protojs {

void ensureTypedArrayConstructors(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot) {}

const proto::ProtoObject* typedArrayGetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               uint8_t elementType) {
    return PROTO_NONE;
}

const proto::ProtoObject* typedArraySetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               const proto::ProtoObject* value,
                                               uint8_t elementType) {
    return const_cast<proto::ProtoObject*>(ta);
}

bool isTypedArray(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    const proto::ProtoObject* tag =
        obj->getAttribute(ctx, JSSymbols::taElementType(ctx), false);
    return tag && tag != PROTO_NONE && tag->isInteger(ctx);
}

uint8_t getTypedArrayElementType(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return 0xFF;
    const proto::ProtoObject* tag =
        obj->getAttribute(ctx, JSSymbols::taElementType(ctx), false);
    if (!tag || tag == PROTO_NONE || !tag->isInteger(ctx)) return 0xFF;
    return static_cast<uint8_t>(tag->asLong(ctx));
}

uint32_t getTypedArrayLength(proto::ProtoContext* ctx, const proto::ProtoObject* ta) {
    if (!ta || ta == PROTO_NONE) return 0;
    const proto::ProtoObject* lenObj =
        ta->getAttribute(ctx, JSSymbols::length(ctx), false);
    if (!lenObj || lenObj == PROTO_NONE || !lenObj->isInteger(ctx)) return 0;
    long long v = lenObj->asLong(ctx);
    return v > 0 ? static_cast<uint32_t>(v) : 0;
}

const proto::ProtoObject* createTypedArrayFromLength(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     uint32_t length) {
    return PROTO_NONE;
}

const proto::ProtoObject* createTypedArrayFromBuffer(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     const proto::ProtoObject* ab,
                                                     long long byteOffset,
                                                     long long length) {
    return PROTO_NONE;
}

} // namespace protojs
