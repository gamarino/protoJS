#include "TypedArrayPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

#include <cstring>
#include <cmath>
#include <limits>

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
    ta = ta->setAttribute(ctx, JSSymbols::taByteOffset(ctx), ctx->fromInteger(0LL));
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
    ta = ta->setAttribute(ctx, JSSymbols::taByteOffset(ctx),
                          ctx->fromInteger(byteOffset));
    ta = ta->setAttribute(ctx, JSSymbols::byteLength(ctx),
                          ctx->fromInteger(static_cast<long long>(viewByteLen)));
    ta = ta->setAttribute(ctx, JSSymbols::length(ctx),
                          ctx->fromInteger(static_cast<long long>(len)));
    return ta;
}

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
