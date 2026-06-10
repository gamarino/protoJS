#include "IteratorPrototype.h"
#include "JSSymbols.h"

namespace protojs {

static const proto::ProtoObject* iteratorReturnSelf(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return self;
}

const proto::ProtoObject* getIteratorPrototype(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* s_iteratorProto = nullptr;
    if (s_iteratorProto) return s_iteratorProto;
    if (!ctx) return nullptr;
    proto::ProtoObject* objProto =
        ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto
        ? objProto->newChild(ctx, true) : ctx->newObject(true);
    if (!proto) return nullptr;

    // @@iterator returning this — required by GetIterator (§27.1.2.1).
    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
    if (symIterKey) {
        const proto::ProtoObject* iterFn = ctx->fromMethod(nullptr, iteratorReturnSelf);
        if (iterFn) proto = proto->setAttribute(ctx, symIterKey, iterFn);
        // pd descriptor 0x3 (writable, configurable, non-enumerable).
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.iterator__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    }

    // @@toStringTag = "Iterator" — ES2024+ §27.1.2.2.
    const proto::ProtoString* tag = JSSymbols::symbolToStringTag(ctx);
    if (tag) {
        proto = proto->setAttribute(ctx, tag, ctx->fromUTF8String("Iterator"));
        // pd descriptor 0x2 (configurable, non-writable, non-enumerable).
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    }

    // Per-target gating flag so resolvePutFieldOOP enforces the
    // writable=false bit on @@toStringTag.  Without this stamp,
    // verifyProperty's writable probe (which writes and re-reads)
    // succeeds and the property-descriptor.js tests fail.
    const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
    if (hnwK) proto = proto->setAttribute(ctx, hnwK, PROTO_TRUE);

    s_iteratorProto = proto;
    return s_iteratorProto;
}

} // namespace protojs
