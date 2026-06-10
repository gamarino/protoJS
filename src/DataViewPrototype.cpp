#include "DataViewPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include <cstring>
#include <cstdint>

namespace protojs {

// ---- Helper: get raw byte pointer adjusted for DataView offset --------
static uint8_t* dvGetRaw(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                          long long byteOffset, uint32_t accessSize)
{
    if (!self || self == PROTO_NONE) return nullptr;
    const proto::ProtoObject* abObj =
        self->getAttribute(ctx, JSSymbols::dvBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return nullptr;

    long long viewOffset = 0;
    const proto::ProtoObject* voObj = self->getAttribute(ctx, JSSymbols::dvByteOffset(ctx), false);
    if (voObj && voObj != PROTO_NONE && voObj->isInteger(ctx)) viewOffset = voObj->asLong(ctx);

    // Use the DataView's own byteLength as the upper bound, falling back to
    // the full ArrayBuffer length if the attribute is not present.
    long long dvByteLen = static_cast<long long>(getArrayBufferByteLength(ctx, abObj));
    const proto::ProtoObject* blObj = self->getAttribute(ctx, JSSymbols::dvByteLength(ctx), false);
    if (blObj && blObj != PROTO_NONE && blObj->isInteger(ctx)) dvByteLen = blObj->asLong(ctx);

    long long abLen = static_cast<long long>(getArrayBufferByteLength(ctx, abObj));
    long long windowEnd = viewOffset + dvByteLen; // end of DataView window in ab coords
    if (windowEnd > abLen) windowEnd = abLen;     // clamp to ab boundary

    long long absOffset = viewOffset + byteOffset;
    if (absOffset < viewOffset || absOffset < 0 ||
        absOffset + static_cast<long long>(accessSize) > windowEnd) return nullptr;

    void* raw = getArrayBufferRawPtr(ctx, abObj);
    return raw ? static_cast<uint8_t*>(raw) + absOffset : nullptr;
}

static bool isLittleEndian(proto::ProtoContext* ctx, const proto::ProtoList* args, int argIdx) {
    if (!args || args->getSize(ctx) <= argIdx) return false;
    const proto::ProtoObject* le = args->getAt(ctx, argIdx);
    if (!le || le == PROTO_NONE || le == PROTO_FALSE) return false;
    if (le->isInteger(ctx))  return le->asLong(ctx) != 0;
    if (le->isDouble(ctx) || le->isFloat(ctx)) return le->asDouble(ctx) != 0.0;
    return true; // any non-null, non-false object is truthy
}

static long long dvGetByteOffset(proto::ProtoContext* ctx, const proto::ProtoList* args) {
    if (!args || args->getSize(ctx) == 0) return 0;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (!a0 || a0 == PROTO_NONE) return 0;
    if (a0->isInteger(ctx)) return a0->asLong(ctx);
    if (a0->isDouble(ctx) || a0->isFloat(ctx)) return static_cast<long long>(a0->asDouble(ctx));
    return 0;
}

// Byte-swap helpers.
static uint16_t bswap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }
static uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }

// ---- get methods --------------------------------------------------------
static const proto::ProtoObject* dv_getInt8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(static_cast<int8_t>(raw[0])));
}

static const proto::ProtoObject* dv_getUint8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(raw[0]));
}

static const proto::ProtoObject* dv_getInt16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u; memcpy(&u, raw, 2);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
#else
    if (le) u = bswap16(u);
#endif
    return ctx->fromInteger(static_cast<long long>(static_cast<int16_t>(u)));
}

static const proto::ProtoObject* dv_getUint16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u; memcpy(&u, raw, 2);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
#else
    if (le) u = bswap16(u);
#endif
    return ctx->fromInteger(static_cast<long long>(u));
}

static const proto::ProtoObject* dv_getInt32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u; memcpy(&u, raw, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
#else
    if (le) u = bswap32(u);
#endif
    return ctx->fromInteger(static_cast<long long>(static_cast<int32_t>(u)));
}

static const proto::ProtoObject* dv_getUint32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u; memcpy(&u, raw, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
#else
    if (le) u = bswap32(u);
#endif
    return ctx->fromInteger(static_cast<long long>(u));
}

static const proto::ProtoObject* dv_getFloat32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u; memcpy(&u, raw, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
#else
    if (le) u = bswap32(u);
#endif
    float f; memcpy(&f, &u, 4);
    return ctx->fromDouble(static_cast<double>(f));
}

static const proto::ProtoObject* dv_getFloat64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u; memcpy(&u, raw, 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
#else
    if (le) u = bswap64(u);
#endif
    double d; memcpy(&d, &u, 8);
    return ctx->fromDouble(d);
}

static const proto::ProtoObject* dv_getBigInt64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u; memcpy(&u, raw, 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
#else
    if (le) u = bswap64(u);
#endif
    return ctx->fromInteger(static_cast<long long>(static_cast<int64_t>(u)));
}

static const proto::ProtoObject* dv_getBigUint64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u; memcpy(&u, raw, 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
#else
    if (le) u = bswap64(u);
#endif
    // Values > INT64_MAX cannot be represented as a signed long long; clamp to INT64_MAX.
    // This matches the same limitation in typedArrayGetElement for BigUint64Array (case 10).
    if (u > static_cast<uint64_t>(INT64_MAX)) u = static_cast<uint64_t>(INT64_MAX);
    return ctx->fromInteger(static_cast<long long>(u));
}

// ---- set methods --------------------------------------------------------
static long long dvSetGetValue(proto::ProtoContext* ctx, const proto::ProtoList* args, int idx) {
    if (!args || args->getSize(ctx) <= idx) return 0;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return 0;
    if (v->isInteger(ctx)) return v->asLong(ctx);
    if (v->isDouble(ctx) || v->isFloat(ctx)) return static_cast<long long>(v->asDouble(ctx));
    return 0;
}

static double dvSetGetValueDouble(proto::ProtoContext* ctx, const proto::ProtoList* args, int idx) {
    if (!args || args->getSize(ctx) <= idx) return 0.0;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return 0.0;
    if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) return v->asDouble(ctx);
    return 0.0;
}

static const proto::ProtoObject* dv_setInt8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    raw[0] = static_cast<uint8_t>(dvSetGetValue(ctx, args, 1) & 0xFF);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setUint8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    raw[0] = static_cast<uint8_t>(dvSetGetValue(ctx, args, 1) & 0xFF);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setInt16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u = static_cast<uint16_t>(dvSetGetValue(ctx, args, 1) & 0xFFFF);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
#else
    if (le) u = bswap16(u);
#endif
    memcpy(raw, &u, 2);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setUint16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u = static_cast<uint16_t>(dvSetGetValue(ctx, args, 1) & 0xFFFF);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
#else
    if (le) u = bswap16(u);
#endif
    memcpy(raw, &u, 2);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setInt32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u = static_cast<uint32_t>(dvSetGetValue(ctx, args, 1) & 0xFFFFFFFF);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
#else
    if (le) u = bswap32(u);
#endif
    memcpy(raw, &u, 4);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setUint32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u = static_cast<uint32_t>(dvSetGetValue(ctx, args, 1) & 0xFFFFFFFF);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
#else
    if (le) u = bswap32(u);
#endif
    memcpy(raw, &u, 4);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setFloat32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    float f = static_cast<float>(dvSetGetValueDouble(ctx, args, 1));
    uint32_t u; memcpy(&u, &f, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
#else
    if (le) u = bswap32(u);
#endif
    memcpy(raw, &u, 4);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setFloat64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    double d = dvSetGetValueDouble(ctx, args, 1);
    uint64_t u; memcpy(&u, &d, 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
#else
    if (le) u = bswap64(u);
#endif
    memcpy(raw, &u, 8);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setBigInt64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u = static_cast<uint64_t>(dvSetGetValue(ctx, args, 1));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
#else
    if (le) u = bswap64(u);
#endif
    memcpy(raw, &u, 8);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setBigUint64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u = static_cast<uint64_t>(dvSetGetValue(ctx, args, 1));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
#else
    if (le) u = bswap64(u);
#endif
    memcpy(raw, &u, 8);
    return PROTO_NONE;
}

// ---- DataView.prototype property accessors (buffer, byteOffset, byteLength) ----
static const proto::ProtoObject* dv_get_buffer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* ab = self->getAttribute(ctx, JSSymbols::dvBuffer(ctx), false);
    return ab ? ab : PROTO_NONE;
}

static const proto::ProtoObject* dv_get_byteOffset(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bo = self->getAttribute(ctx, JSSymbols::dvByteOffset(ctx), false);
    return (bo && bo != PROTO_NONE) ? bo : ctx->fromInteger(0LL);
}

static const proto::ProtoObject* dv_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bl = self->getAttribute(ctx, JSSymbols::dvByteLength(ctx), false);
    return (bl && bl != PROTO_NONE) ? bl : ctx->fromInteger(0LL);
}

// ---- Bootstrap ----------------------------------------------------------
void ensureDataViewConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot) return;
    const proto::ProtoObject* root = *globalRoot;
    if (!root) return;

    const proto::ProtoObject* existing =
        root->getAttribute(ctx, JSSymbols::DataView(ctx), true);
    if (existing && existing != PROTO_NONE) return;

    // proto created mutable so the recursive `proto.constructor = ctor`
    // backref (per §25.3.4.1) installs in place.
    // See [[feedback_protojs_proto_constructor_backref]].
    const proto::ProtoObject* objProto =
        (ctx->space) ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto
        ? objProto->newChild(ctx, true)
        : ctx->newObject(true);

    // Register prototype methods.
    proto = proto->setAttribute(ctx, JSSymbols::getInt8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getInt8));
    proto = proto->setAttribute(ctx, JSSymbols::getUint8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getUint8));
    proto = proto->setAttribute(ctx, JSSymbols::getInt16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getInt16));
    proto = proto->setAttribute(ctx, JSSymbols::getUint16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getUint16));
    proto = proto->setAttribute(ctx, JSSymbols::getInt32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getInt32));
    proto = proto->setAttribute(ctx, JSSymbols::getUint32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getUint32));
    proto = proto->setAttribute(ctx, JSSymbols::getFloat32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getFloat32));
    proto = proto->setAttribute(ctx, JSSymbols::getFloat64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getFloat64));
    proto = proto->setAttribute(ctx, JSSymbols::getBigInt64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getBigInt64));
    proto = proto->setAttribute(ctx, JSSymbols::getBigUint64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getBigUint64));
    proto = proto->setAttribute(ctx, JSSymbols::setInt8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setInt8));
    proto = proto->setAttribute(ctx, JSSymbols::setUint8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setUint8));
    proto = proto->setAttribute(ctx, JSSymbols::setInt16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setInt16));
    proto = proto->setAttribute(ctx, JSSymbols::setUint16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setUint16));
    proto = proto->setAttribute(ctx, JSSymbols::setInt32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setInt32));
    proto = proto->setAttribute(ctx, JSSymbols::setUint32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setUint32));
    proto = proto->setAttribute(ctx, JSSymbols::setFloat32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setFloat32));
    proto = proto->setAttribute(ctx, JSSymbols::setFloat64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setFloat64));
    proto = proto->setAttribute(ctx, JSSymbols::setBigInt64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setBigInt64));
    proto = proto->setAttribute(ctx, JSSymbols::setBigUint64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setBigUint64));
    proto = proto->setAttribute(ctx, JSSymbols::buffer(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_get_buffer));
    proto = proto->setAttribute(ctx, JSSymbols::byteOffset(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_get_byteOffset));
    proto = proto->setAttribute(ctx, JSSymbols::byteLength(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_get_byteLength));
    // §25.3.4.18 DataView.prototype[@@toStringTag] = "DataView" with
    // descriptor {writable:false, enumerable:false, configurable:true}
    // → 0x2.  Object.prototype.toString.call(new DataView(buf))
    // returned "[object Object]" before this stamp.
    {
        const proto::ProtoObject* tagKo = ctx->fromUTF8String("Symbol.toStringTag");
        const proto::ProtoString* tagK = tagKo ? tagKo->asString(ctx) : nullptr;
        if (tagK) {
            proto = proto->setAttribute(ctx, tagK,
                ctx->fromUTF8String("DataView"));
            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
        }
    }

    // Build constructor object — mutable so JS-level \`delete\` of
    // configurable own properties (e.g. delete DataView.name in
    // verifyConfigurable) actually removes the slot.  Mirrors the
    // TypedArray / Array / RegExp pattern.
    const proto::ProtoObject* ctor = ctx->newObject(true);
    ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), proto);
    // §25.3.2.1 / §17: DataView.prototype descriptor bits 0x0.
    {
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }
    // Mark as DataView constructor so OP_call_constructor can dispatch.
    ctor = ctor->setAttribute(ctx, JSSymbols::taCtor(ctx),
                               ctx->fromUTF8String("DataView"));
    // §25.3.4 [[Construct]] — stamp the generic isConstructor marker.
    {
        const proto::ProtoString* icK = JSSymbols::isConstructor(ctx);
        if (icK) ctor = ctor->setAttribute(ctx, icK, PROTO_TRUE);
    }
    // §25.3.2 + §17: DataView.name === "DataView" and length === 1,
    // both with descriptor {!writable, !enumerable, configurable} → 0x2.
    // Pre-fix neither slot was installed (built-ins/DataView/name and
    // built-ins/DataView/length both failed "obj should have an own
    // property ...").
    {
        const proto::ProtoString* nameKey = JSSymbols::name(ctx);
        if (nameKey) {
            ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("DataView"));
            const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
            if (pdns) ctor = ctor->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
            const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
            if (pdls) ctor = ctor->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
        }
        // Hot-path hint mirroring the constructor sweep this round.
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) ctor = ctor->setAttribute(ctx, hnw, PROTO_TRUE);
    }

    // DataView.prototype.constructor === DataView per §25.3.4.1.
    // Non-enumerable per spec (0x3 = writable+configurable).
    {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            proto = proto->setAttribute(ctx, ctorWordKey, ctor);
            const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
            if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
        }
    }

    root = root->setAttribute(ctx, JSSymbols::DataView(ctx), ctor);
    // §17 globalThis.DataView descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_DataView__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) root = root->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    }
    *globalRoot = root;
}

} // namespace protojs
