#include "LazyPrototype.h"

#include "../ObjectPrototype.h"
#include "ProtoInterpreter.h"

#include <string>

namespace protojs {

void materializeLazyPrototype(proto::ProtoContext* ctx, const proto::ProtoObject* fn) {
    if (!ctx || !fn || fn == PROTO_NONE) return;
    const proto::ProtoString* lazyKey = JSSymbols::lazyPrototype(ctx);
    // nullptr for a non-object receiver (tagged integers, strings) and for
    // every object that never had the marker.
    if (!lazyKey || fn->getOwnAttributeDirect(ctx, lazyKey) != PROTO_TRUE) return;
    // Clear the marker first, so nothing below can re-enter.
    fn->setAttribute(ctx, lazyKey, PROTO_FALSE);
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (!protoKey) return;
    // A `prototype` already written by the program (f.prototype = x, or
    // defineProperty) is the property; there is nothing to create.
    if (fn->getOwnAttributeDirect(ctx, protoKey) != nullptr) return;

    // The object MakeConstructor would have made: an ordinary object whose
    // [[Prototype]] is Object.prototype, with constructor === fn
    // ({writable, configurable, not enumerable}: descriptor bits 0x3).
    const proto::ProtoObject* objectProto =
        ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objectProto
        ? objectProto->newChild(ctx, true)
        : ctx->newObject(true);
    if (!proto) return;
    if (const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx))
        proto = proto->setAttribute(ctx, ctorKey, fn);
    if (const proto::ProtoString* pdc = JSSymbols::pdConstructor(ctx))
        proto = proto->setAttribute(ctx, pdc, ctx->fromInteger(0x3LL));
    // fn is a mutable closure: the write lands on fn itself.
    fn->setAttribute(ctx, protoKey, proto);
}

bool keyNamesPrototype(proto::ProtoContext* ctx, const proto::ProtoString* key) {
    if (!ctx || !key || key->getSize(ctx) != 9) return false;
    std::string text;
    key->toUTF8String(ctx, text);
    return text == "prototype";
}

void materializeLazyPrototypeOnChain(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!ctx) return;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    for (int depth = 0; depth < 64 && obj && obj != PROTO_NONE && obj != nullSentinel; ++depth) {
        materializeLazyPrototype(ctx, obj);
        if (!protoKey || obj->getOwnAttributeDirect(ctx, protoKey) != nullptr) return;
        const proto::ProtoObject* next = getJSProtoOverride(ctx, obj);
        if (!next) next = obj->getPrototype(ctx);
        if (next == obj) return;
        obj = next;
    }
}

}  // namespace protojs
