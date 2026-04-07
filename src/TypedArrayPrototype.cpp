#include "TypedArrayPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

#include <cstring>
#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>

namespace protojs {

// ---------------------------------------------------------------------------
// Module-level prototype state (initialized once by ensureTypedArrayConstructors)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* s_taBaseProto = nullptr;
static const proto::ProtoObject* s_taProtos[11] = {};

const proto::ProtoObject* getTypedArrayBaseProto() { return s_taBaseProto; }
const proto::ProtoObject* getTypedArrayConcreteProto(uint8_t elemType) {
    return (elemType < 11) ? s_taProtos[elemType] : nullptr;
}

// ---------------------------------------------------------------------------
// typedArrayGetElement
// ---------------------------------------------------------------------------

const proto::ProtoObject* typedArrayGetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               uint8_t elementType) {
    if (!ta || ta == PROTO_NONE) return PROTO_NONE;

    // Get length and bounds check
    const proto::ProtoObject* lenObj = ta->getAttribute(ctx, JSSymbols::length(ctx), false);
    uint32_t length = 0;
    if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx)) {
        long long lv = lenObj->asLong(ctx);
        length = lv > 0 ? static_cast<uint32_t>(lv) : 0;
    }
    if (index >= length) return PROTO_NONE;

    // Get byteOffset
    const proto::ProtoObject* boObj = ta->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    long long byteOffset = 0;
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx))
        byteOffset = boObj->asLong(ctx);

    // Compute byte index — use explicit unsigned long widening to avoid 32-bit overflow
    // for large indices (e.g., index = 0x1FFFFFFF, elemSize = 8).
    uint8_t elemSize = TA_ELEMENT_SIZE[elementType < 11 ? elementType : 0];
    unsigned long byteIndex = static_cast<unsigned long>(byteOffset) +
                              static_cast<unsigned long>(index) * static_cast<unsigned long>(elemSize);

    // Get raw buffer pointer
    const proto::ProtoObject* abObj = ta->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return PROTO_NONE;
    void* rawPtr = getArrayBufferRawPtr(ctx, abObj);
    if (!rawPtr) return PROTO_NONE;

    const uint8_t* bytes = static_cast<const uint8_t*>(rawPtr) + byteIndex;

    switch (elementType) {
        case 0: { // Int8
            int8_t v; std::memcpy(&v, bytes, 1);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 1: // Uint8
        case 2: { // Uint8Clamped
            uint8_t v; std::memcpy(&v, bytes, 1);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 3: { // Int16
            int16_t v; std::memcpy(&v, bytes, 2);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 4: { // Uint16
            uint16_t v; std::memcpy(&v, bytes, 2);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 5: { // Int32
            int32_t v; std::memcpy(&v, bytes, 4);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 6: { // Uint32
            uint32_t v; std::memcpy(&v, bytes, 4);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 7: { // Float32
            float v; std::memcpy(&v, bytes, 4);
            return ctx->fromDouble(static_cast<double>(v));
        }
        case 8: { // Float64
            double v; std::memcpy(&v, bytes, 8);
            return ctx->fromDouble(v);
        }
        case 9: { // BigInt64
            int64_t v; std::memcpy(&v, bytes, 8);
            return ctx->fromInteger(static_cast<long long>(v));
        }
        case 10: { // BigUint64
            uint64_t v; std::memcpy(&v, bytes, 8);
            // NOTE: BigUint64 values > INT64_MAX cannot be represented as long long.
            // Clamping to INT64_MAX until BigInt support is added.
            long long safe = v > static_cast<uint64_t>(std::numeric_limits<long long>::max())
                           ? std::numeric_limits<long long>::max()
                           : static_cast<long long>(v);
            return ctx->fromInteger(safe);
        }
        default:
            return PROTO_NONE;
    }
}

// ---------------------------------------------------------------------------
// typedArraySetElement
// ---------------------------------------------------------------------------

const proto::ProtoObject* typedArraySetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               const proto::ProtoObject* value,
                                               uint8_t elementType) {
    if (!ta || ta == PROTO_NONE) return const_cast<proto::ProtoObject*>(ta);

    // Get length and bounds check
    const proto::ProtoObject* lenObj = ta->getAttribute(ctx, JSSymbols::length(ctx), false);
    uint32_t length = 0;
    if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx)) {
        long long lv = lenObj->asLong(ctx);
        length = lv > 0 ? static_cast<uint32_t>(lv) : 0;
    }
    if (index >= length) return const_cast<proto::ProtoObject*>(ta);

    // Get byteOffset
    const proto::ProtoObject* boObj = ta->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    long long byteOffset = 0;
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx))
        byteOffset = boObj->asLong(ctx);

    // Compute byte index — use explicit unsigned long widening to avoid 32-bit overflow
    // for large indices (e.g., index = 0x1FFFFFFF, elemSize = 8).
    uint8_t elemSize = TA_ELEMENT_SIZE[elementType < 11 ? elementType : 0];
    unsigned long byteIndex = static_cast<unsigned long>(byteOffset) +
                              static_cast<unsigned long>(index) * static_cast<unsigned long>(elemSize);

    // Get raw buffer pointer
    const proto::ProtoObject* abObj = ta->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return const_cast<proto::ProtoObject*>(ta);
    void* rawPtr = getArrayBufferRawPtr(ctx, abObj);
    if (!rawPtr) return const_cast<proto::ProtoObject*>(ta);

    uint8_t* bytes = static_cast<uint8_t*>(rawPtr) + byteIndex;

    // Coerce value to integer or double
    long long iv = 0;
    double dv = 0.0;
    if (value && value != PROTO_NONE) {
        if (value->isInteger(ctx)) {
            iv = value->asLong(ctx);
            dv = static_cast<double>(iv);
        } else if (value->isDouble(ctx) || value->isFloat(ctx)) {
            dv = value->asDouble(ctx);
            if (!std::isnan(dv) && !std::isinf(dv))
                iv = static_cast<long long>(dv);
            else
                iv = 0;
        }
    }

    switch (elementType) {
        case 0: { // Int8
            int8_t v = static_cast<int8_t>(iv & 0xFF);
            std::memcpy(bytes, &v, 1);
            break;
        }
        case 1: { // Uint8
            uint8_t v = static_cast<uint8_t>(iv & 0xFF);
            std::memcpy(bytes, &v, 1);
            break;
        }
        case 2: { // Uint8Clamped — ECMAScript §23.2.1.1.1: clamp then round-half-to-even
            uint8_t v;
            if (std::isnan(dv) || dv <= 0.0) {
                v = 0;
            } else if (dv >= 255.0) {
                v = 255;
            } else {
                // Round half to even (banker's rounding) per ES spec
                double f = std::floor(dv);
                double rounded;
                if (dv - f == 0.5) {
                    // Exactly half-way: choose the even neighbor
                    rounded = (std::fmod(f, 2.0) == 0.0) ? f : f + 1.0;
                } else {
                    rounded = std::round(dv);
                }
                v = static_cast<uint8_t>(rounded);
            }
            std::memcpy(bytes, &v, 1);
            break;
        }
        case 3: { // Int16
            int16_t v = static_cast<int16_t>(iv & 0xFFFF);
            std::memcpy(bytes, &v, 2);
            break;
        }
        case 4: { // Uint16
            uint16_t v = static_cast<uint16_t>(iv & 0xFFFF);
            std::memcpy(bytes, &v, 2);
            break;
        }
        case 5: { // Int32
            int32_t v = static_cast<int32_t>(iv & 0xFFFFFFFF);
            std::memcpy(bytes, &v, 4);
            break;
        }
        case 6: { // Uint32
            uint32_t v = static_cast<uint32_t>(iv & 0xFFFFFFFF);
            std::memcpy(bytes, &v, 4);
            break;
        }
        case 7: { // Float32
            float v = static_cast<float>(dv);
            std::memcpy(bytes, &v, 4);
            break;
        }
        case 8: { // Float64
            double v = dv;
            std::memcpy(bytes, &v, 8);
            break;
        }
        case 9: { // BigInt64
            int64_t v = static_cast<int64_t>(iv);
            std::memcpy(bytes, &v, 8);
            break;
        }
        case 10: { // BigUint64
            uint64_t v = static_cast<uint64_t>(iv);
            std::memcpy(bytes, &v, 8);
            break;
        }
        default:
            break;
    }

    // The array buffer is mutated in place; the typed array object itself is unchanged.
    return const_cast<proto::ProtoObject*>(ta);
}

// ---------------------------------------------------------------------------
// isTypedArray / getTypedArrayElementType / getTypedArrayLength
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// createTypedArrayFromLength
// ---------------------------------------------------------------------------

const proto::ProtoObject* createTypedArrayFromLength(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     uint32_t length) {
    uint8_t elemSize = TA_ELEMENT_SIZE[elemType < 11 ? elemType : 0];
    unsigned long byteLen = static_cast<unsigned long>(length) * static_cast<unsigned long>(elemSize);

    const proto::ProtoObject* ab = createArrayBuffer(ctx, byteLen);
    if (!ab || ab == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* ta = (proto && proto != PROTO_NONE)
        ? proto->newChild(ctx, true)
        : ctx->newObject(true);

    ta = ta->setAttribute(ctx, JSSymbols::taElementType(ctx),
                          ctx->fromInteger(static_cast<long long>(elemType)));
    ta = ta->setAttribute(ctx, JSSymbols::taBuffer(ctx), ab);
    ta = ta->setAttribute(ctx, JSSymbols::buffer(ctx), ab);
    ta = ta->setAttribute(ctx, JSSymbols::taByteOffset(ctx), ctx->fromInteger(0LL));
    ta = ta->setAttribute(ctx, JSSymbols::byteOffset(ctx), ctx->fromInteger(0LL));
    ta = ta->setAttribute(ctx, JSSymbols::byteLength(ctx),
                          ctx->fromInteger(static_cast<long long>(byteLen)));
    ta = ta->setAttribute(ctx, JSSymbols::length(ctx),
                          ctx->fromInteger(static_cast<long long>(length)));
    return ta;
}

// ---------------------------------------------------------------------------
// createTypedArrayFromBuffer
// ---------------------------------------------------------------------------

const proto::ProtoObject* createTypedArrayFromBuffer(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     const proto::ProtoObject* ab,
                                                     long long byteOffset,
                                                     long long length) {
    if (!ab || ab == PROTO_NONE) return PROTO_NONE;

    unsigned long abLen = getArrayBufferByteLength(ctx, ab);
    uint8_t elemSize = TA_ELEMENT_SIZE[elemType < 11 ? elemType : 0];

    if (byteOffset < 0) byteOffset = 0;
    if (byteOffset > static_cast<long long>(abLen))
        byteOffset = static_cast<long long>(abLen);

    long long remaining = static_cast<long long>(abLen) - byteOffset;
    uint32_t len;
    if (length < 0) {
        len = (elemSize > 0) ? static_cast<uint32_t>(remaining / elemSize) : 0;
    } else {
        len = static_cast<uint32_t>(length);
    }
    unsigned long viewByteLen = static_cast<unsigned long>(len) * static_cast<unsigned long>(elemSize);

    const proto::ProtoObject* ta = (proto && proto != PROTO_NONE)
        ? proto->newChild(ctx, true)
        : ctx->newObject(true);

    ta = ta->setAttribute(ctx, JSSymbols::taElementType(ctx),
                          ctx->fromInteger(static_cast<long long>(elemType)));
    ta = ta->setAttribute(ctx, JSSymbols::taBuffer(ctx), ab);
    ta = ta->setAttribute(ctx, JSSymbols::buffer(ctx), ab);
    ta = ta->setAttribute(ctx, JSSymbols::taByteOffset(ctx),
                          ctx->fromInteger(byteOffset));
    ta = ta->setAttribute(ctx, JSSymbols::byteOffset(ctx),
                          ctx->fromInteger(byteOffset));
    ta = ta->setAttribute(ctx, JSSymbols::byteLength(ctx),
                          ctx->fromInteger(static_cast<long long>(viewByteLen)));
    ta = ta->setAttribute(ctx, JSSymbols::length(ctx),
                          ctx->fromInteger(static_cast<long long>(len)));
    return ta;
}

// ---------------------------------------------------------------------------
// %TypedArray%.prototype method implementations (batch 1)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* ta_fill(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return const_cast<proto::ProtoObject*>(self);
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);

    const proto::ProtoObject* fillVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    long long start = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) start = a1->asLong(ctx);
        else if (a1 && a1 != PROTO_NONE && (a1->isDouble(ctx) || a1->isFloat(ctx)))
            start = static_cast<long long>(a1->asDouble(ctx));
    }
    if (args && args->getSize(ctx) > 2) {
        const proto::ProtoObject* a2 = args->getAt(ctx, 2);
        if (a2 && a2 != PROTO_NONE && a2->isInteger(ctx)) end = a2->asLong(ctx);
        else if (a2 && a2 != PROTO_NONE && (a2->isDouble(ctx) || a2->isFloat(ctx)))
            end = static_cast<long long>(a2->asDouble(ctx));
    }

    long long sLen = static_cast<long long>(len);
    if (start < 0) start = std::max(sLen + start, 0LL);
    else start = std::min(start, sLen);
    if (end < 0) end = std::max(sLen + end, 0LL);
    else end = std::min(end, sLen);

    for (long long i = start; i < end; i++)
        typedArraySetElement(ctx, self, static_cast<uint32_t>(i), fillVal, et);

    return const_cast<proto::ProtoObject*>(self);
}

static bool numericEqual(proto::ProtoContext* ctx,
                         const proto::ProtoObject* a, const proto::ProtoObject* b) {
    double da = 0, db = 0;
    bool aOk = false, bOk = false;
    if (a && a->isInteger(ctx)) { da = static_cast<double>(a->asLong(ctx)); aOk = true; }
    else if (a && (a->isDouble(ctx) || a->isFloat(ctx))) { da = a->asDouble(ctx); aOk = true; }
    if (b && b->isInteger(ctx)) { db = static_cast<double>(b->asLong(ctx)); bOk = true; }
    else if (b && (b->isDouble(ctx) || b->isFloat(ctx))) { db = b->asDouble(ctx); bOk = true; }
    return aOk && bOk && da == db;
}

static const proto::ProtoObject* ta_indexOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return ctx->fromInteger(-1LL);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (!args || args->getSize(ctx) == 0) return ctx->fromInteger(-1LL);
    const proto::ProtoObject* search = args->getAt(ctx, 0);
    long long fromIdx = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) fromIdx = a1->asLong(ctx);
        else if (a1 && a1 != PROTO_NONE && (a1->isDouble(ctx) || a1->isFloat(ctx)))
            fromIdx = static_cast<long long>(a1->asDouble(ctx));
    }
    if (fromIdx < 0) fromIdx = std::max(static_cast<long long>(len) + fromIdx, 0LL);
    for (uint32_t i = static_cast<uint32_t>(fromIdx); i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (numericEqual(ctx, elem, search)) return ctx->fromInteger(static_cast<long long>(i));
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* ta_lastIndexOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return ctx->fromInteger(-1LL);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (!len || !args || args->getSize(ctx) == 0) return ctx->fromInteger(-1LL);
    const proto::ProtoObject* search = args->getAt(ctx, 0);
    long long fromIdx = static_cast<long long>(len) - 1;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) fromIdx = a1->asLong(ctx);
        else if (a1 && a1 != PROTO_NONE && (a1->isDouble(ctx) || a1->isFloat(ctx)))
            fromIdx = static_cast<long long>(a1->asDouble(ctx));
    }
    if (fromIdx < 0) fromIdx = static_cast<long long>(len) + fromIdx;
    if (fromIdx >= static_cast<long long>(len)) fromIdx = static_cast<long long>(len) - 1;
    if (fromIdx < 0) return ctx->fromInteger(-1LL);
    for (long long i = fromIdx; i >= 0; i--) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, static_cast<uint32_t>(i), et);
        if (numericEqual(ctx, elem, search)) return ctx->fromInteger(i);
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* ta_includes(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_FALSE;
    uint32_t len = getTypedArrayLength(ctx, self);
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* search = args->getAt(ctx, 0);
    double searchVal = 0;
    bool searchNaN = false;
    if (search && search->isInteger(ctx)) searchVal = static_cast<double>(search->asLong(ctx));
    else if (search && (search->isDouble(ctx) || search->isFloat(ctx))) {
        searchVal = search->asDouble(ctx);
        searchNaN = std::isnan(searchVal);
    }
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (!elem || elem == PROTO_NONE) continue;
        double ev = 0; bool evNaN = false;
        if (elem->isInteger(ctx)) ev = static_cast<double>(elem->asLong(ctx));
        else if (elem->isDouble(ctx) || elem->isFloat(ctx)) { ev = elem->asDouble(ctx); evNaN = std::isnan(ev); }
        if (searchNaN && evNaN) return PROTO_TRUE;
        if (!searchNaN && !evNaN && ev == searchVal) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* ta_join(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return ctx->fromUTF8String("");
    uint32_t len = getTypedArrayLength(ctx, self);
    std::string sep = ",";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE && a0->isString(ctx)) {
            sep.clear();
            a0->asString(ctx)->toUTF8String(ctx, sep);
        }
    }
    std::string result;
    for (uint32_t i = 0; i < len; i++) {
        if (i > 0) result += sep;
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (elem && elem != PROTO_NONE) {
            if (elem->isInteger(ctx)) result += std::to_string(elem->asLong(ctx));
            else if (elem->isDouble(ctx) || elem->isFloat(ctx)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", elem->asDouble(ctx));
                result += buf;
            }
        }
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* ta_reverse(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len < 2) return const_cast<proto::ProtoObject*>(self);
    for (uint32_t i = 0, j = len - 1; i < j; i++, j--) {
        const proto::ProtoObject* a = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoObject* b = typedArrayGetElement(ctx, self, j, et);
        typedArraySetElement(ctx, self, i, b, et);
        typedArraySetElement(ctx, self, j, a, et);
    }
    return const_cast<proto::ProtoObject*>(self);
}

static const proto::ProtoObject* ta_at(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_NONE;
    uint32_t len = getTypedArrayLength(ctx, self);
    long long idx = 0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) idx = a0->asLong(ctx);
        else if (a0 && (a0->isDouble(ctx) || a0->isFloat(ctx)))
            idx = static_cast<long long>(a0->asDouble(ctx));
    }
    if (idx < 0) idx = static_cast<long long>(len) + idx;
    if (idx < 0 || idx >= static_cast<long long>(len)) return PROTO_NONE;
    return typedArrayGetElement(ctx, self, static_cast<uint32_t>(idx), et);
}

static const proto::ProtoObject* ta_subarray(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_NONE;
    uint32_t len = getTypedArrayLength(ctx, self);
    const proto::ProtoObject* abObj =
        self->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    long long selfBO = 0;
    const proto::ProtoObject* boObj =
        self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx)) selfBO = boObj->asLong(ctx);

    long long begin = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) begin = a0->asLong(ctx);
        else if (a0 && (a0->isDouble(ctx) || a0->isFloat(ctx)))
            begin = static_cast<long long>(a0->asDouble(ctx));
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1->isInteger(ctx)) end = a1->asLong(ctx);
        else if (a1 && (a1->isDouble(ctx) || a1->isFloat(ctx)))
            end = static_cast<long long>(a1->asDouble(ctx));
    }
    long long sLen = static_cast<long long>(len);
    if (begin < 0) begin = std::max(sLen + begin, 0LL);
    else begin = std::min(begin, sLen);
    if (end < 0) end = std::max(sLen + end, 0LL);
    else end = std::min(end, sLen);
    long long newLen = std::max(end - begin, 0LL);

    uint8_t elemSize = TA_ELEMENT_SIZE[et < 11 ? et : 0];
    long long newByteOffset = selfBO + begin * static_cast<long long>(elemSize);
    const proto::ProtoObject* proto = (et < 11) ? s_taProtos[et] : nullptr;
    return createTypedArrayFromBuffer(ctx, proto, et, abObj, newByteOffset, newLen);
}

static const proto::ProtoObject* ta_copyWithin(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);

    long long target = 0, start = 0, end = static_cast<long long>(len);
    auto getArgLL = [&](int pos) -> long long {
        if (!args || args->getSize(ctx) <= static_cast<size_t>(pos)) return 0;
        const proto::ProtoObject* a = args->getAt(ctx, pos);
        if (!a || a == PROTO_NONE) return 0;
        if (a->isInteger(ctx)) return a->asLong(ctx);
        if (a->isDouble(ctx) || a->isFloat(ctx)) return static_cast<long long>(a->asDouble(ctx));
        return 0;
    };
    target = getArgLL(0);
    start  = getArgLL(1);
    if (args && args->getSize(ctx) > 2) {
        const proto::ProtoObject* endArg = args->getAt(ctx, 2);
        if (endArg && endArg != PROTO_NONE) end = getArgLL(2);
    }

    long long sLen = static_cast<long long>(len);
    auto clamp = [&](long long v) { return v < 0 ? std::max(sLen + v, 0LL) : std::min(v, sLen); };
    target = clamp(target); start = clamp(start); end = clamp(end);

    long long count = std::min(end - start, sLen - target);
    if (count <= 0) return const_cast<proto::ProtoObject*>(self);

    uint8_t elemSize = TA_ELEMENT_SIZE[et < 11 ? et : 0];
    const proto::ProtoObject* abObj = self->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return const_cast<proto::ProtoObject*>(self);
    long long selfBO = 0;
    const proto::ProtoObject* boObj = self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx)) selfBO = boObj->asLong(ctx);

    void* raw = getArrayBufferRawPtr(ctx, abObj);
    if (!raw) return const_cast<proto::ProtoObject*>(self);

    uint8_t* base = static_cast<uint8_t*>(raw) + selfBO;
    memmove(base + static_cast<size_t>(target) * static_cast<size_t>(elemSize),
            base + static_cast<size_t>(start)  * static_cast<size_t>(elemSize),
            static_cast<size_t>(count) * static_cast<size_t>(elemSize));
    return const_cast<proto::ProtoObject*>(self);
}

// ---------------------------------------------------------------------------
// Task 5: Callback-based TypedArray prototype methods
// ---------------------------------------------------------------------------

// Helper: invoke a native (ProtoMethod) callback with (elem, index, array) arguments.
// JS bytecode callbacks are not supported here; they return PROTO_NONE.
static const proto::ProtoObject* invokeCallback(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* elem,
    uint32_t idx,
    const proto::ProtoObject* arr)
{
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    if (!fn->isMethod(ctx)) return PROTO_NONE;
    const proto::ProtoList* cargs = ctx->newList();
    cargs = cargs->appendLast(ctx, elem);
    cargs = cargs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(idx)));
    cargs = cargs->appendLast(ctx, arr);
    proto::ProtoMethod m = fn->asMethod(ctx);
    return m ? m(ctx, const_cast<proto::ProtoObject*>(fn), nullptr, cargs, nullptr) : PROTO_NONE;
}

static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* r) {
    if (!r || r == PROTO_NONE || r == PROTO_FALSE) return false;
    if (r->isInteger(ctx) && r->asLong(ctx) == 0) return false;
    if ((r->isDouble(ctx) || r->isFloat(ctx)) && r->asDouble(ctx) == 0.0) return false;
    return true;
}

static const proto::ProtoObject* ta_forEach(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++)
        invokeCallback(ctx, fn, typedArrayGetElement(ctx, self, i, et), i, self);
    return PROTO_NONE;
}

static const proto::ProtoObject* ta_every(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_TRUE;
    if (!args || args->getSize(ctx) == 0) return PROTO_TRUE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;  // No callable: TypeError per ES spec
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* r = invokeCallback(ctx, fn, typedArrayGetElement(ctx, self, i, et), i, self);
        if (!isTruthy(ctx, r)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* ta_some(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* r = invokeCallback(ctx, fn, typedArrayGetElement(ctx, self, i, et), i, self);
        if (isTruthy(ctx, r)) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* ta_find(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (isTruthy(ctx, invokeCallback(ctx, fn, elem, i, self))) return elem;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* ta_findIndex(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return ctx->fromInteger(-1LL);
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (isTruthy(ctx, invokeCallback(ctx, fn, elem, i, self)))
            return ctx->fromInteger(static_cast<long long>(i));
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* ta_map(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    const proto::ProtoObject* proto = (et < 11) ? s_taProtos[et] : nullptr;
    const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, len);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoObject* mapped = invokeCallback(ctx, fn, elem, i, self);
        if (mapped && mapped != PROTO_NONE)
            typedArraySetElement(ctx, result, i, mapped, et);
    }
    return result;
}

static const proto::ProtoObject* ta_reduce(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len == 0) {
        return (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    }
    const proto::ProtoObject* acc;
    uint32_t start = 0;
    if (args->getSize(ctx) > 1) {
        acc = args->getAt(ctx, 1);
    } else {
        acc = typedArrayGetElement(ctx, self, 0, et);
        start = 1;
    }
    if (!fn || !fn->isMethod(ctx)) return acc;
    for (uint32_t i = start; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoList* cargs = ctx->newList();
        cargs = cargs->appendLast(ctx, acc);
        cargs = cargs->appendLast(ctx, elem);
        cargs = cargs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(i)));
        cargs = cargs->appendLast(ctx, self);
        proto::ProtoMethod m = fn->asMethod(ctx);
        const proto::ProtoObject* r = m ? m(ctx, PROTO_NONE, nullptr, cargs, nullptr) : PROTO_NONE;
        acc = r ? r : PROTO_NONE;
    }
    return acc;
}

static const proto::ProtoObject* ta_reduceRight(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len == 0) {
        return (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    }
    const proto::ProtoObject* acc;
    long long start;
    if (args->getSize(ctx) > 1) {
        acc = args->getAt(ctx, 1);
        start = static_cast<long long>(len) - 1;
    } else {
        acc = typedArrayGetElement(ctx, self, len - 1, et);
        start = static_cast<long long>(len) - 2;
    }
    if (!fn || !fn->isMethod(ctx)) return acc;
    for (long long i = start; i >= 0; i--) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, static_cast<uint32_t>(i), et);
        const proto::ProtoList* cargs = ctx->newList();
        cargs = cargs->appendLast(ctx, acc);
        cargs = cargs->appendLast(ctx, elem);
        cargs = cargs->appendLast(ctx, ctx->fromInteger(i));
        cargs = cargs->appendLast(ctx, self);
        proto::ProtoMethod m = fn->asMethod(ctx);
        const proto::ProtoObject* r = m ? m(ctx, PROTO_NONE, nullptr, cargs, nullptr) : PROTO_NONE;
        acc = r ? r : PROTO_NONE;
    }
    return acc;
}

static const proto::ProtoObject* ta_sort(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len <= 1) return const_cast<proto::ProtoObject*>(self);

    // Extract all elements as doubles for sorting.
    std::vector<double> vals(len);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (elem && elem != PROTO_NONE) {
            if (elem->isInteger(ctx)) vals[i] = static_cast<double>(elem->asLong(ctx));
            else if (elem->isDouble(ctx) || elem->isFloat(ctx)) vals[i] = elem->asDouble(ctx);
        }
    }

    // Default numeric sort (NaN goes to end per spec).
    std::sort(vals.begin(), vals.end(), [](double a, double b) {
        if (std::isnan(a)) return false;
        if (std::isnan(b)) return true;
        return a < b;
    });

    // Write back.
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* v;
        if (std::isnan(vals[i])) {
            v = ctx->fromDouble(vals[i]);
        } else if (et != 7 && et != 8 &&
                   vals[i] == std::floor(vals[i]) &&
                   vals[i] >= static_cast<double>(LLONG_MIN) &&
                   vals[i] <= static_cast<double>(LLONG_MAX)) {
            v = ctx->fromInteger(static_cast<long long>(vals[i]));
        } else {
            v = ctx->fromDouble(vals[i]);
        }
        typedArraySetElement(ctx, self, i, v, et);
    }
    return const_cast<proto::ProtoObject*>(self);
}

static const proto::ProtoObject* ta_set(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* src = args->getAt(ctx, 0);
    if (!src || src == PROTO_NONE) return PROTO_NONE;

    long long offset = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) offset = a1->asLong(ctx);
        else if (a1 && a1 != PROTO_NONE && (a1->isDouble(ctx) || a1->isFloat(ctx)))
            offset = static_cast<long long>(a1->asDouble(ctx));
    }
    if (offset < 0) return PROTO_NONE;  // negative offset: RangeError per ES spec

    uint32_t selfLen = getTypedArrayLength(ctx, self);
    if (isTypedArray(ctx, src)) {
        uint8_t srcEt = getTypedArrayElementType(ctx, src);
        uint32_t srcLen = getTypedArrayLength(ctx, src);
        for (uint32_t i = 0; i < srcLen; i++) {
            long long dstIdx = static_cast<long long>(i) + offset;
            if (dstIdx >= static_cast<long long>(selfLen)) break;
            const proto::ProtoObject* elem = typedArrayGetElement(ctx, src, i, srcEt);
            typedArraySetElement(ctx, self, static_cast<uint32_t>(dstIdx), elem, et);
        }
    } else {
        // Array-like source using internal index keys.
        const proto::ProtoObject* lenObj = src->getAttribute(ctx, JSSymbols::length(ctx), true);
        uint32_t srcLen = 0;
        if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx))
            srcLen = static_cast<uint32_t>(std::max(0LL, lenObj->asLong(ctx)));
        for (uint32_t i = 0; i < srcLen; i++) {
            long long dstIdx = static_cast<long long>(i) + offset;
            if (dstIdx >= static_cast<long long>(selfLen)) break;
            const proto::ProtoString* idxKey = JSSymbols::indexKey(ctx, i);
            const proto::ProtoObject* elem = src->getAttribute(ctx, idxKey, false);
            if (elem && elem != PROTO_NONE)
                typedArraySetElement(ctx, self, static_cast<uint32_t>(dstIdx), elem, et);
        }
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Task 6: property getters (buffer, byteOffset, byteLength) + slice
// ---------------------------------------------------------------------------

static const proto::ProtoObject* ta_get_buffer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* ab = self->getAttribute(ctx, JSSymbols::buffer(ctx), false);
    return (ab && ab != PROTO_NONE) ? ab : PROTO_NONE;
}

static const proto::ProtoObject* ta_get_byteOffset(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    // Try the internal key first, fall back to the public key.
    const proto::ProtoObject* bo = self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (bo && bo != PROTO_NONE && bo->isInteger(ctx)) return bo;
    bo = self->getAttribute(ctx, JSSymbols::byteOffset(ctx), false);
    if (bo && bo != PROTO_NONE && bo->isInteger(ctx)) return bo;
    return ctx->fromInteger(0LL);
}

static const proto::ProtoObject* ta_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bl = self->getAttribute(ctx, JSSymbols::byteLength(ctx), false);
    return (bl && bl != PROTO_NONE) ? bl : ctx->fromInteger(0LL);
}

static const proto::ProtoObject* ta_slice(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_NONE;
    uint32_t len = getTypedArrayLength(ctx, self);
    long long start = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE && a0->isInteger(ctx)) start = a0->asLong(ctx);
        else if (a0 && a0 != PROTO_NONE && (a0->isDouble(ctx) || a0->isFloat(ctx)))
            start = static_cast<long long>(a0->asDouble(ctx));
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) end = a1->asLong(ctx);
        else if (a1 && a1 != PROTO_NONE && (a1->isDouble(ctx) || a1->isFloat(ctx)))
            end = static_cast<long long>(a1->asDouble(ctx));
    }
    long long sLen = static_cast<long long>(len);
    if (start < 0) start = std::max(sLen + start, 0LL);
    else start = std::min(start, sLen);
    if (end < 0) end = std::max(sLen + end, 0LL);
    else end = std::min(end, sLen);
    long long newLen = std::max(end - start, 0LL);

    const proto::ProtoObject* proto = (et < 11) ? s_taProtos[et] : nullptr;
    const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, static_cast<uint32_t>(newLen));
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    for (long long i = start; i < end; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, static_cast<uint32_t>(i), et);
        typedArraySetElement(ctx, result, static_cast<uint32_t>(i - start), elem, et);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Task 6: static methods — TypedArray.of() and TypedArray.from()
// ---------------------------------------------------------------------------

struct TAStaticMethods {
    static const proto::ProtoObject* makeOf(
        proto::ProtoContext* ctx, uint8_t et,
        const proto::ProtoObject* proto, const proto::ProtoList* args)
    {
        uint32_t len = args ? static_cast<uint32_t>(args->getSize(ctx)) : 0;
        const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, len);
        if (!result || result == PROTO_NONE) return PROTO_NONE;
        for (uint32_t i = 0; i < len; i++) {
            const proto::ProtoObject* v = args->getAt(ctx, static_cast<int>(i));
            typedArraySetElement(ctx, result, i, v, et);
        }
        return result;
    }

    static const proto::ProtoObject* makeFrom(
        proto::ProtoContext* ctx, uint8_t et,
        const proto::ProtoObject* proto, const proto::ProtoList* args)
    {
        if (!args || args->getSize(ctx) == 0) return createTypedArrayFromLength(ctx, proto, et, 0);
        const proto::ProtoObject* src = args->getAt(ctx, 0);
        if (!src || src == PROTO_NONE) return createTypedArrayFromLength(ctx, proto, et, 0);

        if (isTypedArray(ctx, src)) {
            uint8_t srcEt = getTypedArrayElementType(ctx, src);
            uint32_t srcLen = getTypedArrayLength(ctx, src);
            const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, srcLen);
            if (!result || result == PROTO_NONE) return PROTO_NONE;
            for (uint32_t i = 0; i < srcLen; i++) {
                const proto::ProtoObject* elem = typedArrayGetElement(ctx, src, i, srcEt);
                typedArraySetElement(ctx, result, i, elem, et);
            }
            return result;
        }

        // Array-like: get .length and indexed elements.
        const proto::ProtoObject* lenObj = src->getAttribute(ctx, JSSymbols::length(ctx), true);
        uint32_t srcLen = 0;
        if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx))
            srcLen = static_cast<uint32_t>(std::max(0LL, lenObj->asLong(ctx)));

        const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, srcLen);
        if (!result || result == PROTO_NONE) return PROTO_NONE;
        for (uint32_t i = 0; i < srcLen; i++) {
            const proto::ProtoString* idxKey = JSSymbols::indexKey(ctx, i);
            const proto::ProtoObject* elem = src->getAttribute(ctx, idxKey, false);
            if (elem && elem != PROTO_NONE)
                typedArraySetElement(ctx, result, i, elem, et);
        }
        return result;
    }
};

// Define 11 separate native functions for `of` and `from`, one per element type.
// C++ native callbacks cannot capture state so we generate them via macro.
#define DEFINE_TA_STATIC(idx) \
static const proto::ProtoObject* ta_of_##idx( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, \
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) { \
    return TAStaticMethods::makeOf(ctx, idx, s_taProtos[idx], args); \
} \
static const proto::ProtoObject* ta_from_##idx( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, \
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) { \
    return TAStaticMethods::makeFrom(ctx, idx, s_taProtos[idx], args); \
}

// Index-to-type mapping matches TA_CONFIGS: 0=Int8, 1=Uint8, 2=Uint8Clamped, 3=Int16,
// 4=Uint16, 5=Int32, 6=Uint32, 7=Float32, 8=Float64, 9=BigInt64, 10=BigUint64.
// s_taProtos[idx] is guaranteed non-null at call time because these functions are only
// exposed as constructor attributes after ensureTypedArrayConstructors completes.
DEFINE_TA_STATIC(0)  DEFINE_TA_STATIC(1)  DEFINE_TA_STATIC(2)
DEFINE_TA_STATIC(3)  DEFINE_TA_STATIC(4)  DEFINE_TA_STATIC(5)
DEFINE_TA_STATIC(6)  DEFINE_TA_STATIC(7)  DEFINE_TA_STATIC(8)
DEFINE_TA_STATIC(9)  DEFINE_TA_STATIC(10)
#undef DEFINE_TA_STATIC

using TAStaticMethod = const proto::ProtoObject*(*)(proto::ProtoContext*,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*);

static const TAStaticMethod TA_OF_METHODS[11] = {
    ta_of_0, ta_of_1, ta_of_2, ta_of_3, ta_of_4,
    ta_of_5, ta_of_6, ta_of_7, ta_of_8, ta_of_9, ta_of_10
};
static const TAStaticMethod TA_FROM_METHODS[11] = {
    ta_from_0, ta_from_1, ta_from_2, ta_from_3, ta_from_4,
    ta_from_5, ta_from_6, ta_from_7, ta_from_8, ta_from_9, ta_from_10
};

// ---------------------------------------------------------------------------
// TypedArray iterator support (Task 7)
// ---------------------------------------------------------------------------

/**
 * Iterator next() method for TypedArray iterators.
 *
 * The iterator object stores three internal keys:
 *   __iter_arr__  — reference to the TypedArray being iterated
 *   __iter_idx__  — current index (integer, advances on each call)
 *   __iter_kind__ — 0 = values, 1 = keys, 2 = entries
 *
 * Returns {value, done} objects per the iterator protocol.
 */
static const proto::ProtoObject* taIterNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    // Helper to build a {value, done} result object.
    auto makeResult = [&](const proto::ProtoObject* val,
                          const proto::ProtoObject* doneFlag) -> const proto::ProtoObject* {
        const proto::ProtoObject* r = ctx->newObject(true);
        const proto::ProtoString* vk = JSSymbols::value(ctx);
        const proto::ProtoString* dk = JSSymbols::done(ctx);
        if (vk) r = r->setAttribute(ctx, vk, val ? val : PROTO_NONE);
        if (dk) r = r->setAttribute(ctx, dk, doneFlag);
        return r;
    };

    if (!self || self == PROTO_NONE)
        return makeResult(PROTO_NONE, PROTO_TRUE);

    const proto::ProtoObject* taObj =
        self->getAttribute(ctx, JSSymbols::iterArr(ctx), false);
    const proto::ProtoObject* idxObj =
        self->getAttribute(ctx, JSSymbols::iterIdx(ctx), false);
    const proto::ProtoObject* kindObj =
        self->getAttribute(ctx, JSSymbols::iterKind(ctx), false);

    if (!taObj || taObj == PROTO_NONE)
        return makeResult(PROTO_NONE, PROTO_TRUE);

    uint32_t idx = (idxObj && idxObj != PROTO_NONE && idxObj->isInteger(ctx))
        ? static_cast<uint32_t>(idxObj->asLong(ctx)) : 0u;
    uint32_t len = getTypedArrayLength(ctx, taObj);
    uint8_t  et  = getTypedArrayElementType(ctx, taObj);

    if (idx >= len)
        return makeResult(PROTO_NONE, PROTO_TRUE);

    // Advance the index stored on the (mutable) iterator object.
    const_cast<proto::ProtoObject*>(self)->setAttribute(
        ctx, JSSymbols::iterIdx(ctx),
        ctx->fromInteger(static_cast<long long>(idx + 1)));

    long long kind = (kindObj && kindObj != PROTO_NONE && kindObj->isInteger(ctx))
        ? kindObj->asLong(ctx) : 0LL;

    const proto::ProtoObject* iterValue;
    if (kind == 1) {
        // keys(): yield the numeric index.
        iterValue = ctx->fromInteger(static_cast<long long>(idx));
    } else if (kind == 2) {
        // entries(): yield an array-like pair [index, element].
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, taObj, idx, et);
        const proto::ProtoObject* pair = ctx->newObject(true);
        const proto::ProtoString* k0 = JSSymbols::indexKey(ctx, 0);
        const proto::ProtoString* k1 = JSSymbols::indexKey(ctx, 1);
        const proto::ProtoString* lk = JSSymbols::length(ctx);
        if (k0) pair = pair->setAttribute(ctx, k0, ctx->fromInteger(static_cast<long long>(idx)));
        if (k1) pair = pair->setAttribute(ctx, k1, elem ? elem : PROTO_NONE);
        if (lk) pair = pair->setAttribute(ctx, lk,  ctx->fromInteger(2LL));
        iterValue = pair;
    } else {
        // values() (kind == 0): yield the element value.
        iterValue = typedArrayGetElement(ctx, taObj, idx, et);
    }

    return makeResult(iterValue ? iterValue : PROTO_NONE, PROTO_FALSE);
}

/**
 * Named static used as Symbol.iterator on the iterator itself so that
 * iterator objects are also iterable (for...of protocol requirement).
 */
static const proto::ProtoObject* taIterSelf(
    proto::ProtoContext*, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    return self;
}

/** Create a TypedArray iterator object for the given array and kind. */
static const proto::ProtoObject* makeTAIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* ta, long long kind)
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    const proto::ProtoString* arrKey  = JSSymbols::iterArr(ctx);
    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);

    if (arrKey)     iter = iter->setAttribute(ctx, arrKey,  ta ? ta : PROTO_NONE);
    if (idxKey)     iter = iter->setAttribute(ctx, idxKey,  ctx->fromInteger(0LL));
    if (kindKey)    iter = iter->setAttribute(ctx, kindKey, ctx->fromInteger(kind));
    if (nextKey) {
        const proto::ProtoObject* nextFn = ctx->fromMethod(nullptr, taIterNext);
        if (nextFn) iter = iter->setAttribute(ctx, nextKey, nextFn);
    }
    // Make the iterator itself iterable (Symbol.iterator returns self).
    if (symIterKey) {
        const proto::ProtoObject* selfFn = ctx->fromMethod(nullptr, taIterSelf);
        if (selfFn) iter = iter->setAttribute(ctx, symIterKey, selfFn);
    }
    return iter;
}

/** %TypedArray%.prototype.values() — iterator over element values. */
static const proto::ProtoObject* ta_values(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeTAIterator(ctx, self, 0); }

/** %TypedArray%.prototype.keys() — iterator over element indices. */
static const proto::ProtoObject* ta_keys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeTAIterator(ctx, self, 1); }

/** %TypedArray%.prototype.entries() — iterator over [index, value] pairs. */
static const proto::ProtoObject* ta_entries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeTAIterator(ctx, self, 2); }

// ---------------------------------------------------------------------------
// ensureTypedArrayConstructors
// ---------------------------------------------------------------------------

struct TACtorConfig {
    uint8_t elemType;
    const char* name;
    uint8_t elemSize;
};

static const TACtorConfig TA_CONFIGS[11] = {
    { 0, "Int8Array",          1 },
    { 1, "Uint8Array",         1 },
    { 2, "Uint8ClampedArray",  1 },
    { 3, "Int16Array",         2 },
    { 4, "Uint16Array",        2 },
    { 5, "Int32Array",         4 },
    { 6, "Uint32Array",        4 },
    { 7, "Float32Array",       4 },
    { 8, "Float64Array",       8 },
    { 9, "BigInt64Array",      8 },
    {10, "BigUint64Array",     8 },
};

void ensureTypedArrayConstructors(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    // Idempotency check
    const proto::ProtoString* int8Key = JSSymbols::Int8Array(ctx);
    if (int8Key) {
        const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, int8Key, false);
        if (existing && existing != PROTO_NONE) return;
    }

    const proto::ProtoObject* root = *globalRoot;

    // Build empty base prototype inheriting from objectPrototype
    const proto::ProtoObject* objProto =
        (ctx->space) ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* baseProto = (objProto && objProto != PROTO_NONE)
        ? objProto->newChild(ctx, false)
        : ctx->newObject(false);
    // Register %TypedArray%.prototype methods (batch 1)
    baseProto = baseProto->setAttribute(ctx, JSSymbols::fill(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_fill));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::indexOf(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_indexOf));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::lastIndexOf(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_lastIndexOf));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::includes(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_includes));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::join(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_join));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::reverse(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_reverse));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::at(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_at));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::subarray(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_subarray));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::copyWithin(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_copyWithin));
    // Register %TypedArray%.prototype methods (batch 2 — callback-based)
    baseProto = baseProto->setAttribute(ctx, JSSymbols::forEach(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_forEach));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::every(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_every));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::some(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_some));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::find(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_find));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::findIndex(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_findIndex));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::map(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_map));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::reduce(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_reduce));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::reduceRight(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_reduceRight));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::sort(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_sort));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::set(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_set));
    // Register Task 6 prototype additions: property getters + slice
    baseProto = baseProto->setAttribute(ctx, JSSymbols::buffer(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_get_buffer));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::byteOffset(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_get_byteOffset));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::byteLength(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_get_byteLength));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::slice(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_slice));
    // Register Task 7 iterator protocol methods.
    baseProto = baseProto->setAttribute(ctx, JSSymbols::keys(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_keys));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::values(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_values));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::entries(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_entries));
    baseProto = baseProto->setAttribute(ctx, JSSymbols::symbolIterator(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_values));
    s_taBaseProto = baseProto;

    // Register each concrete typed array constructor
    for (int i = 0; i < 11; i++) {
        const TACtorConfig& cfg = TA_CONFIGS[i];

        // Concrete prototype inherits from base proto (immutable)
        const proto::ProtoObject* concreteProto = s_taBaseProto->newChild(ctx, false);
        concreteProto = concreteProto->setAttribute(
            ctx,
            JSSymbols::BYTES_PER_ELEMENT(ctx),
            ctx->fromInteger(static_cast<long long>(cfg.elemSize)));
        s_taProtos[i] = concreteProto;

        // Constructor object (immutable — holds metadata only)
        const proto::ProtoObject* ctor = ctx->newObject(false);
        ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), concreteProto);
        {
            const proto::ProtoString* bpeKey = JSSymbols::BYTES_PER_ELEMENT(ctx);
            if (bpeKey)
                ctor = ctor->setAttribute(ctx, bpeKey,
                    ctx->fromInteger(static_cast<long long>(cfg.elemSize)));
        }
        {
            const proto::ProtoObject* nameStrObj = ctx->fromUTF8String("name");
            const proto::ProtoString* nameKey = nameStrObj ? nameStrObj->asString(ctx) : nullptr;
            if (nameKey)
                ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String(cfg.name));
        }
        // Tag integer elemType so OP_call_constructor can dispatch
        ctor = ctor->setAttribute(ctx, JSSymbols::taCtor(ctx),
            ctx->fromInteger(static_cast<long long>(cfg.elemType)));

        // Register Task 6 static methods: TypedArray.of() and TypedArray.from()
        ctor = ctor->setAttribute(ctx, JSSymbols::of(ctx),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(ctor), TA_OF_METHODS[i]));
        ctor = ctor->setAttribute(ctx, JSSymbols::from(ctx),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(ctor), TA_FROM_METHODS[i]));

        // Register constructor on global root using the appropriate JSSymbols key
        const proto::ProtoString* globalKey = nullptr;
        switch (i) {
            case  0: globalKey = JSSymbols::Int8Array(ctx);         break;
            case  1: globalKey = JSSymbols::Uint8Array(ctx);        break;
            case  2: globalKey = JSSymbols::Uint8ClampedArray(ctx); break;
            case  3: globalKey = JSSymbols::Int16Array(ctx);        break;
            case  4: globalKey = JSSymbols::Uint16Array(ctx);       break;
            case  5: globalKey = JSSymbols::Int32Array(ctx);        break;
            case  6: globalKey = JSSymbols::Uint32Array(ctx);       break;
            case  7: globalKey = JSSymbols::Float32Array(ctx);      break;
            case  8: globalKey = JSSymbols::Float64Array(ctx);      break;
            case  9: globalKey = JSSymbols::BigInt64Array(ctx);     break;
            case 10: globalKey = JSSymbols::BigUint64Array(ctx);    break;
            default: break;
        }
        if (globalKey)
            root = root->setAttribute(ctx, globalKey, ctor);
    }

    *globalRoot = root;
}

} // namespace protojs
