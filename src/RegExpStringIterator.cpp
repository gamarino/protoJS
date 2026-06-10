#include "RegExpStringIterator.h"
#include "RegExpPrototype.h"
#include "ArrayPrototype.h"
#include "IteratorPrototype.h"
#include "JSSymbols.h"
#include "JSContext.h"
#include "headers/protoCore.h"
extern "C" {
#include "libregexp.h"
}
#include <cstring>
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
 * %IteratorPrototype%[@@iterator] — returns this.  ECMA-262 §27.1.2.1.
 * Required so that `Array.from(it)` and `for (const x of it)` can pass
 * an iterator instance to GetIterator without throwing.  Pre-fix the
 * iterator stored ITSELF (a non-callable object) as the value of
 * Symbol.iterator, so Array.from's `Call(iterFn, src)` returned
 * undefined and the array came out empty.
 */
static const proto::ProtoObject* iteratorReturnSelf(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return self;
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

// %RegExpStringIteratorPrototype% — shared parent per §22.2.7.
// Carries next, [@@iterator] returning this, and
// [@@toStringTag] = "RegExp String Iterator".  Inherits from
// %IteratorPrototype% so [@@toStringTag] cascade ends at "Iterator".
static const proto::ProtoObject* s_regexpStringIteratorProto = nullptr;

static const proto::ProtoObject* getRegExpStringIteratorProto(proto::ProtoContext* ctx) {
    if (s_regexpStringIteratorProto) return s_regexpStringIteratorProto;
    const proto::ProtoObject* iterProto = getIteratorPrototype(ctx);
    const proto::ProtoObject* parent = iterProto ? iterProto
        : (ctx->space ? ctx->space->objectPrototype : nullptr);
    const proto::ProtoObject* proto = parent
        ? parent->newChild(ctx, true) : ctx->newObject(true);
    if (!proto) return nullptr;

    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    if (nextKey && ctx->space && ctx->space->methodPrototype) {
        const proto::ProtoObject* wrapper =
            ctx->space->methodPrototype->newChild(ctx, true);
        if (wrapper) {
            const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
            if (nfk) wrapper = wrapper->setAttribute(ctx, nfk,
                ctx->fromMethod(nullptr, regexpStringIteratorNext));
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            if (lk) {
                wrapper = wrapper->setAttribute(ctx, lk, ctx->fromInteger(0LL));
                const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
                if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
            }
            const proto::ProtoString* nk = JSSymbols::name(ctx);
            if (nk) {
                wrapper = wrapper->setAttribute(ctx, nk, ctx->fromUTF8String("next"));
                const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
                if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
            }
            proto = proto->setAttribute(ctx, nextKey, wrapper);
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_next__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
            if (pdnk) proto = proto->setAttribute(ctx, pdnk, ctx->fromInteger(0x3LL));
        }
    }

    const proto::ProtoString* tagUser = JSSymbols::symbolToStringTag(ctx);
    if (tagUser) {
        proto = proto->setAttribute(ctx, tagUser,
            ctx->fromUTF8String("RegExp String Iterator"));
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
    if (hnwK) proto = proto->setAttribute(ctx, hnwK, PROTO_TRUE);

    s_regexpStringIteratorProto = proto;
    return proto;
}

const proto::ProtoObject* makeRegExpStringIterator(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* regexp,
    const proto::ProtoObject* strObj)
{
    if (!ctx || !regexp || regexp == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* protoParent = getRegExpStringIteratorProto(ctx);
    const proto::ProtoObject* iter = protoParent
        ? protoParent->newChild(ctx, true) : ctx->newObject(true);
    iter = iter->setAttribute(ctx, JSSymbols::iterRe(ctx),   regexp);
    iter = iter->setAttribute(ctx, JSSymbols::iterStr(ctx),  strObj ? strObj : PROTO_NONE);
    iter = iter->setAttribute(ctx, JSSymbols::iterDone(ctx), PROTO_FALSE);

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
    // lre_compile returns a malloc-allocated buffer; ProtoByteBuffer's
    // finalize uses delete[].  Copy into a new[]-allocated buffer so the
    // Cell can free it cleanly; see the analogous fix in
    // RegExpPrototype.cpp::regexpConstructor.
    char* bcCopy = new char[bc_len];
    std::memcpy(bcCopy, bc, static_cast<size_t>(bc_len));
    free(bc);
    const proto::ProtoObject* clone = ctx->newObject(true);
    clone = clone->setAttribute(ctx, JSSymbols::reBytecode(ctx),
        ctx->fromBuffer(static_cast<unsigned long>(bc_len), bcCopy, true));
    clone = clone->setAttribute(ctx, JSSymbols::source(ctx),    ctx->fromUTF8String(patternStr.c_str()));
    clone = clone->setAttribute(ctx, JSSymbols::flags(ctx),     ctx->fromUTF8String(flagsStr.c_str()));
    clone = clone->setAttribute(ctx, JSSymbols::lastIndex(ctx), ctx->fromInteger(0));
    clone = clone->setAttribute(ctx, JSSymbols::global(ctx),    PROTO_TRUE);

    const proto::ProtoObject* strProtoObj = ctx->fromUTF8String(str.c_str());
    return makeRegExpStringIterator(ctx, clone, strProtoObj);
}

} // namespace protojs
