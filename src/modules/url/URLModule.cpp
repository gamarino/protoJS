#include "URLModule.h"
#include "../../ProtoNativeModule.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include <string>

namespace protojs {

namespace {

// URL.prototype.toString → return this.href as-is.
const proto::ProtoObject* urlToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) {
        return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    }
    const proto::ProtoString* hrefKey =
        ctx->fromUTF8String("href")->asString(ctx);
    if (!hrefKey) return ctx->fromUTF8String("");
    const proto::ProtoObject* href = self->getAttribute(ctx, hrefKey, false);
    if (href && href != PROTO_NONE && href->isString(ctx)) return href;
    return ctx->fromUTF8String("");
}

// URL constructor — receives a pre-built `newObj` (parented on
// URL.prototype).  Populates the standard fields and returns
// PROTO_NONE so OP_call_constructor keeps newObj as the constructed
// value.
const proto::ProtoObject* urlConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg0 = args->getAt(ctx, 0);
    if (!arg0 || !arg0->isString(ctx)) return PROTO_NONE;
    auto setStr = [&](const char* name, const proto::ProtoObject* val) {
        const proto::ProtoString* k =
            ctx->fromUTF8String(name)->asString(ctx);
        if (k) self->setAttribute(ctx, k, val);
    };
    setStr("href",     arg0);
    setStr("protocol", ctx->fromUTF8String("http:"));
    setStr("hostname", ctx->fromUTF8String("localhost"));
    setStr("pathname", ctx->fromUTF8String("/"));
    return PROTO_NONE;
}

}  // namespace

const proto::ProtoObject* URLModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    static const NativeEntry protoEntries[] = {
        {"toString", urlToString},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* urlProto =
        ProtoNativeModule::buildModule(ctx, protoEntries, 1);
    if (!urlProto) return globalObj;

    const proto::ProtoObject* urlCtor =
        wrapNativeFunction(ctx, urlConstructor, "URL",
                            /*length=*/1, /*globalRoot=*/nullptr);
    if (!urlCtor) return globalObj;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) urlCtor = urlCtor->setAttribute(ctx, protoKey, urlProto);
    // OP_call_constructor's default-dispatch branch invokes any
    // `__construct__` ProtoMethod on the constructor object with
    // newObj as `self` — that's how Boolean / Number / Map / Set /
    // Promise are wired in.  Use the same hook so `new url.URL(...)`
    // actually runs urlConstructor.
    {
        const proto::ProtoString* ck =
            ctx->fromUTF8String("__construct__")->asString(ctx);
        if (ck) urlCtor = urlCtor->setAttribute(ctx, ck,
            ctx->fromMethod(nullptr, urlConstructor));
    }

    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;
    const proto::ProtoString* urlKey =
        ctx->fromUTF8String("URL")->asString(ctx);
    if (urlKey) mod->setAttribute(ctx, urlKey, urlCtor);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "url", mod);
}

} // namespace protojs
