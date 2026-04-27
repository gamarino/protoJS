#include "UtilModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../JSSymbols.h"
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

void inspectInto(proto::ProtoContext* ctx,
                  const proto::ProtoObject* v, std::string& out) {
    if (!ctx || !v || v == PROTO_NONE || v->isNone(ctx)) {
        out += "undefined";
        return;
    }
    if (v->isString(ctx)) {
        std::string s;
        v->asString(ctx)->toUTF8String(ctx, s);
        out += s;
        return;
    }
    if (v->isBoolean(ctx)) {
        out += v->asBoolean(ctx) ? "true" : "false";
        return;
    }
    if (v->isInteger(ctx)) {
        out += std::to_string(v->asLong(ctx));
        return;
    }
    if (v->isDouble(ctx)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", v->asDouble(ctx));
        out += buf;
        return;
    }
    if (isArrayObj(ctx, v)) {
        out += "[Array]";
        return;
    }
    out += "[Object]";
}

const proto::ProtoObject* utilInspect(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) == 0) {
        return ctx ? ctx->fromUTF8String("undefined") : PROTO_NONE;
    }
    std::string s;
    inspectInto(ctx, args->getAt(ctx, 0), s);
    return ctx->fromUTF8String(s.c_str());
}

const proto::ProtoObject* utilFormat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    // Minimal Node-compatible behaviour: when the first argument is a
    // string, return it verbatim; otherwise inspect each argument and
    // join with single spaces.  Matches the original QuickJS-side
    // module's externally observable surface.
    if (!ctx || !args || args->getSize(ctx) == 0)
        return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    const proto::ProtoObject* first = args->getAt(ctx, 0);
    if (first && first->isString(ctx)) {
        return first;
    }
    std::string out;
    long long n = static_cast<long long>(args->getSize(ctx));
    for (long long i = 0; i < n; ++i) {
        if (i > 0) out += ' ';
        inspectInto(ctx, args->getAt(ctx, static_cast<int>(i)), out);
    }
    return ctx->fromUTF8String(out.c_str());
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
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, utilEntries, 3);
    if (!mod) return globalObj;
    const proto::ProtoString* tk = ctx->fromUTF8String("types")->asString(ctx);
    if (tk) mod = mod->setAttribute(ctx, tk, types);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "util", mod);
}

} // namespace protojs
