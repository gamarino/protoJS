#ifndef PROTOJS_TYPEDARRAYPROTOTYPE_H
#define PROTOJS_TYPEDARRAYPROTOTYPE_H

#include "protoCore.h"
#include <cstdint>
#include <string>

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

/**
 * new <TypedArray>(...args) (ECMA-262 §23.2.5.1): a length, an ArrayBuffer
 * view (buffer[, byteOffset[, length]]), a copy of another typed array, or
 * the values of an iterable or array-like object.  Returns PROTO_NONE with
 * the exception signalled (hasCallException) on a RangeError or when
 * iterating the source throws.
 */
const proto::ProtoObject* constructTypedArray(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* proto,
                                              uint8_t elemType,
                                              const proto::ProtoList* args);

/**
 * ToIndex (ECMA-262 §7.1.22): `out` is the integer index `v` denotes
 * (undefined is 0; strings, booleans, null and objects convert through
 * ToNumber).  Returns false with the exception signalled -- the TypeError of
 * ToNumber (a Symbol or BigInt), or a RangeError carrying `rangeMessage` for a
 * negative value or one above 2^53 - 1.
 */
bool toIndex(proto::ProtoContext* ctx, const proto::ProtoObject* v, long long& out,
             const char* rangeMessage);

/**
 * [[Construct]] of a typed-array or ArrayBuffer constructor `ctor` (one that
 * carries the __typed_array_ctor__ marker) with NewTarget's prototype `proto`:
 * sets `handled` and returns the new object (PROTO_NONE with the exception
 * signalled on failure); leaves `handled` false for any other constructor.
 */
const proto::ProtoObject* constructTypedArrayOrBuffer(proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* ctor,
                                                      const proto::ProtoObject* proto,
                                                      const proto::ProtoList* args,
                                                      bool& handled);

/** True for a typed array instance (one with its own buffer slot). */
bool isTypedArrayInstance(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/**
 * The internal fields a typed array carries as own attributes (buffer,
 * byteLength, byteOffset, length).  They are accessors of
 * %TypedArray%.prototype, not own properties, so the own-key operations
 * hide them.
 */
bool isTypedArrayInternalField(const std::string& key);

/** True when `key` is a canonical index below the typed array's length. */
bool typedArrayOwnIndex(proto::ProtoContext* ctx, const proto::ProtoObject* ta,
                        const std::string& key, uint32_t& index);

const proto::ProtoObject* getTypedArrayBaseProto();
const proto::ProtoObject* getTypedArrayConcreteProto(uint8_t elemType);

} // namespace protojs

#endif // PROTOJS_TYPEDARRAYPROTOTYPE_H
