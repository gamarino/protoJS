#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include <cstring>
#include <algorithm>

namespace protojs {

// Module-level ArrayBuffer prototype pointer (set once during ensureArrayBufferConstructor).
static const proto::ProtoObject* s_abProto = nullptr;

// ---------------------------------------------------------------------------
// Internal helper: native method signature typedef for clarity.
// Signature: (ctx, self, parentLink, args, namedArgs) -> ProtoObject*
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ab_get_byteLength — native getter for ArrayBuffer.prototype.byteLength
// ---------------------------------------------------------------------------
static const proto::ProtoObject* ab_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    unsigned long len = getArrayBufferByteLength(ctx, self);
    return ctx->fromInteger(static_cast<long long>(len));
}

// ---------------------------------------------------------------------------
// ab_slice — ArrayBuffer.prototype.slice(begin, end?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* ab_slice(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    // Check if self is a valid ArrayBuffer by verifying it has the backing store object.
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoString* dataKey = JSSymbols::abData(ctx);
    if (!dataKey) return PROTO_NONE;
    const proto::ProtoObject* bufObj = self->getAttribute(ctx, dataKey, false);
    if (!bufObj || bufObj == PROTO_NONE) return PROTO_NONE;

    unsigned long srcLen = getArrayBufferByteLength(ctx, self);
    void* srcRaw = getArrayBufferRawPtr(ctx, self);

    // Parse begin argument.
    long long iBegin = 0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE) {
            if (a0->isInteger(ctx))       iBegin = a0->asLong(ctx);
            else if (a0->isDouble(ctx) || a0->isFloat(ctx))   iBegin = static_cast<long long>(a0->asDouble(ctx));
        }
    }

    // Parse end argument (defaults to srcLen).
    long long iEnd = static_cast<long long>(srcLen);
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE) {
            if (a1->isInteger(ctx))       iEnd = a1->asLong(ctx);
            else if (a1->isDouble(ctx) || a1->isFloat(ctx))   iEnd = static_cast<long long>(a1->asDouble(ctx));
        }
    }

    // Clamp negative indices relative to srcLen.
    long long sLen = static_cast<long long>(srcLen);
    if (iBegin < 0) iBegin = std::max(0LL, sLen + iBegin);
    else            iBegin = std::min(iBegin, sLen);

    if (iEnd < 0)   iEnd   = std::max(0LL, sLen + iEnd);
    else            iEnd   = std::min(iEnd, sLen);

    long long newLen = std::max(0LL, iEnd - iBegin);

    const proto::ProtoObject* newAb = createArrayBuffer(ctx, static_cast<unsigned long>(newLen));
    if (!newAb || newAb == PROTO_NONE) return PROTO_NONE;

    if (newLen > 0) {
        void* dstRaw = getArrayBufferRawPtr(ctx, newAb);
        if (dstRaw)
            std::memcpy(dstRaw, static_cast<const uint8_t*>(srcRaw) + iBegin,
                        static_cast<size_t>(newLen));
    }
    return newAb;
}

// ---------------------------------------------------------------------------
// ab_isView — ArrayBuffer.isView(arg)
// Returns true if arg is a TypedArray or DataView.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* ab_isView(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* arg = args->getAt(ctx, 0);
    if (!arg || arg == PROTO_NONE) return PROTO_FALSE;

    // TypedArray: has __ta_element_type__ attribute.
    const proto::ProtoString* taElemTypeKey = JSSymbols::taElementType(ctx);
    if (taElemTypeKey) {
        const proto::ProtoObject* v = arg->getAttribute(ctx, taElemTypeKey, false);
        if (v && v != PROTO_NONE) return PROTO_TRUE;
    }

    // DataView: has __dv_buffer__ attribute.
    const proto::ProtoString* dvBufferKey = JSSymbols::dvBuffer(ctx);
    if (dvBufferKey) {
        const proto::ProtoObject* v = arg->getAttribute(ctx, dvBufferKey, false);
        if (v && v != PROTO_NONE) return PROTO_TRUE;
    }

    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const proto::ProtoObject* createArrayBuffer(proto::ProtoContext* ctx,
                                            unsigned long byteLength) {
    if (!ctx) return PROTO_NONE;

    // Create instance inheriting from ArrayBuffer.prototype (if available).
    const proto::ProtoObject* ab = s_abProto
        ? s_abProto->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ab) return PROTO_NONE;

    // Allocate raw backing memory via ExternalBuffer (GC-managed).
    const proto::ProtoObject* bufObj = ctx->newExternalBuffer(byteLength);
    if (!bufObj) return PROTO_NONE;

    // Zero-fill the buffer.
    void* rawPtr = bufObj->getRawPointerIfExternalBuffer(ctx);
    if (rawPtr && byteLength > 0)
        std::memset(rawPtr, 0, byteLength);

    // Store ExternalBuffer on the instance.
    const proto::ProtoString* dataKey = JSSymbols::abData(ctx);
    if (dataKey)
        ab = ab->setAttribute(ctx, dataKey, bufObj);

    return ab;
}

void* getArrayBufferRawPtr(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    if (!ctx || !ab || ab == PROTO_NONE) return nullptr;
    const proto::ProtoString* dataKey = JSSymbols::abData(ctx);
    if (!dataKey) return nullptr;
    const proto::ProtoObject* bufObj = ab->getAttribute(ctx, dataKey, false);
    if (!bufObj || bufObj == PROTO_NONE) return nullptr;
    return bufObj->getRawPointerIfExternalBuffer(ctx);
}

unsigned long getArrayBufferByteLength(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    if (!ctx || !ab || ab == PROTO_NONE) return 0;
    const proto::ProtoString* dataKey = JSSymbols::abData(ctx);
    if (!dataKey) return 0;
    const proto::ProtoObject* bufObj = ab->getAttribute(ctx, dataKey, false);
    if (!bufObj || bufObj == PROTO_NONE) return 0;
    const proto::ProtoExternalBuffer* extBuf = bufObj->asExternalBuffer(ctx);
    if (!extBuf) return 0;
    return extBuf->getSize(ctx);
}

bool isArrayBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    if (!ctx || !ab || ab == PROTO_NONE) return false;

    // Must not be detached.
    const proto::ProtoString* detachedKey = JSSymbols::abDetached(ctx);
    if (detachedKey) {
        const proto::ProtoObject* detached = ab->getAttribute(ctx, detachedKey, false);
        if (detached && detached == PROTO_TRUE) return false;
    }

    // Must have a valid backing store.
    void* raw = getArrayBufferRawPtr(ctx, ab);
    return raw != nullptr;
}

// ---------------------------------------------------------------------------
// ensureArrayBufferConstructor — idempotent bootstrap
// ---------------------------------------------------------------------------
void ensureArrayBufferConstructor(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    // Idempotency guard: return early if already registered.
    const proto::ProtoString* abKey = JSSymbols::ArrayBuffer(ctx);
    if (!abKey) return;
    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, abKey, false);
    if (existing && existing != PROTO_NONE) return;

    // ------------------------------------------------------------------
    // Build ArrayBuffer.prototype (inherits from Object.prototype).
    // ------------------------------------------------------------------
    // proto is mutable so the recursive `proto.constructor = ctor`
    // backref (per §25.1.4.1) installs in place — required so that
    // `new ArrayBuffer(8).constructor === ArrayBuffer` per spec.
    // See [[feedback_protojs_proto_constructor_backref]].
    const proto::ProtoObject* objectProto = ctx->space->objectPrototype;
    const proto::ProtoObject* proto = objectProto
        ? objectProto->newChild(ctx, true)
        : ctx->newObject(true);

    // Register prototype methods.
    struct { const char* name; proto::ProtoMethod fn; long long len; } methods[] = {
        { "byteLength", ab_get_byteLength, 0 },
        { "slice",      ab_slice,          2 },
    };
    for (auto& m : methods) {
        const proto::ProtoString* key = ctx->fromUTF8String(m.name)->asString(ctx);
        if (!key) continue;
        const proto::ProtoObject* fn = ctx->fromMethod(nullptr, m.fn);
        if (!fn || fn == PROTO_NONE) continue;
        const proto::ProtoString* lenKey  = JSSymbols::length(ctx);
        const proto::ProtoString* nameKey = JSSymbols::name(ctx);
        if (lenKey)  fn = fn->setAttribute(ctx, lenKey,  ctx->fromInteger(m.len));
        if (nameKey) fn = fn->setAttribute(ctx, nameKey, ctx->fromUTF8String(m.name));
        proto = proto->setAttribute(ctx, key, fn);
    }

    // Cache the prototype for createArrayBuffer.
    s_abProto = proto;

    // ------------------------------------------------------------------
    // Build the ArrayBuffer constructor object.
    // ------------------------------------------------------------------
    const proto::ProtoObject* ctor = ctx->newObject(false);

    // Set constructor.prototype.
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);

    // Mark with __typed_array_ctor__ = "ArrayBuffer" for interpreter dispatch.
    const proto::ProtoString* taCtorKey = JSSymbols::taCtor(ctx);
    if (taCtorKey)
        ctor = ctor->setAttribute(ctx, taCtorKey,
                                  ctx->fromUTF8String("ArrayBuffer"));

    // Set constructor name.
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey)
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("ArrayBuffer"));

    // Add static method: isView.
    {
        const proto::ProtoString* isViewKey = JSSymbols::isView(ctx);
        if (isViewKey) {
            const proto::ProtoObject* fn = ctx->fromMethod(nullptr, ab_isView);
            if (fn && fn != PROTO_NONE) {
                const proto::ProtoString* lenKey  = JSSymbols::length(ctx);
                const proto::ProtoString* nmKey   = JSSymbols::name(ctx);
                if (lenKey) fn = fn->setAttribute(ctx, lenKey,  ctx->fromInteger(1LL));
                if (nmKey)  fn = fn->setAttribute(ctx, nmKey,   ctx->fromUTF8String("isView"));
            }
            if (fn) ctor = ctor->setAttribute(ctx, isViewKey, fn);
        }
    }

    // ArrayBuffer.prototype.constructor === ArrayBuffer per §25.1.4.1.
    // Non-enumerable per spec (0x3 = writable+configurable).
    {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            const proto::ProtoObject* updated =
                proto->setAttribute(ctx, ctorWordKey, ctor);
            if (updated && updated != PROTO_NONE) {
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_constructor__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) updated = updated->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
                s_abProto = updated;
            }
        }
    }

    // ------------------------------------------------------------------
    // Register on global root.
    // ------------------------------------------------------------------
    *globalRoot = (*globalRoot)->setAttribute(ctx, abKey, ctor);
    // §17 globalThis.ArrayBuffer descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_ArrayBuffer__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
