#include "UtilModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../JSSymbols.h"
#include "../../JSContext.h"
#include "../../runtime/ProtoInterpreter.h"
#include "UtilInspectSource.h"  // generated: kUtilInspectSource
#include <string>

namespace protojs {

namespace {

// ---- Argument helpers --------------------------------------------------

const proto::ProtoObject* arg0(proto::ProtoContext* ctx,
                                const proto::ProtoList* args) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    return a ? a : PROTO_NONE;
}

bool isArrayObj(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    // An Array in protoJS is a mutable ProtoObject carrying an
    // `__elements__` ProtoList.  The presence of that attribute is the
    // canonical test (matches `Array.isArray` internally).
    if (!o || o == PROTO_NONE) return false;
    return getArrayElements(ctx, o) != nullptr;
}

// ---- types.* predicates ------------------------------------------------

const proto::ProtoObject* typesIsArray(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return isArrayObj(ctx, arg0(ctx, args)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* typesIsString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* a = arg0(ctx, args);
    return (ctx && a && a->isString(ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* typesIsNumber(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* a = arg0(ctx, args);
    if (!ctx || !a) return PROTO_FALSE;
    return (a->isInteger(ctx) || a->isDouble(ctx) || a->isFloat(ctx))
        ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* typesIsFunction(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* a = arg0(ctx, args);
    if (!ctx || !a || a == PROTO_NONE) return PROTO_FALSE;
    if (a->isMethod(ctx)) return PROTO_TRUE;
    // Bytecode JS function: the function instance carries a
    // `__bytecode_id__` integer attribute.  Match the runtime's own
    // notion of "is callable bytecode".
    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
    if (bcKey) {
        const proto::ProtoObject* v = a->getAttribute(ctx, bcKey, false);
        if (v && v->isInteger(ctx)) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

const proto::ProtoObject* typesIsObject(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* a = arg0(ctx, args);
    if (!ctx || !a || a == PROTO_NONE) return PROTO_FALSE;
    if (a->isString(ctx) || a->isInteger(ctx) || a->isDouble(ctx) ||
        a->isBoolean(ctx) || a->isNone(ctx) || a->isMethod(ctx)) {
        return PROTO_FALSE;
    }
    if (isArrayObj(ctx, a)) return PROTO_FALSE;
    return PROTO_TRUE;
}

const proto::ProtoObject* typesIsDate(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    // Mirror the original heuristic: any object that responds to
    // `getTime` is treated as a Date.  protoJS does not yet have a
    // first-class Date class; this stays compatible with downstream
    // consumers that test `util.types.isDate(d)`.
    const proto::ProtoObject* a = arg0(ctx, args);
    if (!ctx || !a || a == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoString* gtKey =
        ctx->fromUTF8String("getTime")->asString(ctx);
    if (!gtKey) return PROTO_FALSE;
    const proto::ProtoObject* v = a->getAttribute(ctx, gtKey, false);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    return (v->isMethod(ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

// ---- inspect / format --------------------------------------------------
//
// util.inspect, util.format and util.formatWithOptions are written in
// JavaScript (src/modules/util/inspect.js, a port of Node's), because what they
// print is defined in terms of JavaScript operations -- typeof, String(),
// Number(), parseInt, JSON.stringify, the prototype chain and own keys -- and
// the interpreter already implements those. The build embeds the source; it is
// compiled once per JavaScript context, on the first call, and the object it
// returns is kept on the native global (a GC root) under a private name. The
// native functions below forward their arguments to it; an exception the
// JavaScript raises stays pending and propagates to the caller.

const proto::ProtoObject* inspectImplementation(proto::ProtoContext* ctx) {
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!ctx || !wrapper) return nullptr;
    const proto::ProtoObject* g = wrapper->getNativeGlobal();
    const proto::ProtoObject* keyObj = ctx->fromUTF8String("__pjs_util_inspect__");
    const proto::ProtoString* key = keyObj ? keyObj->asString(ctx) : nullptr;
    if (!g || !key) return nullptr;
    const proto::ProtoObject* cached = g->getAttribute(ctx, key, false);
    if (cached && cached != PROTO_NONE) return cached;

    const std::string source(reinterpret_cast<const char*>(kUtilInspectSource),
                             kUtilInspectSourceSize);
    const proto::ProtoObject* factory =
        wrapper->evalIsolatedToProto(source, "protojs:internal/util/inspect.js");
    if (!factory || factory == PROTO_NONE || hasCallException()) return nullptr;
    const proto::ProtoObject* impl =
        callJSFunction(ctx, factory, PROTO_NONE, ctx->newList());
    if (!impl || impl == PROTO_NONE || hasCallException()) return nullptr;
    // The global is mutable, so this publishes in place; re-read it, since the
    // evaluation may have re-bound the pointer.
    g = wrapper->getNativeGlobal();
    if (g) g->setAttribute(ctx, key, impl);
    return impl;
}

const proto::ProtoObject* callInspectFunction(proto::ProtoContext* ctx,
                                               const char* name,
                                               const proto::ProtoList* args) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* impl = inspectImplementation(ctx);
    if (!impl) {
        if (!hasCallException()) {
            signalNativeException(makeNativeError(ctx, "Error",
                "util: the inspect implementation failed to load"));
        }
        return PROTO_NONE;
    }
    const proto::ProtoObject* nameObj = ctx->fromUTF8String(name);
    const proto::ProtoString* key = nameObj ? nameObj->asString(ctx) : nullptr;
    const proto::ProtoObject* fn = key ? impl->getAttribute(ctx, key, false) : nullptr;
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    return callJSFunction(ctx, fn, impl, args ? args : ctx->newList());
}

const proto::ProtoObject* utilInspect(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return callInspectFunction(ctx, "inspect", args);
}

const proto::ProtoObject* utilFormat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return callInspectFunction(ctx, "format", args);
}

const proto::ProtoObject* utilFormatWithOptions(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return callInspectFunction(ctx, "formatWithOptions", args);
}

const proto::ProtoObject* utilPromisify(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    // Stub — the QuickJS-side implementation was already a no-op
    // returning undefined.  Real implementation would build a Deferred
    // wrapper; tracked separately.
    return ctx ? PROTO_NONE : PROTO_NONE;
}

}  // namespace

const proto::ProtoObject* UtilModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    static const NativeEntry typesEntries[] = {
        {"isArray",    typesIsArray},
        {"isString",   typesIsString},
        {"isNumber",   typesIsNumber},
        {"isObject",   typesIsObject},
        {"isFunction", typesIsFunction},
        {"isDate",     typesIsDate},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* types =
        ProtoNativeModule::buildModule(ctx, typesEntries, 6);
    if (!types) return globalObj;

    static const NativeEntry utilEntries[] = {
        {"promisify", utilPromisify},
        {"inspect",   utilInspect},
        {"format",    utilFormat},
        {"formatWithOptions", utilFormatWithOptions},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, utilEntries, 4);
    if (!mod) return globalObj;
    const proto::ProtoString* tk = ctx->fromUTF8String("types")->asString(ctx);
    if (tk) mod = mod->setAttribute(ctx, tk, types);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "util", mod);
}

} // namespace protojs
