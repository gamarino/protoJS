#include "DataViewPrototype.h"
#include "runtime/AccessorFlags.h"
#include "JSNumber.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "TypedArrayPrototype.h"
#include "runtime/ProtoInterpreter.h"
#include "ProtoCoreTypes.h"
#include <string>
#include "protoCore.h"
#include <cstring>
#include <cstdint>
#include <cstdlib>

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

static bool isLittleEndian(proto::ProtoContext* ctx, const proto::ProtoArguments* args, int argIdx) {
    if (!args || args->getSize(ctx) <= argIdx) return false;
    const proto::ProtoObject* le = args->getAt(ctx, argIdx);
    if (!le || le == PROTO_NONE || le == PROTO_FALSE) return false;
    if (le->isInteger(ctx))  return le->asLong(ctx) != 0;
    if (le->isDouble(ctx) || le->isFloat(ctx)) return le->asDouble(ctx) != 0.0;
    return true; // any non-null, non-false object is truthy
}

static long long dvGetByteOffset(proto::ProtoContext* ctx, const proto::ProtoArguments* args) {
    if (!args || args->getSize(ctx) == 0) return 0;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (!a0 || a0 == PROTO_NONE) return 0;
    if (a0->isInteger(ctx)) return a0->asLong(ctx);
    if (a0->isDouble(ctx) || a0->isFloat(ctx)) return static_cast<long long>(a0->asDouble(ctx));
    return 0;
}

// Byte-swap helpers.
static uint16_t bswap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }
#if defined(_MSC_VER) && !defined(__clang__)
// <cstdlib> declares the byte-swap intrinsics.
static uint32_t bswap32(uint32_t v) { return _byteswap_ulong(v); }
static uint64_t bswap64(uint64_t v) { return _byteswap_uint64(v); }
#else
static uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }
#endif

// ---- get methods --------------------------------------------------------
static const proto::ProtoObject* dv_getInt8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(static_cast<int8_t>(raw[0])));
}

static const proto::ProtoObject* dv_getUint8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(raw[0]));
}

static const proto::ProtoObject* dv_getInt16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    return makeNumber(ctx, static_cast<double>(f));
}

static const proto::ProtoObject* dv_getFloat64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    return makeNumber(ctx, d);
}

static const proto::ProtoObject* dv_getBigInt64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
static long long dvSetGetValue(proto::ProtoContext* ctx, const proto::ProtoArguments* args, int idx) {
    if (!args || args->getSize(ctx) <= idx) return 0;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return 0;
    if (v->isInteger(ctx)) return v->asLong(ctx);
    if (v->isDouble(ctx) || v->isFloat(ctx)) return static_cast<long long>(v->asDouble(ctx));
    return 0;
}

static double dvSetGetValueDouble(proto::ProtoContext* ctx, const proto::ProtoArguments* args, int idx) {
    if (!args || args->getSize(ctx) <= idx) return 0.0;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return 0.0;
    if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) return v->asDouble(ctx);
    return 0.0;
}

static const proto::ProtoObject* dv_setInt8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    raw[0] = static_cast<uint8_t>(dvSetGetValue(ctx, args, 1) & 0xFF);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setUint8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    raw[0] = static_cast<uint8_t>(dvSetGetValue(ctx, args, 1) & 0xFF);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setInt16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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
    const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*)
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

// ---- DataView.prototype accessors (buffer, byteOffset, byteLength) ------
// §25.3.4.1 / .2 / .3: getters that throw a TypeError for a receiver that
// is not a DataView (one without its own [[ViewedArrayBuffer]] slot).
static const proto::ProtoObject* dvRequireView(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const char* what) {
    if (!isDataView(ctx, self)) {
        const std::string msg = std::string("DataView.prototype.") + what
            + " called on an object that is not a DataView";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
    }
    return self;
}

static const proto::ProtoObject* dv_get_buffer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*)
{
    dvRequireView(ctx, self, "buffer");
    return self->getAttribute(ctx, JSSymbols::dvBuffer(ctx), false);
}

static const proto::ProtoObject* dv_get_byteOffset(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*)
{
    dvRequireView(ctx, self, "byteOffset");
    const proto::ProtoObject* bo = self->getAttribute(ctx, JSSymbols::dvByteOffset(ctx), false);
    return (bo && bo != PROTO_NONE) ? bo : ctx->fromInteger(0LL);
}

static const proto::ProtoObject* dv_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*)
{
    dvRequireView(ctx, self, "byteLength");
    const proto::ProtoObject* bl = self->getAttribute(ctx, JSSymbols::dvByteLength(ctx), false);
    return (bl && bl != PROTO_NONE) ? bl : ctx->fromInteger(0LL);
}

bool isDataView(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return ctx && obj && obj != PROTO_NONE && obj != getUndefinedSentinel()
        && obj != getNullSentinel() && !obj->isInteger(ctx) && !obj->isDouble(ctx)
        && !obj->isString(ctx) && !obj->isBoolean(ctx)
        && obj->hasOwnAttribute(ctx, JSSymbols::dvBuffer(ctx)) == PROTO_TRUE;
}

// new DataView(buffer[, byteOffset[, byteLength]]) (§25.3.2.1).  Until
// 2026-10 there was no [[Construct]] for DataView: `new DataView(ab)` made
// an empty object, so every DataView method failed and ArrayBuffer.isView
// was false for it.
const proto::ProtoObject* constructDataView(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proto,
                                            const proto::ProtoList* args) {
    if (!ctx) return PROTO_NONE;
    const auto arg = [&](int i) -> const proto::ProtoObject* {
        return (args && static_cast<int>(args->getSize(ctx)) > i) ? args->getAt(ctx, i)
                                                                  : getUndefinedSentinel();
    };
    const proto::ProtoObject* buffer = arg(0);
    if (!buffer || buffer == PROTO_NONE || buffer == getUndefinedSentinel()
        || buffer == getNullSentinel() || buffer->isInteger(ctx) || buffer->isDouble(ctx)
        || buffer->isString(ctx) || buffer->isBoolean(ctx) || !isArrayBuffer(ctx, buffer)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "First argument to DataView constructor must be an ArrayBuffer"));
    }
    long long offset = 0;
    if (!toIndex(ctx, arg(1), offset, "Start offset is outside the bounds of the buffer"))
        return PROTO_NONE;
    const long long bufferLength = static_cast<long long>(getArrayBufferByteLength(ctx, buffer));
    if (offset > bufferLength) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Start offset is outside the bounds of the buffer"));
    }
    long long viewLength = bufferLength - offset;
    const proto::ProtoObject* lenArg = arg(2);
    if (lenArg && lenArg != PROTO_NONE && lenArg != getUndefinedSentinel()) {
        if (!toIndex(ctx, lenArg, viewLength, "Invalid DataView length")) return PROTO_NONE;
        if (offset + viewLength > bufferLength) {
            signalNativeException(makeNativeError(ctx, "RangeError", "Invalid DataView length"));
        }
    }
    const bool protoIsObject = proto && proto != PROTO_NONE && proto != getUndefinedSentinel()
        && proto != getNullSentinel() && !proto->isInteger(ctx) && !proto->isDouble(ctx)
        && !proto->isString(ctx) && !proto->isBoolean(ctx);
    const proto::ProtoObject* dv = protoIsObject ? proto->newChild(ctx, true)
                                                 : ctx->newObject(true);
    dv = dv->setAttribute(ctx, JSSymbols::dvBuffer(ctx), buffer);
    dv = dv->setAttribute(ctx, JSSymbols::dvByteOffset(ctx), ctx->fromInteger(offset));
    dv = dv->setAttribute(ctx, JSSymbols::dvByteLength(ctx), ctx->fromInteger(viewLength));
    return dv;
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

    // nullptr, not proto: see the note in TypedArrayPrototype.cpp.  A method cell
    // bound to the prototype that holds it is a permanent mutable cycle, and the
    // binding is never read.
    // Register prototype methods.
    proto = proto->setAttribute(ctx, JSSymbols::getInt8(ctx),
        ctx->fromMethod(nullptr, dv_getInt8));
    proto = proto->setAttribute(ctx, JSSymbols::getUint8(ctx),
        ctx->fromMethod(nullptr, dv_getUint8));
    proto = proto->setAttribute(ctx, JSSymbols::getInt16(ctx),
        ctx->fromMethod(nullptr, dv_getInt16));
    proto = proto->setAttribute(ctx, JSSymbols::getUint16(ctx),
        ctx->fromMethod(nullptr, dv_getUint16));
    proto = proto->setAttribute(ctx, JSSymbols::getInt32(ctx),
        ctx->fromMethod(nullptr, dv_getInt32));
    proto = proto->setAttribute(ctx, JSSymbols::getUint32(ctx),
        ctx->fromMethod(nullptr, dv_getUint32));
    proto = proto->setAttribute(ctx, JSSymbols::getFloat32(ctx),
        ctx->fromMethod(nullptr, dv_getFloat32));
    proto = proto->setAttribute(ctx, JSSymbols::getFloat64(ctx),
        ctx->fromMethod(nullptr, dv_getFloat64));
    proto = proto->setAttribute(ctx, JSSymbols::getBigInt64(ctx),
        ctx->fromMethod(nullptr, dv_getBigInt64));
    proto = proto->setAttribute(ctx, JSSymbols::getBigUint64(ctx),
        ctx->fromMethod(nullptr, dv_getBigUint64));
    proto = proto->setAttribute(ctx, JSSymbols::setInt8(ctx),
        ctx->fromMethod(nullptr, dv_setInt8));
    proto = proto->setAttribute(ctx, JSSymbols::setUint8(ctx),
        ctx->fromMethod(nullptr, dv_setUint8));
    proto = proto->setAttribute(ctx, JSSymbols::setInt16(ctx),
        ctx->fromMethod(nullptr, dv_setInt16));
    proto = proto->setAttribute(ctx, JSSymbols::setUint16(ctx),
        ctx->fromMethod(nullptr, dv_setUint16));
    proto = proto->setAttribute(ctx, JSSymbols::setInt32(ctx),
        ctx->fromMethod(nullptr, dv_setInt32));
    proto = proto->setAttribute(ctx, JSSymbols::setUint32(ctx),
        ctx->fromMethod(nullptr, dv_setUint32));
    proto = proto->setAttribute(ctx, JSSymbols::setFloat32(ctx),
        ctx->fromMethod(nullptr, dv_setFloat32));
    proto = proto->setAttribute(ctx, JSSymbols::setFloat64(ctx),
        ctx->fromMethod(nullptr, dv_setFloat64));
    proto = proto->setAttribute(ctx, JSSymbols::setBigInt64(ctx),
        ctx->fromMethod(nullptr, dv_setBigInt64));
    proto = proto->setAttribute(ctx, JSSymbols::setBigUint64(ctx),
        ctx->fromMethod(nullptr, dv_setBigUint64));
    // buffer / byteOffset / byteLength are accessors: a getter, no setter,
    // { !enumerable, configurable } (0x2), installed as %TypedArray%.prototype's
    // are (TypedArrayPrototype.cpp).  They were plain methods, so dv.buffer
    // was a function.
    {
        struct Getter { const char* key; const char* name; proto::ProtoMethod fn; };
        static const Getter kGetters[] = {
            {"buffer", "get buffer", dv_get_buffer},
            {"byteLength", "get byteLength", dv_get_byteLength},
            {"byteOffset", "get byteOffset", dv_get_byteOffset},
        };
        const proto::ProtoObject* fnParent =
            (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
        for (const Getter& g : kGetters) {
            const proto::ProtoObject* getter = fnParent ? fnParent->newChild(ctx, true)
                                                        : ctx->newObject(true);
            getter = getter->setAttribute(ctx, JSSymbols::nativeFn(ctx), ctx->fromMethod(nullptr, g.fn));
            getter = getter->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(0LL));
            getter = getter->setAttribute(ctx, JSSymbols::pdLength(ctx), ctx->fromInteger(0x2LL));
            getter = getter->setAttribute(ctx, JSSymbols::name(ctx), ctx->fromUTF8String(g.name));
            getter = getter->setAttribute(ctx, JSSymbols::pdName(ctx), ctx->fromInteger(0x2LL));
            getter = getter->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);
            const std::string gk = std::string("__get_") + g.key + "__";
            const std::string pd = std::string("__pd_") + g.key + "__";
            proto = proto->setAttribute(ctx, ctx->fromUTF8String(gk.c_str())->asString(ctx), getter);
            proto = proto->setAttribute(ctx, ctx->fromUTF8String(g.key)->asString(ctx), PROTO_NONE);
            proto = proto->setAttribute(ctx, ctx->fromUTF8String(pd.c_str())->asString(ctx),
                                        ctx->fromInteger(0x2LL));
        }
        proto = markHasAccessors(ctx, proto);
        proto = proto->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);
    }
    // §25.3.4.18 DataView.prototype[@@toStringTag] = "DataView" with
    // descriptor {writable:false, enumerable:false, configurable:true}
    // → 0x2.  Object.prototype.toString.call(new DataView(buf))
    // returned "[object Object]" before this stamp.
    {
        const proto::ProtoString* tagK = PROTOJS_NAME(ctx, "Symbol.toStringTag");
        if (tagK) {
            proto = proto->setAttribute(ctx, tagK,
                ctx->fromUTF8String("DataView"));
            const proto::ProtoString* pdk = PROTOJS_NAME(ctx, "__pd_Symbol.toStringTag__");
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
        const proto::ProtoString* pdpk = PROTOJS_NAME(ctx, "__pd_prototype__");
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
        const proto::ProtoString* pdk = PROTOJS_NAME(ctx, "__pd_DataView__");
        if (pdk) root = root->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    }
    *globalRoot = root;
}

} // namespace protojs
