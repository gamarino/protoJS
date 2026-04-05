#include "RegExpStringIterator.h"
#include "RegExpPrototype.h"
#include "ArrayPrototype.h"
#include "JSSymbols.h"
#include "JSContext.h"
#include "headers/protoCore.h"
extern "C" {
#include "libregexp.h"
}
#include <string>
#include <vector>

namespace protojs {

namespace {

/** Convert any ProtoObject to its UTF-8 string representation. */
static std::string objToStrLocal(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return "";
    std::string r;
    if (obj->isString(ctx)) { obj->asString(ctx)->toUTF8String(ctx, r); return r; }
    if (obj->isInteger(ctx)) return std::to_string(obj->asLong(ctx));
    if (obj->isDouble(ctx)) {
        char buf[64]; snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(ctx)); return buf;
    }
    return "";
}

/**
 * RegExpStringIterator.prototype.next() — advances the iterator one step.
 * Returns { value: matchArray | undefined, done: bool }.
 */
static const proto::ProtoObject* regexpStringIteratorNext(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* doneResult = ctx->newObject(true);
    doneResult = doneResult->setAttribute(ctx, JSSymbols::value(ctx), PROTO_NONE);
    doneResult = doneResult->setAttribute(ctx, JSSymbols::done(ctx),  PROTO_TRUE);

    if (!ctx || !self) return doneResult;

    const proto::ProtoObject* doneFlag = self->getAttribute(ctx, JSSymbols::iterDone(ctx), false);
    if (doneFlag && doneFlag == PROTO_TRUE) return doneResult;

    const proto::ProtoObject* reObj  = self->getAttribute(ctx, JSSymbols::iterRe(ctx),  false);
    const proto::ProtoObject* strObj = self->getAttribute(ctx, JSSymbols::iterStr(ctx), false);
    if (!reObj || reObj == PROTO_NONE || !strObj || strObj == PROTO_NONE) return doneResult;

    std::string str = objToStrLocal(ctx, strObj);
    const proto::ProtoList* execArgs = ctx->newList();
    execArgs = execArgs->appendLast(ctx, ctx->fromUTF8String(str.c_str()));

    const proto::ProtoObject* match = regexpExec(ctx, reObj, nullptr, execArgs, nullptr);

    if (!match || match == PROTO_NONE) {
        self->setAttribute(ctx, JSSymbols::iterDone(ctx), PROTO_TRUE);
        return doneResult;
    }

    // For global regexps: advance lastIndex by 1 when match is empty to avoid infinite loop.
    const proto::ProtoObject* m0 = match->getAttribute(ctx, JSSymbols::indexKey(ctx, 0), false);
    std::string m0str = objToStrLocal(ctx, m0);
    if (m0str.empty()) {
        const proto::ProtoObject* liObj = reObj->getAttribute(ctx, JSSymbols::lastIndex(ctx), false);
        long long li = (liObj && liObj->isInteger(ctx)) ? liObj->asLong(ctx) : 0;
        reObj->setAttribute(ctx, JSSymbols::lastIndex(ctx), ctx->fromInteger(li + 1));
    }

    const proto::ProtoObject* result = ctx->newObject(true);
    result = result->setAttribute(ctx, JSSymbols::value(ctx), match);
    result = result->setAttribute(ctx, JSSymbols::done(ctx),  PROTO_FALSE);
    return result;
}

/** Parse a flags string into libregexp flag bits. */
static int parseFlagsLocal(const std::string& f) {
    int flags = 0;
    for (char c : f) {
        switch (c) {
            case 'g': flags |= LRE_FLAG_GLOBAL;       break;
            case 'i': flags |= LRE_FLAG_IGNORECASE;   break;
            case 'm': flags |= LRE_FLAG_MULTILINE;    break;
            case 's': flags |= LRE_FLAG_DOTALL;       break;
            case 'u': flags |= LRE_FLAG_UNICODE;      break;
            case 'y': flags |= LRE_FLAG_STICKY;       break;
            case 'd': flags |= LRE_FLAG_INDICES;      break;
            case 'v': flags |= LRE_FLAG_UNICODE_SETS; break;
            default: break;
        }
    }
    return flags;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const proto::ProtoObject* makeRegExpStringIterator(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* regexp,
    const proto::ProtoObject* strObj)
{
    if (!ctx || !regexp || regexp == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* iter = ctx->newObject(true);
    iter = iter->setAttribute(ctx, JSSymbols::iterRe(ctx),   regexp);
    iter = iter->setAttribute(ctx, JSSymbols::iterStr(ctx),  strObj ? strObj : PROTO_NONE);
    iter = iter->setAttribute(ctx, JSSymbols::iterDone(ctx), PROTO_FALSE);

    const proto::ProtoObject* nextFn = ctx->fromMethod(nullptr, regexpStringIteratorNext);
    iter = iter->setAttribute(ctx, JSSymbols::next(ctx), nextFn);

    // Make the iterator itself iterable (Symbol.iterator returns this).
    const proto::ProtoString* symIterKey = ctx->fromUTF8String("Symbol.iterator")->asString(ctx);
    if (symIterKey) {
        iter = iter->setAttribute(ctx, symIterKey, iter);
    }

    return iter;
}

const proto::ProtoObject* regexpSymbolMatchAll(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || !args || args->getSize(ctx) == 0) return PROTO_NONE;

    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    std::string str = objToStrLocal(ctx, strObj);

    // Extract pattern and flags from the regexp object.
    std::string patternStr = objToStrLocal(ctx, self->getAttribute(ctx, JSSymbols::source(ctx), false));
    std::string flagsStr   = objToStrLocal(ctx, self->getAttribute(ctx, JSSymbols::flags(ctx),  false));

    // Ensure the 'g' flag is set — matchAll requires a global regexp.
    if (flagsStr.find('g') == std::string::npos) flagsStr += 'g';

    void* opaque = nullptr;
    if (JSContextWrapper::current()) opaque = JSContextWrapper::current()->getJSContext();

    int re_flags = parseFlagsLocal(flagsStr);
    int bc_len;
    char errmsg[128];
    uint8_t* bc = lre_compile(&bc_len, errmsg, sizeof(errmsg),
                               patternStr.c_str(), patternStr.size(), re_flags, opaque);
    if (!bc) return PROTO_NONE;

    // Build a fresh clone of the regexp with the guaranteed-global flags.
    const proto::ProtoObject* clone = ctx->newObject(true);
    clone = clone->setAttribute(ctx, JSSymbols::reBytecode(ctx),
        ctx->fromBuffer(static_cast<unsigned long>(bc_len), reinterpret_cast<char*>(bc), true));
    clone = clone->setAttribute(ctx, JSSymbols::source(ctx),    ctx->fromUTF8String(patternStr.c_str()));
    clone = clone->setAttribute(ctx, JSSymbols::flags(ctx),     ctx->fromUTF8String(flagsStr.c_str()));
    clone = clone->setAttribute(ctx, JSSymbols::lastIndex(ctx), ctx->fromInteger(0));
    clone = clone->setAttribute(ctx, JSSymbols::global(ctx),    PROTO_TRUE);
    free(bc);

    const proto::ProtoObject* strProtoObj = ctx->fromUTF8String(str.c_str());
    return makeRegExpStringIterator(ctx, clone, strProtoObj);
}

} // namespace protojs
