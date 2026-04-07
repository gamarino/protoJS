#ifndef PROTOJS_TYPEDARRAYPROTOTYPE_H
#define PROTOJS_TYPEDARRAYPROTOTYPE_H

#include "headers/protoCore.h"
#include <cstdint>

namespace protojs {

enum class TAElementType : uint8_t {
    Int8        = 0,
    Uint8       = 1,
    Uint8Clamped= 2,
    Int16       = 3,
    Uint16      = 4,
    Int32       = 5,
    Uint32      = 6,
    Float32     = 7,
    Float64     = 8,
    BigInt64    = 9,
    BigUint64   = 10,
};

constexpr uint8_t TA_ELEMENT_SIZE[11] = {1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8};

void ensureTypedArrayConstructors(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot);

const proto::ProtoObject* typedArrayGetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               uint8_t elementType);

const proto::ProtoObject* typedArraySetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               const proto::ProtoObject* value,
                                               uint8_t elementType);

bool isTypedArray(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

uint8_t getTypedArrayElementType(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

uint32_t getTypedArrayLength(proto::ProtoContext* ctx, const proto::ProtoObject* ta);

const proto::ProtoObject* createTypedArrayFromLength(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     uint32_t length);

const proto::ProtoObject* createTypedArrayFromBuffer(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     const proto::ProtoObject* ab,
                                                     long long byteOffset,
                                                     long long length);

} // namespace protojs

#endif // PROTOJS_TYPEDARRAYPROTOTYPE_H
