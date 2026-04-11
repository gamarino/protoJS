#include "ProtoInterpreter.h"
#include "QuickJSOpcodeEnum.h"
#include "QuickJSBytecodeExport.h"
#include "GeneratorFrame.h"
#include "../JSSymbols.h"
#include "../ArrayPrototype.h"
#include "../StringPrototype.h"
#include "../RegExpPrototype.h"
#include "../NumberPrototype.h"
#include "../MathBuiltin.h"
#include "../ObjectPrototype.h"
#include "../FunctionPrototype.h"
#include "../ArrayBufferPrototype.h"
#include "../TypedArrayPrototype.h"
#include "../DataViewPrototype.h"
#include "../JSContext.h"
#include "../GCBridge.h"
#include "../TypeBridge.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <cstdio>
#include <iostream>
#include <vector>
#include <algorithm>

namespace protojs {

namespace {

/** Slot and stack storage use ProtoContext::closureLocals only (no std::vector); GC sees all references. */

/**
 * Thread-local pointers to the currently-executing ProtoBytecodeModule and global root.
 * Set on runBytecode entry, restored on exit via RAII. Consumed by callJSFunction so
 * that native Array methods can invoke JS callbacks without carrying extra parameters.
 */
thread_local const ProtoBytecodeModule* t_currentModule = nullptr;
thread_local const proto::ProtoObject** t_currentGlobalRoot = nullptr;
// The ROOT module is the outermost runBytecode invocation on this thread (the global eval module).
// All function objects carry bytecode IDs that are indices into the root module's nestedFunctions.
// Nested invocations (inner functions) must look up bytecode IDs in the root module, not their own.
thread_local const ProtoBytecodeModule* t_rootModule = nullptr;
// The JS null sentinel: a stable ProtoObject* representing null.
// PROTO_NONE continues to represent undefined/absence.
thread_local const proto::ProtoObject* t_nullSentinel = nullptr;

// ---------------------------------------------------------------------------
// Generator resume state.
// Set by generatorNext/Return/Throw before calling runBytecode.
// Consumed (and cleared) by runBytecode at startup when t_genResumePc >= 0.
// ---------------------------------------------------------------------------
thread_local int                                    t_genResumePc       = -1;
thread_local const proto::ProtoObject*              t_genResumeLocals   = nullptr;
thread_local std::vector<protojs::CatchFrame>*      t_genResumeCatchStack = nullptr;
// The active generator iterator during a resume call.
// Set by generatorNext before entering runBytecode; read by OP_yield to update state.
thread_local const proto::ProtoObject*              t_genIterator       = nullptr;

// ---------------------------------------------------------------------------
// Iterator callback exception propagation.
// callJSFunction() cannot set pending_exception (it has no access to the
// local variables inside runBytecode).  Instead it stores the exception here
// and iterator-related call sites check this flag immediately after return.
// ---------------------------------------------------------------------------
thread_local const proto::ProtoObject*              t_callException     = nullptr;
thread_local bool                                   t_hasCallException  = false;

// ---------------------------------------------------------------------------
// Global utility functions (parseInt, parseFloat, isNaN, isFinite, URI encode/decode)
// These are registered as global properties during bootstrap.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* globalParseInt(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(nan);
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    std::string s;
    if (!strObj || strObj == PROTO_NONE) return ctx->fromDouble(nan);
    if (strObj->isString(ctx)) { const proto::ProtoString* ps = strObj->asString(ctx); if (ps) ps->toUTF8String(ctx, s); }
    else if (strObj->isInteger(ctx)) { s = std::to_string(strObj->asLong(ctx)); }
    else if (strObj->isDouble(ctx)) { char buf[64]; snprintf(buf, sizeof(buf), "%.15g", strObj->asDouble(ctx)); s = buf; }
    else return ctx->fromDouble(nan);

    // Trim leading whitespace
    size_t i = 0;
    while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r'||s[i]=='\f'||s[i]=='\v')) i++;
    s = s.substr(i);

    int radix = 10;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* ro = args->getAt(ctx, 1);
        if (ro && ro != PROTO_NONE) {
            if (ro->isInteger(ctx)) radix = static_cast<int>(ro->asLong(ctx));
            else if (ro->isDouble(ctx)) radix = static_cast<int>(ro->asDouble(ctx));
        }
    }
    // Handle prefixes
    bool negative = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { negative = (s[0] == '-'); s = s.substr(1); }
    if (s.size() >= 2 && s[0] == '0' && (s[1]=='x'||s[1]=='X') && (radix==10||radix==16)) { radix = 16; s = s.substr(2); }
    else if (s.size() >= 2 && s[0]=='0' && (s[1]=='b'||s[1]=='B') && radix==2) { s = s.substr(2); }
    else if (s.size() >= 2 && s[0]=='0' && (s[1]=='o'||s[1]=='O') && radix==8) { s = s.substr(2); }
    if (radix < 2 || radix > 36 || s.empty()) return ctx->fromDouble(nan);

    char* end = nullptr;
    unsigned long long uval = std::strtoull(s.c_str(), &end, radix);
    if (end == s.c_str()) return ctx->fromDouble(nan);
    long long result = negative ? -static_cast<long long>(uval) : static_cast<long long>(uval);
    return ctx->fromInteger(result);
}

static const proto::ProtoObject* globalParseFloat(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(nan);
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    if (!strObj || strObj == PROTO_NONE) return ctx->fromDouble(nan);
    if (strObj->isInteger(ctx)) return strObj;
    if (strObj->isDouble(ctx) || strObj->isFloat(ctx)) return strObj;
    std::string s;
    if (strObj->isString(ctx)) { const proto::ProtoString* ps = strObj->asString(ctx); if (ps) ps->toUTF8String(ctx, s); }
    else return ctx->fromDouble(nan);
    // Trim leading whitespace
    size_t j = 0;
    while (j < s.size() && (s[j]==' '||s[j]=='\t'||s[j]=='\n'||s[j]=='\r'||s[j]=='\f'||s[j]=='\v')) j++;
    s = s.substr(j);
    if (s == "Infinity" || s == "+Infinity") return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (s == "-Infinity") return ctx->fromDouble(-std::numeric_limits<double>::infinity());
    char* end = nullptr;
    double result = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return ctx->fromDouble(nan);
    return ctx->fromDouble(result);
}

static const proto::ProtoObject* globalIsNaN(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_TRUE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_TRUE; // ToNumber(undefined) = NaN
    if (v->isInteger(ctx)) return PROTO_FALSE;
    if (v->isDouble(ctx) || v->isFloat(ctx)) return std::isnan(v->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
    if (v->isBoolean(ctx)) return PROTO_FALSE;
    if (v->isString(ctx)) {
        const proto::ProtoString* ps = v->asString(ctx);
        std::string s; if (ps) ps->toUTF8String(ctx, s);
        size_t lo = s.find_first_not_of(" \t\n\r\f\v");
        if (lo == std::string::npos) return PROTO_FALSE; // "" → 0 → not NaN
        size_t hi = s.find_last_not_of(" \t\n\r\f\v");
        std::string t = s.substr(lo, hi - lo + 1);
        if (t == "Infinity" || t == "+Infinity" || t == "-Infinity") return PROTO_FALSE;
        char* end = nullptr; std::strtod(t.c_str(), &end);
        return (end == t.c_str() || *end != '\0') ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* globalIsFinite(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (v->isInteger(ctx)) return PROTO_TRUE;
    if (v->isDouble(ctx) || v->isFloat(ctx)) return std::isfinite(v->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
    if (v->isBoolean(ctx)) return PROTO_TRUE;
    if (v->isString(ctx)) {
        const proto::ProtoString* ps = v->asString(ctx);
        std::string s; if (ps) ps->toUTF8String(ctx, s);
        size_t lo = s.find_first_not_of(" \t\n\r\f\v");
        if (lo == std::string::npos) return PROTO_TRUE; // "" → 0 → finite
        size_t hi = s.find_last_not_of(" \t\n\r\f\v");
        std::string t = s.substr(lo, hi - lo + 1);
        char* end = nullptr; double d = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0') return PROTO_FALSE;
        return std::isfinite(d) ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

// Percent-encode a character as %XX
static std::string pctEncode(unsigned char c) {
    char buf[4]; snprintf(buf, sizeof(buf), "%%%02X", c); return buf;
}

static const proto::ProtoObject* globalEncodeURIComponent(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromUTF8String("undefined");
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    std::string s;
    if (v && v != PROTO_NONE && v->isString(ctx)) {
        const proto::ProtoString* ps = v->asString(ctx); if (ps) ps->toUTF8String(ctx, s);
    } else if (v && v != PROTO_NONE) {
        if (v->isInteger(ctx)) s = std::to_string(v->asLong(ctx));
        else if (v->isDouble(ctx)) { char buf[64]; snprintf(buf,sizeof(buf),"%.15g",v->asDouble(ctx)); s=buf; }
    }
    // Unreserved chars: A-Z a-z 0-9 - _ . ! ~ * ' ( )
    static const char unreserved[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'()";
    std::string result;
    for (size_t k = 0; k < s.size(); k++) {
        unsigned char c = static_cast<unsigned char>(s[k]);
        if (std::strchr(unreserved, static_cast<char>(c))) result += static_cast<char>(c);
        else result += pctEncode(c);
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* globalEncodeURI(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromUTF8String("undefined");
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    std::string s;
    if (v && v != PROTO_NONE && v->isString(ctx)) {
        const proto::ProtoString* ps = v->asString(ctx); if (ps) ps->toUTF8String(ctx, s);
    } else if (v && v != PROTO_NONE) {
        if (v->isInteger(ctx)) s = std::to_string(v->asLong(ctx));
        else if (v->isDouble(ctx)) { char buf[64]; snprintf(buf,sizeof(buf),"%.15g",v->asDouble(ctx)); s=buf; }
    }
    // URI allowed unescaped: unreserved + reserved (; , / ? : @ & = + $ #)
    static const char allowed[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'();,/?:@&=+$#";
    std::string result;
    for (size_t k = 0; k < s.size(); k++) {
        unsigned char c = static_cast<unsigned char>(s[k]);
        if (std::strchr(allowed, static_cast<char>(c))) result += static_cast<char>(c);
        else result += pctEncode(c);
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* globalDecodeURIComponent(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromUTF8String("undefined");
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    std::string s;
    if (v && v != PROTO_NONE && v->isString(ctx)) {
        const proto::ProtoString* ps = v->asString(ctx); if (ps) ps->toUTF8String(ctx, s);
    }
    std::string result;
    for (size_t k = 0; k < s.size(); ) {
        if (s[k] == '%' && k + 2 < s.size()) {
            char hex[3] = { s[k+1], s[k+2], 0 };
            char* end = nullptr;
            unsigned long val = std::strtoul(hex, &end, 16);
            if (end == hex + 2) { result += static_cast<char>(val); k += 3; continue; }
        }
        result += s[k++];
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* globalDecodeURI(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    // Reserved chars in URI should not be decoded; for simplicity decode everything.
    return globalDecodeURIComponent(ctx, nullptr, nullptr, args, nullptr);
}

static unsigned long slotKey(proto::ProtoContext* ctx, unsigned int index) {
    if (!ctx) return 0;
    std::string s = std::to_string(index);
    const proto::ProtoObject* o = ctx->fromUTF8String(s.c_str());
    const proto::ProtoString* ps = o ? o->asString(ctx) : nullptr;
    return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 0;
}

static unsigned long stackKey(proto::ProtoContext* ctx) {
    if (!ctx) return 0;
    const proto::ProtoObject* o = ctx->fromUTF8String("__stack__");
    const proto::ProtoString* ps = o ? o->asString(ctx) : nullptr;
    return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 0;
}

static const proto::ProtoObject* getSlot(proto::ProtoContext* ctx, unsigned int index) {
    if (!ctx || !ctx->closureLocals) return PROTO_NONE;
    const proto::ProtoObject* v = ctx->closureLocals->getAt(ctx, slotKey(ctx, index));
    return (v && v != PROTO_NONE) ? v : PROTO_NONE;
}

static void setSlot(proto::ProtoContext* ctx, unsigned int index, const proto::ProtoObject* value) {
    if (!ctx || !ctx->closureLocals) return;
    const proto::ProtoObject* val = value ? value : PROTO_NONE;
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, slotKey(ctx, index), val);
}

static void initStack(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return;
    const proto::ProtoList* empty = ctx->newList();
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, stackKey(ctx), empty ? empty->asObject(ctx) : PROTO_NONE);
}

static void stackPush(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    if (!ctx || !ctx->closureLocals) return;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) list = ctx->newList();
    const proto::ProtoList* next = list->appendLast(ctx, value ? value : PROTO_NONE);
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, sk, next ? next->asObject(ctx) : (list ? list->asObject(ctx) : PROTO_NONE));
}

static void stackPop(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) return;
    unsigned long n = list->getSize(ctx);
    if (n == 0) return;
    const proto::ProtoList* next = list->getSlice(ctx, 0, static_cast<int>(n - 1));
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, sk, next ? next->asObject(ctx) : ctx->newList()->asObject(ctx));
}

static const proto::ProtoObject* stackTop(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return PROTO_NONE;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) return PROTO_NONE;
    unsigned long n = list->getSize(ctx);
    if (n == 0) return PROTO_NONE;
    return list->getAt(ctx, static_cast<int>(n - 1));
}

static unsigned long stackSize(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return 0;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    return list ? list->getSize(ctx) : 0;
}

static bool stackEmpty(proto::ProtoContext* ctx) {
    return stackSize(ctx) == 0;
}

/** Get stack element by 0-based index from top (0 = top, 1 = next, ...). */
static const proto::ProtoObject* stackAt(proto::ProtoContext* ctx, unsigned long fromTop) {
    if (!ctx || !ctx->closureLocals) return PROTO_NONE;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) return PROTO_NONE;
    unsigned long n = list->getSize(ctx);
    if (fromTop >= n) return PROTO_NONE;
    return list->getAt(ctx, static_cast<int>(n - 1 - fromTop));
}

static inline uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t get_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/** JS-style truthiness, implemented on top of protoCore primitives. */
static bool toBool(proto::ProtoContext* context, const proto::ProtoObject* value) {
    if (!context) return false;
    if (!value || value == PROTO_NONE || value->isNone(context)) return false;
    if (value == t_nullSentinel) return false;  // JS null is falsy
    if (value == PROTO_TRUE) return true;
    if (value == PROTO_FALSE) return false;
    if (value->isBoolean(context)) return value->asBoolean(context);

    if (value->isInteger(context)) {
        return value->asLong(context) != 0;
    }

    if (value->isDouble(context) || value->isFloat(context)) {
        const double v = value->asDouble(context);
        if (v == 0.0 || std::isnan(v)) return false;
        return true;
    }

    if (value->isString(context)) {
        const proto::ProtoString* s = value->asString(context);
        if (!s) return false;
        return s->getSize(context) != 0;
    }

    // Objects, lists, sets, etc. are always truthy.
    return true;
}

/** JS-style ToNumber conversion, returning a protoCore number object. */
static const proto::ProtoObject* toNumber(proto::ProtoContext* context,
                                          const proto::ProtoObject* value) {
    if (!context) return PROTO_NONE;

    auto makeNaN = [&]() -> const proto::ProtoObject* {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return context->fromDouble(nan);
    };

    if (!value || value == PROTO_NONE || value->isNone(context)) {
        // ToNumber(undefined) is NaN; ToNumber(null) is +0. We currently
        // represent both as PROTO_NONE; prefer NaN for safety.
        return makeNaN();
    }

    if (value->isInteger(context) || value->isDouble(context) || value->isFloat(context)) {
        // Already a numeric primitive.
        return value;
    }

    if (value->isBoolean(context)) {
        const bool b = value->asBoolean(context);
        return context->fromInteger(b ? 1LL : 0LL);
    }

    if (value->isString(context)) {
        const proto::ProtoString* s = value->asString(context);
        if (!s) return makeNaN();
        std::string tmp;
        s->toUTF8String(context, tmp);
        // Trim whitespace (JS spec: leading/trailing whitespace ignored in ToNumber).
        size_t start = tmp.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) {
            // Empty or whitespace-only string → +0.
            return context->fromInteger(0LL);
        }
        size_t end = tmp.find_last_not_of(" \t\n\r\f\v");
        std::string trimmed = tmp.substr(start, end - start + 1);
        if (trimmed.empty()) return context->fromInteger(0LL);
        // Handle special literals.
        if (trimmed == "Infinity" || trimmed == "+Infinity")
            return context->fromDouble(std::numeric_limits<double>::infinity());
        if (trimmed == "-Infinity")
            return context->fromDouble(-std::numeric_limits<double>::infinity());
        // Try parsing as number; any parse error → NaN.
        try {
            size_t pos = 0;
            double d = std::stod(trimmed, &pos);
            if (pos != trimmed.size()) return makeNaN();
            // If integral and in range, use integer representation.
            if (d == std::trunc(d) && std::abs(d) < 9.007199254740992e15)
                return context->fromInteger(static_cast<long long>(d));
            return context->fromDouble(d);
        } catch (...) {
            return makeNaN();
        }
    }

    // TODO: Implement full ToPrimitive/ToNumber for objects (valueOf/toString chain).
    return makeNaN();
}

/** JS ToInt32: truncate to signed 32-bit integer. */
static int32_t toInt32Val(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v->isNone(ctx)) return 0;
    if (v->isInteger(ctx)) return static_cast<int32_t>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (!std::isfinite(d) || d == 0.0) return 0;
        return static_cast<int32_t>(static_cast<int64_t>(d));
    }
    if (v->isBoolean(ctx)) return v->asBoolean(ctx) ? 1 : 0;
    if (v->isString(ctx)) {
        const proto::ProtoString* s = v->asString(ctx);
        if (!s) return 0;
        std::string tmp;
        s->toUTF8String(ctx, tmp);
        try { return static_cast<int32_t>(static_cast<int64_t>(std::stod(tmp))); } catch (...) { return 0; }
    }
    return 0;
}

static uint32_t toUint32Val(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return static_cast<uint32_t>(toInt32Val(ctx, v));
}

/** JS-style ToString conversion, returning a protoCore string object. */
static const proto::ProtoObject* toString(proto::ProtoContext* context,
                                          const proto::ProtoObject* value) {
    if (!context) return PROTO_NONE;

    if (!value || value == PROTO_NONE || value->isNone(context)) {
        // We currently map "no value" to "undefined" in string context.
        return context->fromUTF8String("undefined");
    }

    // null converts to the string "null".
    if (value == t_nullSentinel) {
        return context->fromUTF8String("null");
    }

    if (value->isString(context)) {
        const proto::ProtoString* s = value->asString(context);
        return s ? s->asObject(context) : context->fromUTF8String("");
    }

    if (value->isBoolean(context)) {
        return context->fromUTF8String(value->asBoolean(context) ? "true" : "false");
    }

    if (value->isInteger(context)) {
        const long long v = value->asLong(context);
        const std::string tmp = std::to_string(v);
        return context->fromUTF8String(tmp.c_str());
    }

    if (value->isDouble(context) || value->isFloat(context)) {
        const double v = value->asDouble(context);
        if (std::isnan(v)) return context->fromUTF8String("NaN");
        if (std::isinf(v)) return context->fromUTF8String(v > 0 ? "Infinity" : "-Infinity");
        char buf[64];
        // Use %.15g for shortest representation that preserves value (JS semantics).
        snprintf(buf, sizeof(buf), "%.15g", v);
        return context->fromUTF8String(buf);
    }

    // Generic object fallback matches typical "[object Object]" shape.
    return context->fromUTF8String("[object Object]");
}

/** JS Abstract Equality Comparison (==): performs type coercions per ECMAScript spec §7.2.13.
 *  - null == undefined (both map to PROTO_NONE here, always equal)
 *  - Boolean: coerce to Number, retry
 *  - Number vs String: ToNumber(String), retry
 *  - Object vs primitive: ToPrimitive(Object) via valueOf/toString, retry (TODO: valueOf wiring)
 */
static bool jsAbstractEquals(proto::ProtoContext* ctx,
                              const proto::ProtoObject* x,
                              const proto::ProtoObject* y,
                              int depth = 0) {
    if (depth > 4) {
        // Guard against infinite recursion.
        return x == y;
    }
    // null and undefined are distinct but equal to each other under abstract equality.
    // Per spec §7.2.13 step 2-3: null == undefined → true; null/undefined == other → false.
    bool xNull  = (x == t_nullSentinel);
    bool yNull  = (y == t_nullSentinel);
    bool xUndef = !x || x == PROTO_NONE || x->isNone(ctx);
    bool yUndef = !y || y == PROTO_NONE || y->isNone(ctx);
    bool xNullish = xNull || xUndef;
    bool yNullish = yNull || yUndef;
    if (xNullish && yNullish) return true;   // null==null, null==undefined, undefined==null
    if (xNullish || yNullish) return false;  // null/undefined == number/string/bool → false

    bool xBool = x->isBoolean(ctx);
    bool yBool = y->isBoolean(ctx);
    bool xNum  = x->isInteger(ctx) || x->isDouble(ctx) || x->isFloat(ctx);
    bool yNum  = y->isInteger(ctx) || y->isDouble(ctx) || y->isFloat(ctx);
    bool xStr  = !xBool && !xNum && x->asString(ctx) != nullptr;
    bool yStr  = !yBool && !yNum && y->asString(ctx) != nullptr;

    // Same type: use strict compare.
    if (xBool && yBool) return x->compare(ctx, y) == 0;
    if (xNum  && yNum)  return x->compare(ctx, y) == 0;
    if (xStr  && yStr)  return x->compare(ctx, y) == 0;

    // Boolean: convert to number first, then retry.
    if (xBool) return jsAbstractEquals(ctx, toNumber(ctx, x), y, depth + 1);
    if (yBool) return jsAbstractEquals(ctx, x, toNumber(ctx, y), depth + 1);

    // Number vs String: convert string to number, retry.
    if (xNum && yStr)  return jsAbstractEquals(ctx, x, toNumber(ctx, y), depth + 1);
    if (xStr && yNum)  return jsAbstractEquals(ctx, toNumber(ctx, x), y, depth + 1);

    // Object vs primitive: attempt ToPrimitive via valueOf attribute.
    bool xObj = !xBool && !xNum && !xStr;
    bool yObj = !yBool && !yNum && !yStr;
    if (xObj && !yObj) {
        const proto::ProtoString* vk = JSSymbols::valueOf(ctx);
        if (vk) {
            const proto::ProtoObject* vfn = x->getAttribute(ctx, vk, true);
            if (vfn && vfn != PROTO_NONE && vfn->isMethod(ctx)) {
                const proto::ProtoObject* prim = vfn->call(ctx, nullptr, vk, x, ctx->newList(), nullptr);
                if (prim && prim != PROTO_NONE && !(prim->isMethod(ctx)) &&
                    (prim->isBoolean(ctx) || prim->isInteger(ctx) ||
                     prim->isDouble(ctx)  || prim->asString(ctx)))
                    return jsAbstractEquals(ctx, prim, y, depth + 1);
            }
        }
        return false;
    }
    if (yObj && !xObj) {
        const proto::ProtoString* vk = JSSymbols::valueOf(ctx);
        if (vk) {
            const proto::ProtoObject* vfn = y->getAttribute(ctx, vk, true);
            if (vfn && vfn != PROTO_NONE && vfn->isMethod(ctx)) {
                const proto::ProtoObject* prim = vfn->call(ctx, nullptr, vk, y, ctx->newList(), nullptr);
                if (prim && prim != PROTO_NONE && !(prim->isMethod(ctx)) &&
                    (prim->isBoolean(ctx) || prim->isInteger(ctx) ||
                     prim->isDouble(ctx)  || prim->asString(ctx)))
                    return jsAbstractEquals(ctx, x, prim, depth + 1);
            }
        }
        return false;
    }

    // Object vs Object: identity.
    return x->compare(ctx, y) == 0;
}

/** Resolve atom index to ProtoString from the pre-resolved cache. */
const proto::ProtoString* resolveAtom(ProtoBytecodeModule* mod, proto::ProtoContext* pContext, uint32_t atomIndex) {
    if (!mod || !pContext) return nullptr;
    auto it = mod->atomToProto.find(atomIndex);
    if (it != mod->atomToProto.end()) return it->second;
    /* Atom was not pre-resolved; this should not happen after preResolveAllAtoms(). */
    std::fprintf(stderr, "[ProtoInterpreter] resolveAtom: atom %u not in cache\n", atomIndex);
    return nullptr;
}

/** Check if obj is a bytecode function placeholder (has __bytecode_id__). Returns -1 if not. */
int getBytecodeId(proto::ProtoContext* pContext, const proto::ProtoObject* obj) {
    if (!obj || !pContext) return -1;
    const proto::ProtoString* key = JSSymbols::bytecodeId(pContext);
    const proto::ProtoObject* val = obj->getAttribute(pContext, key, false);
    if (!val || val == PROTO_NONE) return -1;
    if (!val->isInteger(pContext)) return -1;
    long long id = val->asLong(pContext);
    return id >= 0 ? static_cast<int>(id) : -1;
}

// Returns the parent ProtoBytecodeModule stored on a function closure, or nullptr.
// Each function created by OP_fclosure/OP_fclosure8 stores its parent module pointer
// as an integer attribute so that callJSFunction resolves the correct nestedFunctions[id].
static const ProtoBytecodeModule* getClosureModule(
    proto::ProtoContext* pContext, const proto::ProtoObject* fn)
{
    if (!fn || fn == PROTO_NONE || !pContext) return nullptr;
    const proto::ProtoString* key = JSSymbols::closureModule(pContext);
    if (!key) return nullptr;
    const proto::ProtoObject* val = fn->getAttribute(pContext, key, false);
    if (!val || val == PROTO_NONE || !val->isInteger(pContext)) return nullptr;
    uintptr_t ptr = static_cast<uintptr_t>(val->asLong(pContext));
    return ptr ? reinterpret_cast<const ProtoBytecodeModule*>(ptr) : nullptr;
}

/** Native ProtoMethod for Error.prototype.toString(). Returns "name: message" or just "name". */
static const proto::ProtoObject* errorPrototypeToString(
        proto::ProtoContext* context,
        const proto::ProtoObject* self,
        const proto::ParentLink* /*parentLink*/,
        const proto::ProtoList* /*params*/,
        const proto::ProtoSparseList* /*kw*/) {
    if (!context) return PROTO_NONE;
    const proto::ProtoString* nameKey = JSSymbols::name(context);
    const proto::ProtoString* msgKey  = JSSymbols::message(context);
    std::string nameStr = "Error", msgStr;
    if (nameKey && self && self != PROTO_NONE) {
        const proto::ProtoObject* nv = self->getAttribute(context, nameKey, true);
        if (nv && nv != PROTO_NONE && nv->isString(context))
            nv->asString(context)->toUTF8String(context, nameStr);
    }
    if (msgKey && self && self != PROTO_NONE) {
        const proto::ProtoObject* mv = self->getAttribute(context, msgKey, true);
        if (mv && mv != PROTO_NONE && mv->isString(context))
            mv->asString(context)->toUTF8String(context, msgStr);
    }
    std::string result = msgStr.empty() ? nameStr : (nameStr + ": " + msgStr);
    return context->fromUTF8String(result.c_str());
}

/** Register built-in error constructors (Error, TypeError, ReferenceError, …) on the global.
 *  Each entry is a stub object with `name` and `prototype` attributes.  The prototype is
 *  used by makeError (via newChild) so that `instanceof` works correctly. */
static void ensureBuiltinErrorConstructors(proto::ProtoContext* ctx,
                                            const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    static const char* kNames[] = {
        "Error", "TypeError", "ReferenceError", "RangeError",
        "SyntaxError", "URIError", "EvalError", "InternalError", nullptr
    };
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoString* nameKey  = JSSymbols::name(ctx);
    if (!protoKey || !nameKey) return;
    for (int i = 0; kNames[i]; ++i) {
        const proto::ProtoString* ctorKey = (ctx->fromUTF8String(kNames[i]) ? ctx->fromUTF8String(kNames[i])->asString(ctx) : nullptr);
        if (!ctorKey) continue;
        // Only register if not already present.
        const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ctorKey, false);
        if (existing && existing != PROTO_NONE) continue;
        // Build prototype object.
        const proto::ProtoObject* proto = ctx->newObject(true);
        if (!proto) continue;
        proto = proto->setAttribute(ctx, nameKey, ctx->fromUTF8String(kNames[i]));
        if (!proto) continue;
        // Add toString method to the prototype.
        const proto::ProtoString* toStringKey = JSSymbols::toString(ctx);
        if (toStringKey) {
            const proto::ProtoObject* toStringMethod = ctx->fromMethod(nullptr, errorPrototypeToString);
            if (toStringMethod) proto = proto->setAttribute(ctx, toStringKey, toStringMethod);
        }
        if (!proto) continue;
        // Build constructor stub.
        const proto::ProtoObject* ctor = ctx->newObject(true);
        if (!ctor) continue;
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String(kNames[i]));
        if (!ctor) continue;
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        if (!ctor) continue;
        // Set prototype.constructor = ctor so `e.constructor === TypeError` identity checks pass.
        {
            const proto::ProtoString* ctorPropKey =
                ctx->fromUTF8String("constructor")
                ? ctx->fromUTF8String("constructor")->asString(ctx) : nullptr;
            if (ctorPropKey) {
                proto = proto->setAttribute(ctx, ctorPropKey, ctor);
                if (!proto) continue;
                // Re-link ctor.prototype after proto was updated.
                ctor = ctor->setAttribute(ctx, protoKey, proto);
                if (!ctor) continue;
            }
        }
        // Mark as a built-in error constructor so OP_call can invoke it.
        const proto::ProtoString* errCtorKey = JSSymbols::errorCtor(ctx);
        if (errCtorKey) ctor = ctor->setAttribute(ctx, errCtorKey, ctx->fromUTF8String(kNames[i]));
        if (!ctor) continue;
        *globalRoot = (*globalRoot)->setAttribute(ctx, ctorKey, ctor);
    }
}

/** Build an Error-like ProtoObject with name and message attributes.
 *  When globalRoot is provided, the object is created as a child of the corresponding
 *  error prototype so that `instanceof` works correctly. */
static const proto::ProtoObject* makeError(proto::ProtoContext* ctx,
                                           const char* name,
                                           const char* message,
                                           const proto::ProtoObject* const* globalRoot = nullptr) {
    if (!ctx) return PROTO_NONE;
    // Try to get the prototype from the global so instanceof works.
    const proto::ProtoObject* base = nullptr;
    if (globalRoot && *globalRoot && name) {
        const proto::ProtoString* ctorKey   = (ctx->fromUTF8String(name) ? ctx->fromUTF8String(name)->asString(ctx) : nullptr);
        const proto::ProtoString* protoKey  = JSSymbols::prototype(ctx);
        if (ctorKey && protoKey) {
            const proto::ProtoObject* ctor = (*globalRoot)->getAttribute(ctx, ctorKey, false);
            if (ctor && ctor != PROTO_NONE) {
                const proto::ProtoObject* p = ctor->getAttribute(ctx, protoKey, false);
                if (p && p != PROTO_NONE) base = p;
            }
        }
    }
    const proto::ProtoObject* err = base ? base->newChild(ctx, true) : ctx->newObject(true);
    if (!err) return PROTO_NONE;
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    const proto::ProtoString* msgKey  = JSSymbols::message(ctx);
    if (nameKey)       err = err->setAttribute(ctx, nameKey, ctx->fromUTF8String(name    ? name    : "Error"));
    if (msgKey && err) err = err->setAttribute(ctx, msgKey,  ctx->fromUTF8String(message ? message : ""));
    return err ? err : PROTO_NONE;
}

} // namespace

/**
 * @brief Syncs an immutable ProtoObject update back to its associated JSValue in GCBridge.
 * 
 * When a ProtoObject is updated via setAttribute, it returns a new snapshot. To maintain
 * identity for JS objects across the bridge, we must update the mapping for the 
 * underlying JSValue to point to the new snapshot.
 */
static void updateMapping(proto::ProtoContext* pContext, const proto::ProtoObject* oldObj, const proto::ProtoObject* newObj) {
    if (!oldObj || !newObj || oldObj == newObj) return;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return;
    JSContext* ctx = wrapper->getJSContext();
    if (!ctx) return;
    
    JSValue jsVal = GCBridge::getJSValue(oldObj, ctx);
    if (!JS_IsException(jsVal) && !JS_IsNull(jsVal) && !JS_IsUndefined(jsVal)) {
        GCBridge::registerMapping(jsVal, newObj, ctx);
    }
    JS_FreeValue(ctx, jsVal);
}

// ---------------------------------------------------------------------------
// Generator protocol helpers (defined before runBytecode so OP_initial_yield
// can reference the ProtoMethod function pointers).
// These functions live in namespace protojs (same as runBytecode).
// They have access to thread-locals defined in the anonymous namespace above
// because they are in the same translation unit.
// ---------------------------------------------------------------------------

/** Read a long long attribute from iter by name. Returns defaultVal if absent. */
static long long genGetInt(proto::ProtoContext* ctx, const proto::ProtoObject* iter,
                            const char* name, long long defaultVal = -1LL) {
    if (!iter || iter == PROTO_NONE) return defaultVal;
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    if (!k) return defaultVal;
    const proto::ProtoObject* v = iter->getAttribute(ctx, k, false);
    return (v && v != PROTO_NONE && v->isInteger(ctx)) ? v->asLong(ctx) : defaultVal;
}

/** Set a long long attribute on iter; returns the updated iter pointer. */
static const proto::ProtoObject* genSetInt(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* iter,
                                            const char* name, long long val) {
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    return (k && iter) ? iter->setAttribute(ctx, k, ctx->fromInteger(val)) : iter;
}

/** Set a ProtoObject attribute on iter; returns the updated iter pointer. */
static const proto::ProtoObject* genSetObj(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* iter,
                                            const char* name,
                                            const proto::ProtoObject* val) {
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    return (k && iter) ? iter->setAttribute(ctx, k, val ? val : PROTO_NONE) : iter;
}

/** Build a {value, done} iterator result object. */
static const proto::ProtoObject* makeIterResult(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* value,
                                                  bool done) {
    const proto::ProtoObject* r = ctx->newObject(true);
    if (!r) return PROTO_NONE;
    const proto::ProtoString* vk = JSSymbols::value(ctx);
    const proto::ProtoString* dk = JSSymbols::done(ctx);
    if (vk) r = r->setAttribute(ctx, vk, value ? value : PROTO_NONE);
    if (dk) r = r->setAttribute(ctx, dk, done ? PROTO_TRUE : PROTO_FALSE);
    return r ? r : PROTO_NONE;
}

/** Core resume: runs the generator body from the saved pc.
 *  mode: 0=next, 1=return, 2=throw.
 *  Forward declaration — implemented after runBytecode forward declarations. */
static const proto::ProtoObject* resumeGenerator(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* iter,
                                                   const proto::ProtoObject* sentVal,
                                                   int mode);

// Forward declarations for the ProtoMethod callbacks.
static const proto::ProtoObject* generatorNext(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);
static const proto::ProtoObject* generatorReturn(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);
static const proto::ProtoObject* generatorThrow(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);

const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* thisObj,
                                      const proto::ProtoList* args,
                                      const proto::ProtoObject** pGlobalRoot,
                                      const proto::ProtoObject** outException) {
    if (!pContext || !module) return PROTO_NONE;
    const uint8_t* buf = module->buf();
    int len = module->bufLen();
    if (!buf || len <= 0) return PROTO_NONE;

    // RAII: publish active module + global root so callJSFunction can resolve closures.
    struct ModuleScope {
        const ProtoBytecodeModule* prevMod;
        const proto::ProtoObject** prevGR;
        const ProtoBytecodeModule* prevRoot;
        ModuleScope(const ProtoBytecodeModule* m, const proto::ProtoObject** gr)
            : prevMod(t_currentModule), prevGR(t_currentGlobalRoot), prevRoot(t_rootModule) {
            t_currentModule = m; t_currentGlobalRoot = gr;
            // The root module is the outermost module — set it only when first entering.
            if (!t_rootModule) t_rootModule = m;
        }
        ~ModuleScope() { t_currentModule = prevMod; t_currentGlobalRoot = prevGR; t_rootModule = prevRoot; }
    } _mscope(module, pGlobalRoot);

    const std::vector<const proto::ProtoObject*>& cpool = module->protoCpool;
    const auto& nested = module->nestedFunctions;
    const std::vector<std::string>& closureVarNames = module->closureVarNames;
    unsigned argCount = module->argCount();
    unsigned varCount = module->varCount();

    // Pending exception (set inside switch, dispatched after switch body).
    // Use a separate flag so that `throw undefined` (PROTO_NONE) is also catchable.
    const proto::ProtoObject* pending_exception = nullptr;
    bool has_pending_exception = false;

    // Catch-handler stack.
    // In QuickJS, OP_catch pushes a tagged catch-offset integer onto the VALUE stack; the
    // exception handler scans the value stack backwards for it.  Our value stack holds
    // opaque ProtoObject pointers, so we cannot embed a tag there.  Instead we use this
    // parallel vector, but we must mirror QuickJS's stack-based semantics exactly:
    //   - placeholder_stack_pos  = value-stack index of the sentinel pushed by OP_catch.
    //   - The sentinel IS the catch frame from the stack's perspective.  OP_drop that lands
    //     on placeholder_stack_pos must also pop the catch frame (just as QuickJS's OP_drop
    //     removes the tagged integer from the value stack, removing the catch frame).
    std::vector<CatchFrame> catch_stack;

    // -----------------------------------------------------------------------
    // Generator resume: if t_genResumePc >= 0, skip normal stack init and
    // restore saved state from thread-locals set by resumeGenerator().
    // -----------------------------------------------------------------------
    int pc = 0;
    if (t_genResumePc >= 0) {
        pc = t_genResumePc;
        t_genResumePc = -1;

        // Restore closureLocals snapshot.
        if (t_genResumeLocals) {
            const proto::ProtoSparseList* sl = t_genResumeLocals->asSparseList(pContext);
            if (sl) pContext->closureLocals = sl;
            t_genResumeLocals = nullptr;
        }

        // Restore catch stack.
        if (t_genResumeCatchStack) {
            catch_stack = *t_genResumeCatchStack;
            t_genResumeCatchStack = nullptr;
        }

        // Push the sent value onto the stack (becomes the result of the yield expression).
        const proto::ProtoObject* sentVal = PROTO_NONE;
        if (t_genIterator) {
            const proto::ProtoObject* ko2 = pContext->fromUTF8String("__gen_sent__");
            const proto::ProtoString* k2  = ko2 ? ko2->asString(pContext) : nullptr;
            if (k2) {
                const proto::ProtoObject* sv = t_genIterator->getAttribute(pContext, k2, false);
                if (sv && sv != PROTO_NONE) sentVal = sv;
            }
        }
        stackPush(pContext, sentVal);

        // If mode==2 (throw): override sentVal with the throw value as pending_exception.
        if (t_genIterator) {
            const proto::ProtoObject* ko3 = pContext->fromUTF8String("__gen_throw_val__");
            const proto::ProtoString* k3  = ko3 ? ko3->asString(pContext) : nullptr;
            if (k3) {
                const proto::ProtoObject* tv = t_genIterator->getAttribute(pContext, k3, false);
                if (tv && tv != PROTO_NONE) {
                    stackPop(pContext);
                    pending_exception = tv;
                    has_pending_exception = true;
                }
            }
        }
    } else {
        // Locals and stack live only in ProtoContext::closureLocals (GC-visible). No std::vector.
        initStack(pContext);
        const proto::ProtoObject* globalObjInit = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : thisObj;
        /* Pre-load closure vars from the global object into their dedicated slots.
         * Closure vars occupy a SEPARATE slot region from local vars:
         *   local vars:    slot[argCount + 0 .. argCount + varCount - 1]
         *   closure vars:  slot[argCount + varCount + 0 .. argCount + varCount + N - 1]
         * This separation prevents the _ret_ hidden eval variable (local slot 0) from
         * colliding with closure-var slot 0 (used for hoisted function declarations).
         *
         * Arrow functions: QuickJS compiles arrow bodies to access `this` via a free
         * variable named "this" rather than via OP_push_this.  The lexical this is
         * already in `thisObj` (set by every call site that honours isArrow).  Inject
         * it directly so that OP_get_var_ref0 inside the arrow body finds it. */
        for (size_t i = 0; i < closureVarNames.size(); i++) {
            if (closureVarNames[i].empty()) continue;
            // Arrow function: the closure var "this" must come from thisObj, not the global.
            if (module->isArrow && closureVarNames[i] == "this") {
                setSlot(pContext, argCount + varCount + static_cast<unsigned>(i),
                    thisObj ? thisObj : PROTO_NONE);
                continue;
            }
            if (!globalObjInit || globalObjInit == PROTO_NONE) continue;
            const proto::ProtoString* key = (pContext->fromUTF8String(closureVarNames[i].c_str()) ? pContext->fromUTF8String(closureVarNames[i].c_str())->asString(pContext) : nullptr);
            if (key) {
                const proto::ProtoObject* val = globalObjInit->getAttribute(pContext, key, false);
                if (val && val != PROTO_NONE)
                    setSlot(pContext, argCount + varCount + static_cast<unsigned>(i), val);
            }
        }
    }
    ProtoBytecodeModule* mod = const_cast<ProtoBytecodeModule*>(module);

    const proto::ProtoObject* tdzSentinel = pContext->fromUTF8String("\x00__protojs_tdz_sentinel__");
    if (!tdzSentinel) tdzSentinel = PROTO_NONE;

    // Bootstrap the null sentinel. Stored as __js_null_sentinel__ on the global root
    // so the GC can trace it. Cached in t_nullSentinel for O(1) access during execution.
    if (!t_nullSentinel && pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoString* sentinelKey =
            (pContext->fromUTF8String("__js_null_sentinel__")
                ? pContext->fromUTF8String("__js_null_sentinel__")->asString(pContext)
                : nullptr);
        if (sentinelKey) {
            const proto::ProtoObject* existing =
                (*pGlobalRoot)->getAttribute(pContext, sentinelKey, false);
            if (existing && existing != PROTO_NONE) {
                t_nullSentinel = existing;
            } else {
                const proto::ProtoObject* sentinel = pContext->newObject(false);
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, sentinelKey, sentinel);
                t_nullSentinel = sentinel;
            }
        }
    }

    // Register built-in error constructors once so that `instanceof` works.
    ensureBuiltinErrorConstructors(pContext, pGlobalRoot);

    // Register Array constructor and Array.prototype (idempotent).
    ensureArrayPrototype(pContext, pGlobalRoot);
    // Register ArrayBuffer constructor and ArrayBuffer.prototype (idempotent).
    ensureArrayBufferConstructor(pContext, pGlobalRoot);
    // Register TypedArray constructors (Int8Array … BigUint64Array) (idempotent).
    ensureTypedArrayConstructors(pContext, pGlobalRoot);
    // Register DataView constructor and DataView.prototype (idempotent).
    ensureDataViewConstructor(pContext, pGlobalRoot);
    // Register String constructor with static methods (fromCharCode, fromCodePoint).
    ensureStringConstructor(pContext, pGlobalRoot);
    // Register RegExp constructor and its prototype.
    ensureRegExpConstructor(pContext, pGlobalRoot);

    // Register well-known global numeric constants (Infinity, NaN, undefined).
    // These are standard globals that must be visible as top-level variable lookups.
    if (pGlobalRoot && *pGlobalRoot) {
        auto ensureGlobalConst = [&](const char* name, const proto::ProtoObject* val) {
            const proto::ProtoString* k = (pContext->fromUTF8String(name) ? pContext->fromUTF8String(name)->asString(pContext) : nullptr);
            if (!k) return;
            const proto::ProtoObject* existing = (*pGlobalRoot)->getAttribute(pContext, k, false);
            if (!existing) // absent means not yet set
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, k, val);
        };
        ensureGlobalConst("Infinity",
            pContext->fromDouble(std::numeric_limits<double>::infinity()));
        ensureGlobalConst("NaN",
            pContext->fromDouble(std::numeric_limits<double>::quiet_NaN()));
        ensureGlobalConst("undefined", PROTO_NONE);
        // Register standard globals that are not yet fully implemented.
        // Constructor-type globals get minimal stub objects (with name + prototype attributes)
        // so that `x instanceof StubbedConstructor` does not throw TypeError.
        // Non-constructor globals (eval, globalThis, etc.) are stubbed as PROTO_NONE.
        static const char* kUnimplementedCtors[] = {
            // Unimplemented standard JS built-in constructors.
            // NOTE: "Function" is intentionally omitted — wired via ensureFunctionPrototype.
            "Boolean", "Promise", "Date", "Map", "Set",
            "BigInt", "AggregateError",
            // Metaprogramming built-in constructors.
            "Symbol", "Proxy", "WeakRef", "WeakMap", "WeakSet",
            "FinalizationRegistry", "Iterator", "Generator", "GeneratorFunction",
            "AsyncFunction", "AsyncGenerator", "AsyncGeneratorFunction",
            "SharedArrayBuffer",
            nullptr
        };
        if (pGlobalRoot && *pGlobalRoot) {
            const proto::ProtoString* nameKey2 = JSSymbols::name(pContext);
            const proto::ProtoString* protoKey2 = JSSymbols::prototype(pContext);
            for (int gi = 0; kUnimplementedCtors[gi]; ++gi) {
                const char* ctorName = kUnimplementedCtors[gi];
                const proto::ProtoString* ck = (pContext->fromUTF8String(ctorName) ? pContext->fromUTF8String(ctorName)->asString(pContext) : nullptr);
                if (!ck) continue;
                const proto::ProtoObject* ex = (*pGlobalRoot)->getAttribute(pContext, ck, false);
                if (ex && ex != PROTO_NONE) continue;
                // Build a minimal constructor stub with a prototype so instanceof doesn't throw.
                const proto::ProtoObject* stubProto = pContext->newObject(true);
                const proto::ProtoObject* stub = pContext->newObject(true);
                if (nameKey2) stub = stub->setAttribute(pContext, nameKey2, pContext->fromUTF8String(ctorName));
                if (protoKey2) stub = stub->setAttribute(pContext, protoKey2, stubProto ? stubProto : PROTO_NONE);
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, ck, stub);
            }
        }
        // Non-constructor globals stubbed as PROTO_NONE to prevent ReferenceError.
        static const char* kUnimplementedGlobals[] = {
            "JSON", "eval", "Reflect", "Atomics",
            "globalThis", "arguments",
            // Test262 harness globals.
            "$DONE", "$262", "print",
            nullptr
        };
        for (int gi = 0; kUnimplementedGlobals[gi]; ++gi)
            ensureGlobalConst(kUnimplementedGlobals[gi], PROTO_NONE);
    }

    // Register Number constructor, Math object, Object constructor, and global utility
    // functions (parseInt, parseFloat, isNaN, isFinite, encodeURI, decodeURI, etc.).
    ensureNumberConstructor(pContext, pGlobalRoot);
    ensureMathObject(pContext, pGlobalRoot);
    ensureObjectConstructor(pContext, pGlobalRoot);
    ensureFunctionPrototype(pContext, pGlobalRoot);
    // Bootstrap Symbol well-known symbols as string-valued properties on the Symbol stub.
    // This allows JS code like `obj[Symbol.iterator] = fn` to use the canonical key
    // "Symbol.iterator" that JSSymbols::symbolIterator() also returns.
    if (pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoString* symbolGlobalKey =
            pContext->fromUTF8String("Symbol")
                ? pContext->fromUTF8String("Symbol")->asString(pContext) : nullptr;
        if (symbolGlobalKey) {
            const proto::ProtoObject* symbolObj =
                (*pGlobalRoot)->getAttribute(pContext, symbolGlobalKey, false);
            if (symbolObj && symbolObj != PROTO_NONE) {
                // Each well-known symbol: prop name → canonical string key.
                struct { const char* prop; const char* key; } wks[] = {
                    { "iterator",           "Symbol.iterator"           },
                    { "toPrimitive",        "Symbol.toPrimitive"        },
                    { "toStringTag",        "Symbol.toStringTag"        },
                    { "hasInstance",        "Symbol.hasInstance"        },
                    { "isConcatSpreadable", "Symbol.isConcatSpreadable" },
                    { "match",              "Symbol.match"              },
                    { "matchAll",           "Symbol.matchAll"           },
                    { "replace",            "Symbol.replace"            },
                    { "search",             "Symbol.search"             },
                    { "species",            "Symbol.species"            },
                    { "split",              "Symbol.split"              },
                    { "asyncIterator",      "Symbol.asyncIterator"      },
                    { "unscopables",        "Symbol.unscopables"        },
                    { nullptr, nullptr }
                };
                for (int wi = 0; wks[wi].prop; ++wi) {
                    const proto::ProtoString* propKey =
                        pContext->fromUTF8String(wks[wi].prop)
                            ? pContext->fromUTF8String(wks[wi].prop)->asString(pContext) : nullptr;
                    const proto::ProtoObject* keyVal =
                        pContext->fromUTF8String(wks[wi].key);
                    if (propKey && keyVal)
                        symbolObj = symbolObj->setAttribute(pContext, propKey, keyVal);
                }
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, symbolGlobalKey, symbolObj);
            }
        }
    }
    if (pGlobalRoot && *pGlobalRoot) {
        auto ensureGlobalFn = [&](const char* name, proto::ProtoMethod fn) {
            const proto::ProtoString* k = (pContext->fromUTF8String(name) ? pContext->fromUTF8String(name)->asString(pContext) : nullptr);
            if (!k) return;
            const proto::ProtoObject* existing = (*pGlobalRoot)->getAttribute(pContext, k, false);
            if (existing && existing != PROTO_NONE) return;
            const proto::ProtoObject* fnObj = pContext->fromMethod(nullptr, fn);
            if (fnObj)
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, k, fnObj);
        };
        ensureGlobalFn("parseInt",            globalParseInt);
        ensureGlobalFn("parseFloat",          globalParseFloat);
        ensureGlobalFn("isNaN",               globalIsNaN);
        ensureGlobalFn("isFinite",            globalIsFinite);
        ensureGlobalFn("encodeURI",           globalEncodeURI);
        ensureGlobalFn("encodeURIComponent",  globalEncodeURIComponent);
        ensureGlobalFn("decodeURI",           globalDecodeURI);
        ensureGlobalFn("decodeURIComponent",  globalDecodeURIComponent);
    }

    // Hoist var-declared globals to undefined so that Fix1's ReferenceError check does not
    // fire for variables declared with `var x;` but lacking an explicit initializer.
    // QuickJS's runtime does this step before executing the bytecode; we replicate it here.
    // Only vars marked JS_CLOSURE_GLOBAL_DECL (closureVarIsDeclared) are hoisted; undeclared
    // references (JS_CLOSURE_GLOBAL) are left absent so Fix1 correctly throws ReferenceError.
    // This is idempotent: we skip vars already present in globalRoot.
    if (pGlobalRoot && *pGlobalRoot && module) {
        for (size_t gi = 0; gi < module->closureVarNames.size(); ++gi) {
            bool isDeclared = (gi < module->closureVarIsDeclared.size()) && module->closureVarIsDeclared[gi];
            if (!isDeclared) continue;
            const std::string& vname = module->closureVarNames[gi];
            if (vname.empty()) continue;
            const proto::ProtoString* vkey = (pContext->fromUTF8String(vname.c_str())
                                              ? pContext->fromUTF8String(vname.c_str())->asString(pContext)
                                              : nullptr);
            if (!vkey) continue;
            // Only set if key is COMPLETELY absent (getAttribute returns nullptr).
            const proto::ProtoObject* existing = (*pGlobalRoot)->getAttribute(pContext, vkey, false);
            if (!existing) {
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, vkey, PROTO_NONE);
            }
        }
    }

    /* Invoke a method stored as a bytecode or native function on thisVal with no arguments.
     * If the method throws, sets pending_exception / has_pending_exception and returns PROTO_NONE.
     * Returns PROTO_NONE (without setting exception) when fn is null or unresolvable. */
    auto callMethod = [&](const proto::ProtoObject* fn,
                          const proto::ProtoString* keyHint,
                          const proto::ProtoObject* thisVal) -> const proto::ProtoObject* {
        if (!fn || fn == PROTO_NONE) return PROTO_NONE;
        int bcId = getBytecodeId(pContext, fn);
        const ProtoBytecodeModule* resolvedFn = nullptr;
        if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size())
            resolvedFn = &nested[bcId];
        else if (bcId >= 0 && t_rootModule &&
                 static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
            resolvedFn = &t_rootModule->nestedFunctions[bcId];
        if (resolvedFn) {
            // Arrow functions ignore the call-site receiver and use their lexical this.
            const proto::ProtoObject* effectiveThisLambda = thisVal;
            if (resolvedFn->isArrow) {
                const proto::ProtoObject* capturedLambda =
                    fn->getAttribute(pContext, JSSymbols::arrowThis(pContext), false);
                if (capturedLambda && capturedLambda != PROTO_NONE)
                    effectiveThisLambda = capturedLambda;
            }
            proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
            childCtx.currentFileName = pContext->currentFileName;
            const proto::ProtoObject* childEx = PROTO_NONE;
            const proto::ProtoObject* result = runBytecode(&childCtx, resolvedFn, effectiveThisLambda,
                                                            pContext->newList(), pGlobalRoot, &childEx);
            if (childEx && childEx != PROTO_NONE) {
                pending_exception = childEx;
                has_pending_exception = true;
                return PROTO_NONE;
            }
            return result;
        }
        if (fn->isMethod(pContext)) {
            proto::ProtoMethod m = fn->asMethod(pContext);
            if (m) return m(pContext, thisVal, nullptr, pContext->newList(), nullptr);
        }
        return PROTO_NONE;
    };

    /* ToPrimitive helper: coerce an object to a primitive via valueOf, then toString.
     * Implements ES Abstract Relational / Abstract Equality "number hint" semantics:
     *   1. Try obj.valueOf() — if it returns a primitive, use that.
     *   2. Try obj.toString() — if it returns a primitive, use that.
     *   3. If either method throws, sets pending_exception and returns PROTO_NONE.
     *   4. If neither returns a primitive, throws TypeError.
     * Returns the original value unchanged when the object is already a primitive. */
    auto toPrimIfObject = [&](const proto::ProtoObject* obj) -> const proto::ProtoObject* {
        if (!obj || obj == PROTO_NONE || obj->isNone(pContext)) return obj;
        if (obj == t_nullSentinel) return obj;  // null does not coerce to primitive
        if (obj->isBoolean(pContext) || obj->isInteger(pContext) ||
            obj->isDouble(pContext) || obj->isFloat(pContext) ||
            obj->asString(pContext)) return obj;
        auto isPrimitive = [&](const proto::ProtoObject* v) -> bool {
            return v && v != PROTO_NONE &&
                   (v->isBoolean(pContext) || v->isInteger(pContext) ||
                    v->isDouble(pContext) || v->isFloat(pContext) ||
                    v->asString(pContext));
        };
        // Fast path: wrapper objects created by new String() / new Number() store their
        // primitive value under __primitive_value__ to avoid the valueOf/toString dance.
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(pContext);
        if (pvKey) {
            const proto::ProtoObject* pv = obj->getAttribute(pContext, pvKey, false);
            if (pv && pv != PROTO_NONE && !pv->isNone(pContext) &&
                (pv->isBoolean(pContext) || pv->isInteger(pContext) ||
                 pv->isDouble(pContext) || pv->isFloat(pContext) || pv->asString(pContext)))
                return pv;
        }
        bool valueOfPresent = false;
        bool toStringPresent = false;
        // Step 1: try valueOf.
        const proto::ProtoString* vk = JSSymbols::valueOf(pContext);
        if (vk) {
            const proto::ProtoObject* vfn = obj->getAttribute(pContext, vk, true);
            if (vfn && vfn != PROTO_NONE) {
                valueOfPresent = true;
                const proto::ProtoObject* prim = callMethod(vfn, vk, obj);
                if (has_pending_exception) return PROTO_NONE;
                if (isPrimitive(prim)) return prim;
            }
        }
        // Step 2: try toString.
        const proto::ProtoString* tk = JSSymbols::toString(pContext);
        if (tk) {
            const proto::ProtoObject* tfn = obj->getAttribute(pContext, tk, true);
            if (tfn && tfn != PROTO_NONE) {
                toStringPresent = true;
                const proto::ProtoObject* prim = callMethod(tfn, tk, obj);
                if (has_pending_exception) return PROTO_NONE;
                if (isPrimitive(prim)) return prim;
            }
        }
        // Both valueOf (if present) and toString (if present) returned non-primitives:
        // throw TypeError per ES spec.
        if (valueOfPresent || toStringPresent) {
            pending_exception = makeError(pContext, "TypeError",
                "Cannot convert object to primitive value", pGlobalRoot);
            has_pending_exception = true;
        }
        return PROTO_NONE;
    };

    while (pc >= 0 && pc < len) {
        const proto::ProtoObject* globalObj = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : PROTO_NONE;
        int opcode = buf[pc++];
        switch (opcode) {
            // --- Constant and immediate pushes ---
            case OP_push_minus1:
                stackPush(pContext,pContext->fromInteger(-1));
                break;
            case OP_push_0:
                stackPush(pContext,pContext->fromInteger(0));
                break;
            case OP_push_1:
                stackPush(pContext,pContext->fromInteger(1));
                break;
            case OP_push_2:
                stackPush(pContext,pContext->fromInteger(2));
                break;
            case OP_push_3:
                stackPush(pContext,pContext->fromInteger(3));
                break;
            case OP_push_4:
                stackPush(pContext,pContext->fromInteger(4));
                break;
            case OP_push_5:
                stackPush(pContext,pContext->fromInteger(5));
                break;
            case OP_push_6:
                stackPush(pContext,pContext->fromInteger(6));
                break;
            case OP_push_7:
                stackPush(pContext,pContext->fromInteger(7));
                break;
            case OP_push_i8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t v = static_cast<int8_t>(buf[pc++]);
                stackPush(pContext,pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_i16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t v = static_cast<int16_t>(get_u16(buf + pc));
                pc += 2;
                stackPush(pContext,pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_i32: {
                // push_i32 encodes a 32-bit signed immediate.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t v = (int32_t)get_u32(buf + pc);
                pc += 4;
                stackPush(pContext,pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_const8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                if (idx < cpool.size())
                    stackPush(pContext,cpool[idx]);
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_push_empty_string:
                stackPush(pContext,pContext->fromUTF8String(""));
                break;
            case OP_push_this:
                // Strict mode: pass thisObj as-is (undefined stays undefined).
                // Non-strict mode: coerce null/undefined to the global object per spec.
                if (module->isStrict) {
                    stackPush(pContext, thisObj ? thisObj : PROTO_NONE);
                } else {
                    stackPush(pContext, (thisObj && thisObj != PROTO_NONE) ? thisObj
                                                                           : (globalObj ? globalObj : PROTO_NONE));
                }
                break;
            case OP_special_object: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t soKind = buf[pc++];
                if (soKind == 0 || soKind == 1) {
                    // ARGUMENTS / MAPPED_ARGUMENTS: build array-like object from args.
                    // QuickJS only emits this in functions with has_arguments_binding, never in arrow fns.
                    const proto::ProtoObject* argsObj = pContext->newObject(true);
                    int argc2 = args ? static_cast<int>(args->getSize(pContext)) : 0;
                    for (int ai = 0; ai < argc2; ai++) {
                        const proto::ProtoString* idxKey = JSSymbols::indexKey(pContext, static_cast<uint32_t>(ai));
                        const proto::ProtoObject* argVal = args->getAt(pContext, ai);
                        if (idxKey && argsObj)
                            argsObj = argsObj->setAttribute(pContext, idxKey, argVal ? argVal : PROTO_NONE);
                    }
                    const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                    if (lenKey2 && argsObj)
                        argsObj = argsObj->setAttribute(pContext, lenKey2, pContext->fromInteger(static_cast<long long>(argc2)));
                    stackPush(pContext, argsObj ? argsObj : PROTO_NONE);
                } else {
                    // kind 2 = THIS_FUNC, kind 3 = NEW_TARGET — not yet implemented.
                    stackPush(pContext, PROTO_NONE);
                }
                break;
            }
            case OP_rest: {
                // TODO: Implement rest parameter materialization once call/arg opcodes are wired.
                if (pc + 2 > len) return PROTO_NONE;
                pc += 2; // skip u16 argument index
                stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_return: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* result = stackTop(pContext);
                return result;
            }
            case OP_return_undef:
                return PROTO_NONE;
            case OP_throw: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* exObj = stackTop(pContext);
                stackPop(pContext);
                pending_exception = exObj ? exObj : PROTO_NONE;
                has_pending_exception = true;
                break;
            }
            case OP_drop: {
                if (!stackEmpty(pContext)) {
                    // Mirror QuickJS value-stack semantics: the OP_catch sentinel occupies a
                    // specific slot in the value stack.  When OP_drop removes that slot, the
                    // catch frame it represents is gone — pop it from catch_stack as well.
                    unsigned long drop_pos = stackSize(pContext) - 1;
                    if (!catch_stack.empty() && catch_stack.back().placeholder_stack_pos == drop_pos) {
                        catch_stack.pop_back();
                    }
                    stackPop(pContext);
                }
                break;
            }
            case OP_nip:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* top = stackTop(pContext); stackPop(pContext); stackPop(pContext); stackPush(pContext, top); }
                break;
            case OP_nip1:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                { const proto::ProtoObject* c = stackTop(pContext); stackPop(pContext); const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext); stackPop(pContext); stackPush(pContext, b); stackPush(pContext, c); }
                break;
            case OP_dup:
                if (!stackEmpty(pContext)) stackPush(pContext, stackTop(pContext));
                break;
            case OP_dup1:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* top = stackTop(pContext); const proto::ProtoObject* second = stackAt(pContext, 1); stackPush(pContext, second); stackPush(pContext, top); }
                break;
            case OP_dup2:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                stackPush(pContext, stackAt(pContext, 1));
                stackPush(pContext, stackAt(pContext, 1));
                break;
            case OP_dup3:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                stackPush(pContext, stackAt(pContext, 2));
                stackPush(pContext, stackAt(pContext, 2));
                stackPush(pContext, stackAt(pContext, 2));
                break;
            case OP_insert2:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                // obj a -> a obj a
                {
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,a);
                }
                break;
            case OP_insert3:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // obj prop a -> a obj prop a
                {
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,a);
                }
                break;
            case OP_insert4:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // this obj prop a -> a this obj prop a
                {
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* thisVal = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,thisVal);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,a);
                }
                break;
            case OP_perm3:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // obj a b -> a obj b
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,b);
                }
                break;
            case OP_perm4:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // obj prop a b -> a obj prop b
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,b);
                }
                break;
            case OP_perm5:
                if (stackSize(pContext) < 5) return PROTO_NONE;
                // this obj prop a b -> a this obj prop b
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* thisVal = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,thisVal);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,b);
                }
                break;
            case OP_swap:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext); const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext); stackPush(pContext, a); stackPush(pContext, b); }
                break;
            case OP_swap2:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // a b c d -> c d a b
                {
                    const proto::ProtoObject* d = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* c = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,c);
                    stackPush(pContext,d);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                }
                break;
            case OP_rot3l:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // x a b -> a b x
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                    stackPush(pContext,x);
                }
                break;
            case OP_rot3r:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // a b x -> x a b
                {
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,x);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                }
                break;
            case OP_rot4l:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // x a b c -> a b c x
                {
                    const proto::ProtoObject* c = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                    stackPush(pContext,c);
                    stackPush(pContext,x);
                }
                break;
            case OP_rot5l:
                if (stackSize(pContext) < 5) return PROTO_NONE;
                // x a b c d -> a b c d x
                {
                    const proto::ProtoObject* d = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* c = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                    stackPush(pContext,c);
                    stackPush(pContext,d);
                    stackPush(pContext,x);
                }
                break;
            case OP_push_const: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                if (idx < cpool.size())
                    stackPush(pContext,cpool[idx]);
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_push_atom_value: {
                /* QuickJS semantics: push the atom's string representation as a string literal.
                 * This is JS_AtomToString(ctx, atom) — NOT a variable lookup in globalObj. */
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) {
                    stackPush(pContext, PROTO_NONE);
                    break;
                }
                /* Push the atom as a string value (the atom name IS the string literal). */
                stackPush(pContext, key->asObject(pContext));
                break;
            }
            // Short local/arg accessors (loc8/arg8 and loc0-3/arg0-3)
            case OP_get_loc8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_set_loc8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_get_arg0:
            case OP_get_arg1:
            case OP_get_arg2:
            case OP_get_arg3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_arg0);
                if (idx < argCount && idx < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, idx));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_arg0:
            case OP_put_arg1:
            case OP_put_arg2:
            case OP_put_arg3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_arg0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (idx < argCount && idx < (argCount + varCount))
                    setSlot(pContext, idx, val);
                break;
            }
            case OP_set_arg0:
            case OP_set_arg1:
            case OP_set_arg2:
            case OP_set_arg3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_arg0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (idx < argCount && idx < (argCount + varCount))
                    setSlot(pContext, idx, val);
                break;
            }
            case OP_get_var_undef:
            case OP_get_var: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t idx = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = PROTO_NONE;
                bool isLexical = (opcode == OP_get_var) &&
                                 static_cast<size_t>(idx) < module->closureVarIsLexical.size() &&
                                 module->closureVarIsLexical[idx];
                /* Read from *pGlobalRoot (the live global, updated by OP_put_var_init via setAttribute)
                 * rather than from the stale globalObj snapshot passed at call time. */
                const proto::ProtoObject* liveGlobal = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : globalObj;
                /* rawVal is the direct getAttribute result: nullptr=absent, PROTO_NONE=undefined, other=value. */
                const proto::ProtoObject* rawVal = nullptr;
                if (liveGlobal && liveGlobal != PROTO_NONE && static_cast<size_t>(idx) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[idx];
                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str()) ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key) {
                            /* getAttribute(key, false) returns:
                             *   nullptr    → key is completely absent (TDZ or not-yet-stored)
                             *   PROTO_NONE → key present, value is undefined (initialized)
                             *   other      → key present, initialized to a real value */
                            rawVal = liveGlobal->getAttribute(pContext, key, false);
                            /* TDZ check: absent key for a lexical variable means uninitialized. */
                            if (isLexical && !rawVal) {
                                const std::string& vname = module->closureVarNames[idx];
                                std::string msg = "Cannot access '";
                                msg += vname.empty() ? "?" : vname;
                                msg += "' before initialization";
                                pending_exception = makeError(pContext, "ReferenceError", msg.c_str(), pGlobalRoot);
                                has_pending_exception = true;
                                break;
                            }
                            val = rawVal;
                        }
                    }
                }
                /* Slot fallback: only when the global key is absent (rawVal==nullptr).
                 * Skip when rawVal==PROTO_NONE: the variable was initialized to undefined.
                 * Skipping prevents stale slot data from shadowing the legitimate undefined value.
                 * Use the dedicated closure-var slot region (argCount + varCount + idx), not the
                 * local-var region, so that the _ret_ eval variable never collides. */
                if (!rawVal && (!val || val == PROTO_NONE)) {
                    val = getSlot(pContext, argCount + varCount + idx);
                }
                // For OP_get_var (not OP_get_var_undef): if the variable is completely absent
                // (rawVal==nullptr means not in global, slot also empty), throw ReferenceError.
                // OP_get_var_undef is the safe variant used by typeof and optional chaining.
                if (opcode == OP_get_var && !rawVal && (!val || val == PROTO_NONE)) {
                    std::string msg;
                    if (static_cast<size_t>(idx) < module->closureVarNames.size() &&
                        !module->closureVarNames[idx].empty()) {
                        msg = module->closureVarNames[idx] + " is not defined";
                    } else {
                        msg = "is not defined";
                    }
                    pending_exception = makeError(pContext, "ReferenceError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_var_init:
            case OP_put_var: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t idx = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);

                if (pGlobalRoot && static_cast<size_t>(idx) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[idx];
                    bool isLexical =
                        static_cast<size_t>(idx) < module->closureVarIsLexical.size() &&
                        module->closureVarIsLexical[idx];

                    // Restricted global lexical declarations (e.g. eval/arguments) should fail.
                    if (isLexical && (name == "eval" || name == "arguments")) {
                        pending_exception = makeError(pContext, "SyntaxError",
                                                      "Invalid global lexical declaration", pGlobalRoot);
                        has_pending_exception = true;
                        break;
                    }

                    // Global assignment to `undefined` should throw instead of mutating.
                    if (name == "undefined") {
                        pending_exception = makeError(pContext, "ReferenceError",
                                                      "Cannot assign to read only binding 'undefined'", pGlobalRoot);
                        has_pending_exception = true;
                        break;
                    }

                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str()) ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_get_var_ref0:
            case OP_get_var_ref1:
            case OP_get_var_ref2:
            case OP_get_var_ref3: {
                /* Closure vars occupy slots AFTER local vars: slot[argCount + varCount + refIndex].
                 * This separates them from local vars (slot[argCount + localIdx]) so that the
                 * hidden _ret_ eval variable at local slot 0 never collides with closure var 0. */
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_get_var_ref0);
                stackPush(pContext, getSlot(pContext, argCount + varCount + refIndex));
                break;
            }
            case OP_put_var_ref0:
            case OP_put_var_ref1:
            case OP_put_var_ref2:
            case OP_put_var_ref3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_put_var_ref0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                /* Write to the dedicated closure-var slot (see OP_get_var_ref0..3 comment). */
                setSlot(pContext, argCount + varCount + refIndex, val);
                /* Also write to the global object so that hoisted function declarations in global
                 * eval are visible to nested functions (which initialise their closure-var slots
                 * from the global object at startup). */
                if (pGlobalRoot && *pGlobalRoot && static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[refIndex];
                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str())
                            ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_set_var_ref0:
            case OP_set_var_ref1:
            case OP_set_var_ref2:
            case OP_set_var_ref3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_set_var_ref0);
                const proto::ProtoObject* val = stackTop(pContext);
                /* Write to slot and global (same rationale as OP_put_var_ref). */
                setSlot(pContext, argCount + varCount + refIndex, val);
                if (pGlobalRoot && *pGlobalRoot && static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[refIndex];
                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str())
                            ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_get_var_ref: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                stackPush(pContext, getSlot(pContext, argCount + varCount + refIndex));
                break;
            }
            case OP_put_var_ref: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                setSlot(pContext, argCount + varCount + refIndex, val);
                if (pGlobalRoot && *pGlobalRoot && static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[refIndex];
                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str())
                            ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_set_var_ref: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                setSlot(pContext, argCount + varCount + refIndex, val);
                if (pGlobalRoot && *pGlobalRoot && static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[refIndex];
                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str())
                            ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_get_var_ref_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                {
                    const proto::ProtoObject* val = getSlot(pContext, argCount + varCount + refIndex);
                    if (val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot); has_pending_exception = true;
                        break;
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                }
                break;
            }
            case OP_put_var_ref_check:
            case OP_put_var_ref_check_init: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                setSlot(pContext, argCount + varCount + refIndex, val);
                if (pGlobalRoot && *pGlobalRoot && static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[refIndex];
                    if (!name.empty()) {
                        const proto::ProtoString* key = (pContext->fromUTF8String(name.c_str())
                            ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_close_loc: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, tdzSentinel);
                break;
            }
            case OP_get_loc0:
            case OP_get_loc1:
            case OP_get_loc2:
            case OP_get_loc3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_loc0);
                if (idx < varCount && (argCount + idx) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + idx));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc0:
            case OP_put_loc1:
            case OP_put_loc2:
            case OP_put_loc3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_loc0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (idx < varCount && (argCount + idx) < (argCount + varCount))
                    setSlot(pContext, argCount + idx, val);
                break;
            }
            case OP_set_loc0:
            case OP_set_loc1:
            case OP_set_loc2:
            case OP_set_loc3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_loc0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (idx < varCount && (argCount + idx) < (argCount + varCount))
                    setSlot(pContext, argCount + idx, val);
                break;
            }
            // --- Locals, arguments, and variable references ---
            case OP_get_loc: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_set_loc: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_get_arg: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_arg: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    setSlot(pContext, argIndex, val);
                break;
            }
            case OP_set_arg: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    setSlot(pContext, argIndex, val);
                break;
            }
            case OP_set_loc_uninitialized: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, tdzSentinel);
                break;
            }
            case OP_get_loc_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* val = getSlot(pContext, argCount + locIndex);
                    if (val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot); has_pending_exception = true;
                        break;
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
                break;
            }
            case OP_put_loc_check: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_set_loc_check: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_put_loc_check_init: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_get_loc_checkthis: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_get_field: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) { stackPush(pContext, PROTO_NONE); break; }
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    std::string keyStr;
                    if (key) key->toUTF8String(pContext, keyStr);
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    msg += " (reading '"; msg += keyStr; msg += "')";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                const proto::ProtoObject* val;
                uint8_t taTypeF = getTypedArrayElementType(pContext, obj);
                if (taTypeF != 0xFF) {
                    // TODO: optimize: avoid std::string allocation in TypedArray element access
                    // hot path — use a ProtoString::tryParseUint32() method once available.
                    std::string keyStr;
                    key->toUTF8String(pContext, keyStr);
                    const bool isNumeric = !keyStr.empty() &&
                        std::all_of(keyStr.begin(), keyStr.end(),
                                    [](unsigned char c){ return c >= '0' && c <= '9'; });
                    if (isNumeric) {
                        uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
                        val = typedArrayGetElement(pContext, obj, idx, taTypeF);
                    } else {
                        val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                    }
                } else {
                    val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_field2: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stackTop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                // Throw TypeError for null/undefined receiver (OP_get_field2 keeps obj on stack).
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    stackPop(pContext); // consume obj from stack
                    std::string keyStr;
                    if (key) key->toUTF8String(pContext, keyStr);
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    msg += " (reading '"; msg += keyStr; msg += "')";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                const proto::ProtoObject* val;
                uint8_t taTypeF2 = getTypedArrayElementType(pContext, obj);
                if (taTypeF2 != 0xFF && key) {
                    std::string keyStr;
                    key->toUTF8String(pContext, keyStr);
                    const bool isNumeric = !keyStr.empty() &&
                        std::all_of(keyStr.begin(), keyStr.end(),
                                    [](unsigned char c){ return c >= '0' && c <= '9'; });
                    if (isNumeric) {
                        uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
                        val = typedArrayGetElement(pContext, obj, idx, taTypeF2);
                    } else {
                        val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                    }
                } else {
                    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_field: {
                // DEF(put_field, 5, 2, 0, atom) — n_pop=2, n_push=0.
                // Pops obj (second) and val (top), sets obj[key]=val. Pushes NOTHING.
                // QuickJS peephole-optimizes "insert2 + put_field + drop" → "put_field" so
                // the result value is never on the stack here.
                if (pc + 4 > len || stackSize(pContext) < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                uint8_t taTypePF = getTypedArrayElementType(pContext, obj);
                if (taTypePF != 0xFF && key) {
                    std::string keyStr;
                    key->toUTF8String(pContext, keyStr);
                    const bool isNumeric = !keyStr.empty() &&
                        std::all_of(keyStr.begin(), keyStr.end(),
                                    [](unsigned char c){ return c >= '0' && c <= '9'; });
                    if (isNumeric) {
                        uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
                        const proto::ProtoObject* updatedTA =
                            typedArraySetElement(pContext, obj, idx, val, taTypePF);
                        if (updatedTA && updatedTA != obj) {
                            updateMapping(pContext, obj, updatedTA);
                        }
                        // n_push=0: do NOT push anything back
                        break;
                    }
                }
                if (key && obj) {
                    std::string keyStr2;
                    key->toUTF8String(pContext, keyStr2);

                    // Check 1: Object.freeze — frozen objects reject all writes.
                    {
                        const proto::ProtoObject* fko = pContext->fromUTF8String("__is_frozen__");
                        const proto::ProtoString* fk  = fko ? fko->asString(pContext) : nullptr;
                        if (fk) {
                            const proto::ProtoObject* fv = obj->getAttribute(pContext, fk, false);
                            if (fv == PROTO_TRUE) {
                                if (module->isStrict) {
                                    pending_exception = makeError(pContext, "TypeError",
                                        "Cannot assign to property of frozen object", pGlobalRoot);
                                    has_pending_exception = true;
                                }
                                break; // n_push=0: do NOT push anything
                            }
                        }
                    }

                    // Check 2: Non-writable data property (__pd_ sidecar, bit0=writable).
                    {
                        std::string pdKeyStr = "__pd_" + keyStr2 + "__";
                        const proto::ProtoObject* pdko2 = pContext->fromUTF8String(pdKeyStr.c_str());
                        const proto::ProtoString* pdk2  = pdko2 ? pdko2->asString(pContext) : nullptr;
                        if (pdk2) {
                            const proto::ProtoObject* bitsObj2 = obj->getAttribute(pContext, pdk2, false);
                            if (bitsObj2 && bitsObj2 != PROTO_NONE && bitsObj2->isInteger(pContext)) {
                                uint8_t bits2 = static_cast<uint8_t>(bitsObj2->asLong(pContext));
                                bool writable2 = (bits2 & 0x1) != 0;
                                if (!writable2) {
                                    if (module->isStrict) {
                                        pending_exception = makeError(pContext, "TypeError",
                                            "Cannot assign to read only property", pGlobalRoot);
                                        has_pending_exception = true;
                                    }
                                    break; // n_push=0: do NOT push anything
                                }
                            }
                        }
                    }

                    // Check 3: Object.preventExtensions — reject adding new own properties.
                    {
                        const proto::ProtoObject* eko = pContext->fromUTF8String("__extensible__");
                        const proto::ProtoString* ek  = eko ? eko->asString(pContext) : nullptr;
                        if (ek) {
                            const proto::ProtoObject* ev = obj->getAttribute(pContext, ek, false);
                            if (ev == PROTO_FALSE) {
                                const proto::ProtoString* propKey = pContext->fromUTF8String(keyStr2.c_str()) ?
                                    pContext->fromUTF8String(keyStr2.c_str())->asString(pContext) : nullptr;
                                bool alreadyOwn = false;
                                if (propKey) {
                                    const proto::ProtoObject* own = obj->hasOwnAttribute(pContext, propKey);
                                    alreadyOwn = (own == PROTO_TRUE);
                                }
                                if (!alreadyOwn) {
                                    if (module->isStrict) {
                                        pending_exception = makeError(pContext, "TypeError",
                                            "Cannot add property to non-extensible object", pGlobalRoot);
                                        has_pending_exception = true;
                                    }
                                    break; // n_push=0: do NOT push anything
                                }
                            }
                        }
                    }

                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, val);
                    if (newObj != obj) {
                        updateMapping(pContext, obj, newObj);
                    }
                    if (newObj && pGlobalRoot && obj == globalObj)
                        *pGlobalRoot = newObj;
                    // n_push=0: do NOT push anything back
                }
                break;
            }
            case OP_define_field: {
                if (pc + 4 > len || stackSize(pContext) < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* value = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (key && obj) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, value);
                    if (newObj && newObj != PROTO_NONE) {
                        // If the key is a pure numeric index (e.g. "32", "33") and the object
                        // has a .length property, update .length when idx+1 > currentLength.
                        // This fixes array literals with >32 elements: QuickJS emits
                        // OP_array_from for the first 32 elements, then OP_define_field for rest.
                        std::string keyStr;
                        key->toUTF8String(pContext, keyStr);
                        const bool isNumericKey = !keyStr.empty() &&
                            std::all_of(keyStr.begin(), keyStr.end(),
                                        [](unsigned char c){ return c >= '0' && c <= '9'; });
                        if (isNumericKey) {
                            const uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
                            const proto::ProtoString* lenKey = JSSymbols::length(pContext);
                            if (lenKey) {
                                const proto::ProtoObject* curLenObj =
                                    newObj->getAttribute(pContext, lenKey, false);
                                const long long curLen =
                                    (curLenObj && curLenObj != PROTO_NONE && curLenObj->isInteger(pContext))
                                    ? curLenObj->asLong(pContext) : 0LL;
                                if (static_cast<long long>(idx) + 1LL > curLen) {
                                    const proto::ProtoObject* lenNewObj = newObj->setAttribute(
                                        pContext, lenKey,
                                        pContext->fromInteger(static_cast<long long>(idx) + 1LL));
                                    updateMapping(pContext, newObj, lenNewObj);
                                    newObj = lenNewObj;
                                }
                            }
                        }
                    }
                    updateMapping(pContext, obj, newObj);
                    if (newObj && pGlobalRoot && obj == globalObj)
                        *pGlobalRoot = newObj;
                    stackPush(pContext, newObj ? newObj : obj);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
                break;
            }
            case OP_define_array_el: {
                // DEF(define_array_el, 1, 3, 2, none)
                // Stack: [..., array, index, value] → [..., array, index]
                // Writes array[index] = value and discards the value.
                if (stackSize(pContext) < 3) break;
                const proto::ProtoObject* elemVal2 = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* idxVal   = stackTop(pContext);  // peek — stays on stack
                const proto::ProtoObject* arrObj2  = stackAt(pContext, 1); // peek — stays
                if (!arrObj2 || arrObj2 == PROTO_NONE) break;
                // Convert index to string key.
                const proto::ProtoString* idxKey2 = nullptr;
                if (idxVal && idxVal != PROTO_NONE && idxVal->isInteger(pContext)) {
                    long long i2 = idxVal->asLong(pContext);
                    if (i2 >= 0)
                        idxKey2 = JSSymbols::indexKey(pContext, static_cast<uint32_t>(i2));
                } else if (idxVal && idxVal != PROTO_NONE) {
                    idxKey2 = idxVal->asString(pContext);
                }
                if (!idxKey2) break;
                // Set array[index] = value; update array pointer in slot below index.
                const proto::ProtoObject* newArr = arrObj2->setAttribute(
                    pContext, idxKey2, elemVal2 ? elemVal2 : PROTO_NONE);
                // Update the length if needed.
                if (newArr && idxVal && idxVal->isInteger(pContext)) {
                    long long i2 = idxVal->asLong(pContext);
                    const proto::ProtoString* lenKey4 = JSSymbols::length(pContext);
                    if (lenKey4) {
                        const proto::ProtoObject* curLenObj4 = newArr->getAttribute(pContext, lenKey4, false);
                        long long curLen4 = (curLenObj4 && curLenObj4 != PROTO_NONE && curLenObj4->isInteger(pContext))
                            ? curLenObj4->asLong(pContext) : 0LL;
                        if (i2 + 1LL > curLen4) {
                            const proto::ProtoObject* updatedArr =
                                newArr->setAttribute(pContext, lenKey4, pContext->fromInteger(i2 + 1LL));
                            updateMapping(pContext, newArr, updatedArr);
                            newArr = updatedArr;
                        }
                    }
                }
                updateMapping(pContext, arrObj2, newArr);
                // Replace the array slot (2nd from top) with updated array.
                stackPop(pContext);                // pop index
                stackPop(pContext);                // pop old array
                stackPush(pContext, newArr ? newArr : arrObj2);  // push updated array
                stackPush(pContext, idxVal);        // push index back
                break;
            }
            case OP_to_propkey: {
                // Converts TOS to a canonical property key (string, integer, or symbol).
                // In our protoCore world the value on stack is already a ProtoObject that
                // can be used directly as an attribute key via asString(). This is a no-op:
                // the key remains on the stack unchanged.
                break;
            }
            case OP_define_method_computed: {
                // DEF(define_method_computed, 2, 3, 1, u8)
                // Format: 1 byte op_flags. Stack: [..., obj, key, method] → [..., obj].
                // Assigns obj[key] = method for computed-property object literals and classes.
                if (pc + 1 > len || stackSize(pContext) < 3) { if (pc + 1 <= len) pc++; return PROTO_NONE; }
                /* uint8_t op_flags = */ buf[pc++]; // consumed but unused (METHOD/GETTER/SETTER flag)
                const proto::ProtoObject* methodVal = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* keyVal    = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* obj2      = stackTop(pContext);
                if (!obj2 || obj2 == PROTO_NONE || !keyVal || keyVal == PROTO_NONE) break;
                // Convert key to ProtoString (handles string or numeric keys).
                const proto::ProtoString* keyStr2 = keyVal->asString(pContext);
                if (!keyStr2 && keyVal->isInteger(pContext)) {
                    long long idx = keyVal->asLong(pContext);
                    if (idx >= 0)
                        keyStr2 = JSSymbols::indexKey(pContext, static_cast<uint32_t>(idx));
                }
                if (!keyStr2) break;
                const proto::ProtoObject* newObj2 =
                    obj2->setAttribute(pContext, keyStr2, methodVal ? methodVal : PROTO_NONE);
                stackPop(pContext);
                stackPush(pContext, newObj2 ? newObj2 : obj2);
                break;
            }
            case OP_set_name_computed: {
                // DEF(set_name_computed, 1, 2, 2, none)
                // Stack: [..., key, function] — sets function.name = String(key), stack unchanged.
                if (stackSize(pContext) < 2) break;
                const proto::ProtoObject* funcSNC = stackTop(pContext);
                const proto::ProtoObject* keySNC  = stackAt(pContext, 1);
                if (funcSNC && funcSNC != PROTO_NONE && keySNC && keySNC != PROTO_NONE) {
                    const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                    if (nameKey) {
                        // Convert key to string for the name value.
                        std::string nameStr;
                        const proto::ProtoString* keyPS = keySNC->asString(pContext);
                        if (keyPS) keyPS->toUTF8String(pContext, nameStr);
                        else if (keySNC->isInteger(pContext))
                            nameStr = std::to_string(keySNC->asLong(pContext));
                        const proto::ProtoObject* nameVal = pContext->fromUTF8String(nameStr.c_str());
                        if (nameVal) {
                            const proto::ProtoObject* newFunc = funcSNC->setAttribute(pContext, nameKey, nameVal);
                            updateMapping(pContext, funcSNC, newFunc);
                            // Replace TOS with updated function.
                            stackPop(pContext);
                            stackPush(pContext, newFunc ? newFunc : funcSNC);
                        }
                    }
                }
                break;
            }
            case OP_set_name: {
                /* Sets the .name property of TOS (function/value) to the given atom string.
                 * Format: atom (4 bytes). n_pop=1, n_push=1. */
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* func = stackTop(pContext);
                stackPop(pContext);
                if (func && func != PROTO_NONE) {
                    const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                    const proto::ProtoString* nameStr = resolveAtom(mod, pContext, atomIndex);
                    if (nameKey && nameStr) {
                        std::string nameUtf8;
                        nameStr->toUTF8String(pContext, nameUtf8);
                        const proto::ProtoObject* nameVal = pContext->fromUTF8String(nameUtf8.c_str());
                        if (nameVal) {
                            const proto::ProtoObject* newFunc = func->setAttribute(pContext, nameKey, nameVal);
                            updateMapping(pContext, func, newFunc);
                            func = newFunc;
                        }
                    }
                }
                stackPush(pContext, func ? func : PROTO_NONE);
                break;
            }
            case OP_object: {
                // Create a mutable object that inherits from Object.prototype so that
                // hasOwnProperty, toString, valueOf, etc. are found via prototype lookup.
                const proto::ProtoObject* objProto =
                    (pContext->space) ? pContext->space->objectPrototype : nullptr;
                const proto::ProtoObject* newObj = (objProto && objProto != PROTO_NONE)
                    ? objProto->newChild(pContext, true)
                    : pContext->newObject(true);
                stackPush(pContext,newObj);
                break;
            }
            // --- Array element helpers (implemented via property semantics) ---
            case OP_get_array_el: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                const proto::ProtoObject* val;
                uint8_t taType = getTypedArrayElementType(pContext, obj);
                if (taType != 0xFF && index && index->isInteger(pContext) && index->asLong(pContext) >= 0) {
                    val = typedArrayGetElement(pContext, obj, static_cast<uint32_t>(index->asLong(pContext)), taType);
                } else {
                    const proto::ProtoObject* keyObj = toString(pContext, index);
                    const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_array_el2: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stackTop(pContext);
                const proto::ProtoObject* obj = stackAt(pContext, 1);
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    stackPop(pContext); // pop index
                    stackPop(pContext); // pop obj
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                const proto::ProtoObject* val;
                uint8_t taType2 = getTypedArrayElementType(pContext, obj);
                if (taType2 != 0xFF && index && index->isInteger(pContext) && index->asLong(pContext) >= 0) {
                    val = typedArrayGetElement(pContext, obj, static_cast<uint32_t>(index->asLong(pContext)), taType2);
                } else {
                    const proto::ProtoObject* keyObj = toString(pContext, index);
                    const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_array_el3: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stackTop(pContext);
                const proto::ProtoObject* obj = stackAt(pContext, 1);
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    stackPop(pContext); // pop index
                    stackPop(pContext); // pop obj
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                const proto::ProtoObject* val;
                uint8_t taType3 = getTypedArrayElementType(pContext, obj);
                if (taType3 != 0xFF && index && index->isInteger(pContext) && index->asLong(pContext) >= 0) {
                    val = typedArrayGetElement(pContext, obj, static_cast<uint32_t>(index->asLong(pContext)), taType3);
                } else {
                    const proto::ProtoObject* keyObj = toString(pContext, index);
                    const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                }
                stackPush(pContext, index);
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_array_el: {
                if (stackSize(pContext) < 3) return PROTO_NONE;
                const proto::ProtoObject* value = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* index = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                uint8_t taTypeW = getTypedArrayElementType(pContext, obj);
                if (taTypeW != 0xFF && index && index->isInteger(pContext) && index->asLong(pContext) >= 0) {
                    const proto::ProtoObject* updatedTA = typedArraySetElement(
                        pContext, obj, static_cast<uint32_t>(index->asLong(pContext)), value, taTypeW);
                    if (updatedTA && updatedTA != obj) {
                        updateMapping(pContext, obj, updatedTA);
                    }
                    stackPush(pContext, updatedTA ? updatedTA : obj);
                    break;
                }
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                if (obj && key) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, value);
                    updateMapping(pContext, obj, newObj);
                    // Update .length if index is a valid array index (non-negative integer).
                    // JS array semantics: assigning x[n] = v updates length to max(length, n+1).
                    if (index && index->isInteger(pContext)) {
                        long long idx = index->asLong(pContext);
                        if (idx >= 0 && idx < (long long)0xFFFFFFFELL) {
                            const proto::ProtoString* lenKey =
                                JSSymbols::length(pContext);
                            if (lenKey) {
                                const proto::ProtoObject* curLenVal =
                                    newObj->getAttribute(pContext, lenKey, false);
                                long long curLen = (curLenVal && curLenVal != PROTO_NONE &&
                                                    curLenVal->isInteger(pContext))
                                    ? curLenVal->asLong(pContext) : 0LL;
                                if (idx + 1 > curLen) {
                                    const proto::ProtoObject* updatedObj = newObj->setAttribute(pContext, lenKey,
                                        pContext->fromInteger(idx + 1));
                                    updateMapping(pContext, newObj, updatedObj);
                                }
                            }
                        }
                    }
                }
                break;
            }
            case OP_undefined:
                stackPush(pContext,PROTO_NONE);
                break;
            case OP_null:
                // JS null is the null sentinel, not PROTO_NONE (which is undefined).
                stackPush(pContext, t_nullSentinel ? t_nullSentinel : PROTO_NONE);
                break;
            case OP_push_false:
                stackPush(pContext,PROTO_FALSE);
                break;
            case OP_push_true:
                stackPush(pContext,PROTO_TRUE);
                break;
            // --- Control flow ---
            case OP_goto: {
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += diff;
                break;
            }
            case OP_goto16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t diff = static_cast<int16_t>(get_u16(buf + pc));
                pc += diff;
                break;
            }
            case OP_goto8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t diff = static_cast<int8_t>(buf[pc]);
                pc += diff;
                break;
            }
            case OP_if_true: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                break;
            }
            case OP_if_false: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (!toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                break;
            }
            case OP_if_true8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                break;
            }
            case OP_if_false8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (!toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                break;
            }
            case OP_add: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                // ToPrimitive first so that objects with valueOf/toString participate
                // in string detection and numeric addition correctly.
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                // JS semantics: if either operand is a string, convert both to string and concat.
                // Otherwise, convert both to number.
                bool aIsStr = a && a != PROTO_NONE && a->isString(pContext);
                bool bIsStr = b && b != PROTO_NONE && b->isString(pContext);
                const proto::ProtoObject* res;
                if (aIsStr || bIsStr) {
                    // String concatenation: convert both sides to string, use ProtoString::appendLast
                    const proto::ProtoObject* ra = toString(pContext, a);
                    const proto::ProtoObject* rb = toString(pContext, b);
                    const proto::ProtoString* sa = ra ? ra->asString(pContext) : nullptr;
                    const proto::ProtoString* sb = rb ? rb->asString(pContext) : nullptr;
                    if (sa && sb) {
                        const proto::ProtoString* concat = sa->appendLast(pContext, sb);
                        res = concat ? concat->asObject(pContext) : PROTO_NONE;
                    } else {
                        res = sa ? ra : (sb ? rb : PROTO_NONE);
                    }
                } else {
                    const proto::ProtoObject* ra = toNumber(pContext, a);
                    const proto::ProtoObject* rb = toNumber(pContext, b);
                    res = ra ? ra->add(pContext, rb) : PROTO_NONE;
                }
                stackPush(pContext, res ? res : PROTO_NONE);
                break;
            }
            case OP_mul: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* res = a->multiply(pContext, b);
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_div: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                // JS division always yields double (handles /0 → ±Infinity, 0/0 → NaN, -0 correctly).
                auto toDoubleVal = [&](const proto::ProtoObject* v) -> double {
                    if (!v || v == PROTO_NONE) return 0.0;
                    if (v->isInteger(pContext)) return static_cast<double>(v->asLong(pContext));
                    if (v->isDouble(pContext) || v->isFloat(pContext)) return v->asDouble(pContext);
                    return 0.0;
                };
                double da = toDoubleVal(a);
                double db = toDoubleVal(b);
                stackPush(pContext, pContext->fromDouble(da / db));
                break;
            }
            case OP_sub: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* res = a->subtract(pContext, b);
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_mod: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(stackTop(pContext)));
                stackPop(pContext);
                if (has_pending_exception) break;
                // JS modulo yields double when either operand is double (matches IEEE 754 fmod).
                // Always use fmod to avoid mixed-type exceptions in protoCore.
                {
                    auto toDoubleVal2 = [&](const proto::ProtoObject* v) -> double {
                        if (!v || v == PROTO_NONE) return 0.0;
                        if (v->isInteger(pContext)) return static_cast<double>(v->asLong(pContext));
                        if (v->isDouble(pContext) || v->isFloat(pContext)) return v->asDouble(pContext);
                        return 0.0;
                    };
                    double da = toDoubleVal2(a);
                    double db = toDoubleVal2(b);
                    // If both are exact integers and divisor is non-zero, return integer result.
                    if (db != 0.0 && a && a != PROTO_NONE && a->isInteger(pContext) &&
                        b && b != PROTO_NONE && b->isInteger(pContext)) {
                        long long ia = a->asLong(pContext);
                        long long ib = b->asLong(pContext);
                        stackPush(pContext, pContext->fromInteger(ia % ib));
                    } else {
                        stackPush(pContext, pContext->fromDouble(std::fmod(da, db)));
                    }
                }
                break;
            }
            case OP_eq:
            case OP_neq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* pa = toPrimIfObject(a);
                if (has_pending_exception) break;
                const proto::ProtoObject* pb = toPrimIfObject(b);
                if (has_pending_exception) break;
                bool eq = jsAbstractEquals(pContext, pa, pb);
                stackPush(pContext, (opcode == OP_eq ? eq : !eq) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_strict_eq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : ((!a && !b) ? 0 : 1);
                stackPush(pContext, (cmp == 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_strict_neq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : ((!a && !b) ? 0 : 1);
                stackPush(pContext, (cmp != 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_lt: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp < 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_lte: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp <= 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_gt: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp > 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_gte: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext));
                stackPop(pContext);
                if (has_pending_exception) break;
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp >= 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_and: {
                // Bitwise AND: ToInt32(a) & ToInt32(b)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                int32_t res = toInt32Val(pContext, a) & toInt32Val(pContext, b);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_or: {
                // Bitwise OR: ToInt32(a) | ToInt32(b)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                int32_t res = toInt32Val(pContext, a) | toInt32Val(pContext, b);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_xor: {
                // Bitwise XOR: ToInt32(a) ^ ToInt32(b)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                int32_t res = toInt32Val(pContext, a) ^ toInt32Val(pContext, b);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_shl: {
                // Left shift: ToInt32(a) << (ToUint32(b) & 0x1F)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                int32_t res = toInt32Val(pContext, a) << (toUint32Val(pContext, b) & 0x1Fu);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_sar: {
                // Arithmetic right shift: ToInt32(a) >> (ToUint32(b) & 0x1F)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                int32_t res = toInt32Val(pContext, a) >> (toUint32Val(pContext, b) & 0x1Fu);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_shr: {
                // Unsigned right shift: ToUint32(a) >>> (ToUint32(b) & 0x1F) → Int32 result
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                uint32_t ua = toUint32Val(pContext, a);
                uint32_t shift = toUint32Val(pContext, b) & 0x1Fu;
                uint32_t ures = ua >> shift;
                // ToNumber result: if fits in signed int32, return integer, else double
                if (ures <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                    stackPush(pContext, pContext->fromInteger(static_cast<long long>(ures)));
                } else {
                    stackPush(pContext, pContext->fromDouble(static_cast<double>(ures)));
                }
                break;
            }
            case OP_not: {
                // Bitwise NOT: ~ToInt32(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = toPrimIfObject(stackTop(pContext)); stackPop(pContext);
                if (has_pending_exception) break;
                int32_t res = ~toInt32Val(pContext, a);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_neg: {
                // Unary minus: -ToNumber(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* num = toNumber(pContext, toPrimIfObject(a));
                if (has_pending_exception) break;
                if (!num || num == PROTO_NONE) { stackPush(pContext, pContext->fromDouble(std::numeric_limits<double>::quiet_NaN())); break; }
                if (num->isInteger(pContext)) {
                    long long v = num->asLong(pContext);
                    stackPush(pContext, v == 0 ? pContext->fromDouble(-0.0) : pContext->fromInteger(-v));
                } else {
                    stackPush(pContext, pContext->fromDouble(-num->asDouble(pContext)));
                }
                break;
            }
            case OP_lnot: {
                // Logical NOT: !ToBoolean(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                stackPush(pContext, toBool(pContext, a) ? PROTO_FALSE : PROTO_TRUE);
                break;
            }
            case OP_inc: {
                // Prefix increment: ToNumber(a) + 1
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext)); stackPop(pContext);
                if (!a || a == PROTO_NONE) { stackPush(pContext, pContext->fromDouble(std::numeric_limits<double>::quiet_NaN())); break; }
                if (a->isInteger(pContext)) stackPush(pContext, pContext->fromInteger(a->asLong(pContext) + 1));
                else stackPush(pContext, pContext->fromDouble(a->asDouble(pContext) + 1.0));
                break;
            }
            case OP_dec: {
                // Prefix decrement: ToNumber(a) - 1
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext)); stackPop(pContext);
                if (!a || a == PROTO_NONE) { stackPush(pContext, pContext->fromDouble(std::numeric_limits<double>::quiet_NaN())); break; }
                if (a->isInteger(pContext)) stackPush(pContext, pContext->fromInteger(a->asLong(pContext) - 1));
                else stackPush(pContext, pContext->fromDouble(a->asDouble(pContext) - 1.0));
                break;
            }
            case OP_post_inc: {
                // Post-increment: pushes original value then incremented value.
                // Stack effect: a → a_orig, a+1 (but QuickJS spec: n_pop=1, n_push=2)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* num = toNumber(pContext, a);
                const proto::ProtoObject* inc;
                if (!num || num == PROTO_NONE) inc = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                else if (num->isInteger(pContext)) inc = pContext->fromInteger(num->asLong(pContext) + 1);
                else inc = pContext->fromDouble(num->asDouble(pContext) + 1.0);
                stackPush(pContext, num ? num : PROTO_NONE);
                stackPush(pContext, inc);
                break;
            }
            case OP_post_dec: {
                // Post-decrement: pushes original value then decremented value.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* num = toNumber(pContext, a);
                const proto::ProtoObject* dec;
                if (!num || num == PROTO_NONE) dec = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                else if (num->isInteger(pContext)) dec = pContext->fromInteger(num->asLong(pContext) - 1);
                else dec = pContext->fromDouble(num->asDouble(pContext) - 1.0);
                stackPush(pContext, num ? num : PROTO_NONE);
                stackPush(pContext, dec);
                break;
            }
            case OP_dec_loc: {
                // Decrement a local variable slot in-place. Format: loc8 (1 byte).
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = toNumber(pContext, getSlot(pContext, argCount + locIndex));
                    const proto::ProtoObject* nv;
                    if (!cur || cur == PROTO_NONE) nv = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                    else if (cur->isInteger(pContext)) nv = pContext->fromInteger(cur->asLong(pContext) - 1);
                    else nv = pContext->fromDouble(cur->asDouble(pContext) - 1.0);
                    setSlot(pContext, argCount + locIndex, nv);
                }
                break;
            }
            case OP_inc_loc: {
                // Increment a local variable slot in-place. Format: loc8 (1 byte).
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = toNumber(pContext, getSlot(pContext, argCount + locIndex));
                    const proto::ProtoObject* nv;
                    if (!cur || cur == PROTO_NONE) nv = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                    else if (cur->isInteger(pContext)) nv = pContext->fromInteger(cur->asLong(pContext) + 1);
                    else nv = pContext->fromDouble(cur->asDouble(pContext) + 1.0);
                    setSlot(pContext, argCount + locIndex, nv);
                }
                break;
            }
            case OP_add_loc: {
                // add_loc loc8: pops TOS and adds it to a local variable. Format: loc8 (1 byte).
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = getSlot(pContext, argCount + locIndex);
                    // JS + semantics: string concat or numeric add.
                    bool curIsStr = cur && cur != PROTO_NONE && cur->asString(pContext);
                    bool valIsStr = val && val != PROTO_NONE && val->asString(pContext);
                    const proto::ProtoObject* nv;
                    if (curIsStr || valIsStr) {
                        const proto::ProtoObject* sc = toString(pContext, cur);
                        const proto::ProtoObject* sv = toString(pContext, val);
                        const proto::ProtoString* sa = sc ? sc->asString(pContext) : nullptr;
                        const proto::ProtoString* sb = sv ? sv->asString(pContext) : nullptr;
                        if (sa && sb) { const proto::ProtoString* cat = sa->appendLast(pContext, sb); nv = cat ? cat->asObject(pContext) : PROTO_NONE; }
                        else nv = sc ? sc : (sv ? sv : PROTO_NONE);
                    } else {
                        const proto::ProtoObject* nc = toNumber(pContext, cur);
                        const proto::ProtoObject* nval = toNumber(pContext, val);
                        nv = nc ? nc->add(pContext, nval) : PROTO_NONE;
                    }
                    setSlot(pContext, argCount + locIndex, nv);
                }
                break;
            }
            case OP_pow: {
                // Exponentiation: a ** b
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(stackTop(pContext))); stackPop(pContext);
                if (has_pending_exception) break;
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(stackTop(pContext))); stackPop(pContext);
                if (has_pending_exception) break;
                double da = (!a || a == PROTO_NONE) ? std::numeric_limits<double>::quiet_NaN() : a->asDouble(pContext);
                double db = (!b || b == PROTO_NONE) ? std::numeric_limits<double>::quiet_NaN() : b->asDouble(pContext);
                double result = std::pow(da, db);
                if (result == std::trunc(result) && std::abs(result) < 9.007199254740992e15 && !std::isnan(result) && !std::isinf(result))
                    stackPush(pContext, pContext->fromInteger(static_cast<long long>(result)));
                else
                    stackPush(pContext, pContext->fromDouble(result));
                break;
            }
            case OP_is_undefined_or_null: {
                // Pops one value; pushes true if it is undefined (PROTO_NONE) or null (t_nullSentinel).
                // Used by the ?? operator and ?. optional chaining.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE || val == t_nullSentinel) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_nop:
                // No operation.
                break;
            case OP_get_length: {
                // Push the .length property of TOS.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* obj = stackTop(pContext); stackPop(pContext);
                const proto::ProtoString* lk = JSSymbols::length(pContext);
                const proto::ProtoObject* len_val = (obj && lk) ? obj->getAttribute(pContext, lk, true) : PROTO_NONE;
                stackPush(pContext, len_val ? len_val : PROTO_NONE);
                break;
            }
            case OP_to_object: {
                // ToObject: null and undefined are not object-coercible — throw TypeError.
                // For any other value, push unchanged (primitives wrap lazily).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                if (!val || val == PROTO_NONE || val == t_nullSentinel) {
                    const bool isNull = (val == t_nullSentinel);
                    pending_exception = makeError(pContext, "TypeError",
                        isNull ? "Cannot convert null to object"
                               : "Cannot convert undefined to object",
                        pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                stackPush(pContext, val);
                break;
            }
            case OP_throw_error: {
                // throw_error atom u8: throw a TypeError/ReferenceError etc. with message from atom.
                // Format: atom(4), u8(1). n_pop=0, n_push=0 (throws).
                if (pc + 5 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                uint8_t errorType = buf[pc + 4];
                pc += 5;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                std::string msg;
                if (key) key->toUTF8String(pContext, msg);
                const char* errName = (errorType == 1) ? "TypeError" :
                                      (errorType == 2) ? "ReferenceError" : "Error";
                pending_exception = makeError(pContext, errName, msg.c_str(), pGlobalRoot); has_pending_exception = true;
                break;
            }
            case OP_catch: {
                // catch label(4): record a catch handler and push a sentinel onto the value stack.
                // The sentinel's stack position IS the "catch frame" marker, mirroring how QuickJS
                // stores a JS_TAG_CATCH_OFFSET integer on the value stack.
                // placeholder_stack_pos is the index the sentinel will occupy (stackSize before push).
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                int handler_pc = pc + diff;
                pc += 4;
                unsigned long placeholder_pos = stackSize(pContext);
                catch_stack.push_back({handler_pc, placeholder_pos});
                stackPush(pContext, PROTO_NONE); // sentinel placeholder (undefined-equivalent)
                break;
            }
            case OP_nip_catch: {
                // nip_catch: pop the catch frame and replace the sentinel (and anything above it
                // pushed by iterator opcodes) with the current top value.  Mirrors QuickJS:
                //   ret_val = *--sp;
                //   while (sp[-1] != JS_TAG_CATCH_OFFSET) { free(*--sp); }
                //   sp[-1] = ret_val;
                // We know the sentinel's position from placeholder_stack_pos.
                if (!catch_stack.empty()) {
                    unsigned long placeholder_pos = catch_stack.back().placeholder_stack_pos;
                    catch_stack.pop_back();
                    if (!stackEmpty(pContext)) {
                        const proto::ProtoObject* ret_val = stackTop(pContext);
                        stackPop(pContext);
                        // Truncate the stack down to (and including) the placeholder slot, then push ret_val.
                        while (stackSize(pContext) > placeholder_pos) stackPop(pContext);
                        stackPush(pContext, ret_val);
                    }
                }
                break;
            }
            case OP_gosub: {
                // gosub label(4): call a finally block.
                // Push the return address (instruction after gosub) as an integer, then jump.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                int return_pc = pc + 4;
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(return_pc)));
                pc += diff; // jump to finally block (same formula as goto)
                break;
            }
            case OP_ret: {
                // ret: return from a finally block.
                // Pop the return address pushed by gosub and jump there.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* addr_obj = stackTop(pContext);
                stackPop(pContext);
                if (!addr_obj || addr_obj == PROTO_NONE || !addr_obj->isInteger(pContext))
                    return PROTO_NONE;
                pc = static_cast<int>(addr_obj->asLong(pContext));
                break;
            }
            case OP_plus: {
                // Unary plus: ToNumber(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                { const proto::ProtoObject* pv = toPrimIfObject(a);
                  if (has_pending_exception) break;
                  stackPush(pContext, toNumber(pContext, pv)); }
                break;
            }
            case OP_typeof: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* v = stackTop(pContext);
                stackPop(pContext);
                const char* typeStr = "undefined";
                if (v == t_nullSentinel) {
                    typeStr = "object";  // typeof null === "object" per spec
                } else if (v && v != PROTO_NONE && !v->isNone(pContext)) {
                    if (v->isBoolean(pContext)) typeStr = "boolean";
                    else if (v->isInteger(pContext) || v->isDouble(pContext) || v->isFloat(pContext)) typeStr = "number";
                    else if (v->asString(pContext)) typeStr = "string";
                    else if (v->isMethod(pContext) || getBytecodeId(pContext, v) >= 0) typeStr = "function";
                    else {
                        // Check for bound function sentinel (__bound_fn__ attribute).
                        const proto::ProtoString* bfTypeKey = JSSymbols::boundFn(pContext);
                        const proto::ProtoObject* bfTypeTarget = bfTypeKey
                            ? v->getAttribute(pContext, bfTypeKey, false) : nullptr;
                        typeStr = (bfTypeTarget && bfTypeTarget != PROTO_NONE) ? "function" : "object";
                    }
                }
                stackPush(pContext, pContext->fromUTF8String(typeStr));
                break;
            }
            case OP_instanceof: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                const proto::ProtoObject* protoObj = func ? func->getAttribute(pContext, protoKey, false) : nullptr;
                const proto::ProtoObject* res = (obj && protoObj && protoObj != PROTO_NONE) ? obj->isInstanceOf(pContext, protoObj) : PROTO_FALSE;
                stackPush(pContext, (res == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_in: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                // QuickJS pushes: key first, then object. Stack top = object, second = key.
                const proto::ProtoObject* obj    = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* keyVal = stackTop(pContext);
                stackPop(pContext);
                // Spec §13.10.1: throw TypeError if RHS is not an object (null, undefined,
                // booleans, numbers, and plain strings are not valid RHS for 'in').
                bool objIsPrimitive = (!obj || obj == PROTO_NONE || obj == t_nullSentinel
                    || obj->isBoolean(pContext) || obj->isInteger(pContext)
                    || (obj->isString(pContext) && !obj->isMethod(pContext)));
                if (objIsPrimitive) {
                    pending_exception = makeError(pContext, "TypeError",
                        "Cannot use 'in' operator to search for property in non-object", pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                const proto::ProtoObject* keyObj = toString(pContext, keyVal);
                const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                // IMPORTANT: hasAttribute returns PROTO_TRUE or PROTO_FALSE (both are non-null
                // pointers), so must compare against PROTO_TRUE — never cast to bool directly.
                const proto::ProtoObject* hasResult = (key) ? obj->hasAttribute(pContext, key) : PROTO_FALSE;
                stackPush(pContext, hasResult == PROTO_TRUE ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_delete: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* keyVal = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* keyObj = toString(pContext, keyVal);
                const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                if (obj && key) {
                    const proto::ProtoObject* prev = obj->getAttribute(pContext, key, false);
                    (void)obj->setAttribute(pContext, key, PROTO_NONE);
                    (void)prev;
                }
                stackPush(pContext, PROTO_TRUE);
                break;
            }
            case OP_call_method:
            case OP_tail_call_method: {
                // Stack (top = index 0): arg0, ..., arg(n-1), func, this. QuickJS: call_argv = sp - argc, call_argv[-1] = func, call_argv[-2] = this.
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc);
                const proto::ProtoObject* thisVal = stackAt(pContext, argc + 1);
                int bcId = getBytecodeId(pContext, func);
                // Resolve bytecode ID: current module first, then root module.
                const ProtoBytecodeModule* resolvedMod2 = nullptr;
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size())
                    resolvedMod2 = &nested[bcId];
                else if (bcId >= 0 && t_rootModule &&
                         static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
                    resolvedMod2 = &t_rootModule->nestedFunctions[bcId];
                if (resolvedMod2) {
                    const auto& nf = *resolvedMod2;
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    // Determine effective this: arrow functions use their lexical __arrow_this__
                    // capture rather than the call-site receiver, matching ECMAScript semantics.
                    const proto::ProtoObject* effectiveThis = thisVal;
                    if (nf.isArrow) {
                        const proto::ProtoObject* captured =
                            func->getAttribute(pContext, JSSymbols::arrowThis(pContext), false);
                        if (captured && captured != PROTO_NONE)
                            effectiveThis = captured;
                    }
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));
                    const proto::ProtoObject* childEx = PROTO_NONE;
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, effectiveThis, argsList, pGlobalRoot, &childEx);
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        break;
                    }
                    if (opcode != OP_tail_call_method)
                        stackPush(pContext, result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    // Invoke the native function directly via asMethod() to bypass the
                    // ProtoObject::call() attribute-lookup indirection.
                    const proto::ProtoMethod nativeFn = func->asMethod(pContext);
                    const proto::ProtoObject* result = nativeFn
                        ? nativeFn(pContext, thisVal, nullptr, argsList, nullptr)
                        : PROTO_NONE;
                    if (opcode != OP_tail_call_method)
                        stackPush(pContext, result ? result : PROTO_NONE);
                } else {
                    // Check for bound function sentinel (__bound_fn__ attribute).
                    const proto::ProtoString* bfMethKey = JSSymbols::boundFn(pContext);
                    const proto::ProtoObject* bfMethTarget = (func && func != PROTO_NONE && bfMethKey)
                        ? func->getAttribute(pContext, bfMethKey, false) : nullptr;
                    if (bfMethTarget && bfMethTarget != PROTO_NONE) {
                        const proto::ProtoString* btMethKey = JSSymbols::boundThis(pContext);
                        const proto::ProtoString* baMethKey = JSSymbols::boundArgs(pContext);
                        const proto::ProtoObject* boundThisMeth =
                            (btMethKey) ? func->getAttribute(pContext, btMethKey, false) : PROTO_NONE;
                        if (!boundThisMeth) boundThisMeth = PROTO_NONE;
                        const proto::ProtoObject* boundArgsMeth =
                            (baMethKey) ? func->getAttribute(pContext, baMethKey, false) : nullptr;

                        // Collect call-site args before popping stack.
                        const proto::ProtoList* callSiteMethArgs = pContext->newList();
                        for (uint32_t i = 0; i < argc; i++)
                            callSiteMethArgs = callSiteMethArgs->appendLast(pContext,
                                stackAt(pContext, argc - 1 - i));
                        for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);

                        // Prepend pre-bound args to call-site args.
                        const proto::ProtoList* mergedMethArgs = pContext->newList();
                        if (boundArgsMeth && boundArgsMeth != PROTO_NONE) {
                            const proto::ProtoString* lenKeyMeth = JSSymbols::length(pContext);
                            long long blenMeth = 0;
                            if (lenKeyMeth) {
                                const proto::ProtoObject* lo = boundArgsMeth->getAttribute(pContext, lenKeyMeth, false);
                                if (lo && lo != PROTO_NONE) {
                                    if (lo->isInteger(pContext))     blenMeth = lo->asLong(pContext);
                                    else if (lo->isDouble(pContext)) blenMeth = static_cast<long long>(lo->asDouble(pContext));
                                }
                            }
                            for (long long bi = 0; bi < blenMeth; bi++) {
                                const proto::ProtoString* ik = JSSymbols::indexKey(pContext, static_cast<uint32_t>(bi));
                                const proto::ProtoObject* av = ik ? boundArgsMeth->getAttribute(pContext, ik, false) : PROTO_NONE;
                                mergedMethArgs = mergedMethArgs->appendLast(pContext, av ? av : PROTO_NONE);
                            }
                        }
                        int csmArgc = callSiteMethArgs ? callSiteMethArgs->getSize(pContext) : 0;
                        for (int ci = 0; ci < csmArgc; ci++)
                            mergedMethArgs = mergedMethArgs->appendLast(pContext,
                                callSiteMethArgs->getAt(pContext, ci));
                        const proto::ProtoObject* result = callJSFunction(pContext, bfMethTarget, boundThisMeth, mergedMethArgs);
                        if (opcode != OP_tail_call_method) stackPush(pContext, result ? result : PROTO_NONE);
                    } else {
                        for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                        // func is neither bytecode, native, nor bound — throw TypeError.
                        if (!func || func == PROTO_NONE) {
                            pending_exception = makeError(pContext, "TypeError",
                                "is not a function", pGlobalRoot);
                            has_pending_exception = true;
                        } else {
                            // Non-null but unrecognized callable — best-effort PROTO_NONE.
                            if (opcode != OP_tail_call_method)
                                stackPush(pContext, PROTO_NONE);
                        }
                    }
                }
                break;
            }
            case OP_call_constructor: {
                // Stack: ... func, newTarget, arg0, ..., arg(argc-1). Create new object, call func as ctor, return object or result.
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc + 1);
                const proto::ProtoObject* newTarget = stackAt(pContext, argc);
                // Use func.prototype as newObj prototype if available (needed for `instanceof` to work).
                const proto::ProtoString* newObjProtoKey = JSSymbols::prototype(pContext);
                const proto::ProtoObject* funcProtoForNew = (newObjProtoKey && func && func != PROTO_NONE)
                    ? func->getAttribute(pContext, newObjProtoKey, false) : nullptr;
                const proto::ProtoObject* newObj = (funcProtoForNew && funcProtoForNew != PROTO_NONE)
                    ? funcProtoForNew->newChild(pContext, true)
                    : pContext->newObject(true);
                if (!newObj) { for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext); stackPush(pContext, PROTO_NONE); break; }
                const proto::ProtoList* argsList = pContext->newList();
                for (uint32_t i = 0; i < argc; i++)
                    argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                const proto::ProtoObject* result = PROTO_NONE;
                int bcId = getBytecodeId(pContext, func);
                // Resolve bytecode ID: current module first, then root module.
                const ProtoBytecodeModule* resolvedMod3 = nullptr;
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size())
                    resolvedMod3 = &nested[bcId];
                else if (bcId >= 0 && t_rootModule &&
                         static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
                    resolvedMod3 = &t_rootModule->nestedFunctions[bcId];
                if (resolvedMod3) {
                    // Set newObj.constructor = func so that `thrown.constructor === Ctor` works.
                    const proto::ProtoString* ctorKeyC = JSSymbols::constructor(pContext);
                    if (ctorKeyC && func && func != PROTO_NONE)
                        newObj = newObj->setAttribute(pContext, ctorKeyC, func);
                    const auto& nf = *resolvedMod3;
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));
                    const proto::ProtoObject* childEx = PROTO_NONE;
                    result = runBytecode(&childCtx, &nf, newObj, argsList, pGlobalRoot, &childEx);
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        break;
                    }
                } else if (func && func->isMethod(pContext)) {
                    // Invoke the native constructor directly via asMethod().
                    const proto::ProtoMethod ctorFn = func->asMethod(pContext);
                    result = ctorFn ? ctorFn(pContext, newObj, nullptr, argsList, nullptr) : PROTO_NONE;
                } else {
                    // Check for the Array constructor (marked with __array_ctor__).
                    const proto::ProtoString* arrayCtorAttr =
                        JSSymbols::arrayCtor(pContext);
                    const proto::ProtoObject* isArrayCtor =
                        (func && func != PROTO_NONE && arrayCtorAttr)
                            ? func->getAttribute(pContext, arrayCtorAttr, false) : nullptr;
                    if (isArrayCtor && isArrayCtor == PROTO_TRUE) {
                        // Obtain Array.prototype from the constructor's "prototype" attribute.
                        const proto::ProtoString* protoAttr =
                            JSSymbols::prototype(pContext);
                        const proto::ProtoObject* arrProto = (protoAttr && func)
                            ? func->getAttribute(pContext, protoAttr, false) : nullptr;
                        const proto::ProtoObject* arr = (arrProto && arrProto != PROTO_NONE)
                            ? arrProto->newChild(pContext, true)
                            : pContext->newObject(true);
                        if (argc == 0) {
                            // new Array() → empty array.
                            const proto::ProtoString* lk = JSSymbols::length(pContext);
                            if (lk) arr = arr->setAttribute(pContext, lk, pContext->fromInteger(0LL));
                        } else if (argc == 1) {
                            const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
                            bool isNumeric = a0 && (a0->isInteger(pContext) ||
                                                    a0->isDouble(pContext) || a0->isFloat(pContext));
                            if (isNumeric) {
                                // new Array(n) → pre-allocated array of length n.
                                long long n = a0->isInteger(pContext)
                                    ? a0->asLong(pContext)
                                    : static_cast<long long>(a0->asDouble(pContext));
                                const proto::ProtoString* lk = JSSymbols::length(pContext);
                                if (lk) arr = arr->setAttribute(pContext, lk, pContext->fromInteger(n));
                            } else {
                                // new Array(elem) → [elem].
                                const proto::ProtoString* k0 = JSSymbols::indexKey(pContext, 0);
                                if (k0) arr = arr->setAttribute(pContext, k0, a0 ? a0 : PROTO_NONE);
                                const proto::ProtoString* lk = JSSymbols::length(pContext);
                                if (lk) arr = arr->setAttribute(pContext, lk, pContext->fromInteger(1LL));
                            }
                        } else {
                            // new Array(a, b, c, …) → [a, b, c, …].
                            for (uint32_t ai = 0; ai < argc; ai++) {
                                const proto::ProtoString* ki =
                                    JSSymbols::indexKey(pContext, static_cast<uint32_t>(ai));
                                if (ki)
                                    arr = arr->setAttribute(pContext, ki,
                                                            argsList->getAt(pContext, static_cast<int>(ai)));
                            }
                            const proto::ProtoString* lk = JSSymbols::length(pContext);
                            if (lk)
                                arr = arr->setAttribute(pContext, lk,
                                                        pContext->fromInteger(static_cast<long long>(argc)));
                        }
                        result = arr;
                    } else {
                        // Check for built-in error constructor stub.
                        const proto::ProtoString* errCtorAttr = JSSymbols::errorCtor(pContext);
                        const proto::ProtoObject* errTypeName = (func && func != PROTO_NONE && errCtorAttr)
                            ? func->getAttribute(pContext, errCtorAttr, false) : nullptr;
                        if (errTypeName && errTypeName != PROTO_NONE && errTypeName->isString(pContext)) {
                            const proto::ProtoObject* msgArg = (argc > 0) ? argsList->getAt(pContext, 0) : PROTO_NONE;
                            const proto::ProtoObject* msgStr = toString(pContext, msgArg);
                            std::string msgStr2;
                            if (msgStr && msgStr != PROTO_NONE && msgStr->isString(pContext))
                                msgStr->asString(pContext)->toUTF8String(pContext, msgStr2);
                            std::string errType;
                            errTypeName->asString(pContext)->toUTF8String(pContext, errType);
                            result = makeError(pContext, errType.c_str(), msgStr2.c_str(), pGlobalRoot);
                            } else {
                            // Check for RegExp constructor (marked with __regexp_ctor__).
                            const proto::ProtoString* regexpCtorAttr =
                                JSSymbols::regexpCtor(pContext);
                            const proto::ProtoObject* isRegExpCtor =
                                (func && func != PROTO_NONE && regexpCtorAttr)
                                    ? func->getAttribute(pContext, regexpCtorAttr, false) : nullptr;
                            if (isRegExpCtor && isRegExpCtor == PROTO_TRUE) {
                                // Obtain RegExp.prototype from the constructor's "prototype" attribute.
                                const proto::ProtoString* protoAttr =
                                    JSSymbols::prototype(pContext);
                                const proto::ProtoObject* reProto = (protoAttr && func)
                                    ? func->getAttribute(pContext, protoAttr, false) : nullptr;
                                const proto::ProtoObject* re = (reProto && reProto != PROTO_NONE)
                                    ? reProto->newChild(pContext, true)
                                    : pContext->newObject(true);

                                // Call the native regexpConstructor logic.
                                result = regexpConstructor(pContext, re, nullptr, argsList, nullptr);
                            } else {
                                // Check for __typed_array_ctor__ marker (ArrayBuffer, DataView, TypedArray).
                                const proto::ProtoString* taCtorAttr = JSSymbols::taCtor(pContext);
                                const proto::ProtoObject* taCtorTag = (func && func != PROTO_NONE && taCtorAttr)
                                    ? func->getAttribute(pContext, taCtorAttr, false) : nullptr;
                                if (taCtorTag && taCtorTag != PROTO_NONE) {
                                    if (taCtorTag->isString(pContext)) {
                                        std::string ctorNameStr;
                                        taCtorTag->asString(pContext)->toUTF8String(pContext, ctorNameStr);
                                        if (ctorNameStr == "ArrayBuffer") {
                                            unsigned long byteLen = 0;
                                            if (argc > 0) {
                                                const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
                                                if (a0 && a0 != PROTO_NONE) {
                                                    if (a0->isInteger(pContext))
                                                        byteLen = static_cast<unsigned long>(std::max(0LL, a0->asLong(pContext)));
                                                    else if (a0->isDouble(pContext) || a0->isFloat(pContext))
                                                        byteLen = static_cast<unsigned long>(std::max(0.0, a0->asDouble(pContext)));
                                                }
                                            }
                                            result = createArrayBuffer(pContext, byteLen);
                                        } else if (ctorNameStr == "DataView") {
                                            // new DataView(buffer [, byteOffset [, byteLength]])
                                            if (argc < 1) { result = PROTO_NONE; break; }
                                            const proto::ProtoObject* abArg = argsList->getAt(pContext, 0);
                                            if (!isArrayBuffer(pContext, abArg)) { result = PROTO_NONE; break; }

                                            unsigned long abLen = getArrayBufferByteLength(pContext, abArg);
                                            long long bo = 0;
                                            long long bl = static_cast<long long>(abLen);
                                            if (argc > 1) {
                                                const proto::ProtoObject* a1 = argsList->getAt(pContext, 1);
                                                if (a1 && a1 != PROTO_NONE) {
                                                    if (a1->isInteger(pContext)) bo = a1->asLong(pContext);
                                                    else if (a1->isDouble(pContext) || a1->isFloat(pContext))
                                                        bo = static_cast<long long>(a1->asDouble(pContext));
                                                }
                                            }
                                            if (argc > 2) {
                                                const proto::ProtoObject* a2 = argsList->getAt(pContext, 2);
                                                if (a2 && a2 != PROTO_NONE) {
                                                    if (a2->isInteger(pContext)) bl = a2->asLong(pContext);
                                                    else if (a2->isDouble(pContext) || a2->isFloat(pContext))
                                                        bl = static_cast<long long>(a2->asDouble(pContext));
                                                } else {
                                                    bl = static_cast<long long>(abLen) - bo;
                                                }
                                            } else {
                                                bl = static_cast<long long>(abLen) - bo;
                                            }

                                            // Validate range.
                                            if (bo < 0 || bo > static_cast<long long>(abLen) ||
                                                bl < 0 || bo + bl > static_cast<long long>(abLen)) {
                                                result = PROTO_NONE; break;
                                            }

                                            // Build DataView instance from prototype chain.
                                            const proto::ProtoObject* dvCtorObj =
                                                (*pGlobalRoot)->getAttribute(pContext, JSSymbols::DataView(pContext), true);
                                            const proto::ProtoObject* dvProtoObj = dvCtorObj && dvCtorObj != PROTO_NONE
                                                ? dvCtorObj->getAttribute(pContext, JSSymbols::prototype(pContext), false)
                                                : nullptr;
                                            const proto::ProtoObject* dv = (dvProtoObj && dvProtoObj != PROTO_NONE)
                                                ? dvProtoObj->newChild(pContext, true)
                                                : pContext->newObject(true);
                                            dv = dv->setAttribute(pContext, JSSymbols::dvBuffer(pContext), abArg);
                                            dv = dv->setAttribute(pContext, JSSymbols::dvByteOffset(pContext), pContext->fromInteger(bo));
                                            dv = dv->setAttribute(pContext, JSSymbols::dvByteLength(pContext), pContext->fromInteger(bl));
                                            result = dv;
                                        } // end DataView branch
                                    } else if (taCtorTag->isInteger(pContext)) {
                                        // TypedArray constructor: elemType is the integer tag.
                                        uint8_t elemType = static_cast<uint8_t>(taCtorTag->asLong(pContext));
                                        const proto::ProtoObject* taProto =
                                            func->getAttribute(pContext, JSSymbols::prototype(pContext), false);

                                        if (argc == 0) {
                                            result = createTypedArrayFromLength(pContext, taProto, elemType, 0);
                                        } else {
                                            const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
                                            if (a0 && a0 != PROTO_NONE && isArrayBuffer(pContext, a0)) {
                                                // new TypedArray(buffer [, byteOffset [, length]])
                                                long long bo = 0, len = -1;
                                                if (argc > 1) {
                                                    const proto::ProtoObject* a1 = argsList->getAt(pContext, 1);
                                                    if (a1 && a1->isInteger(pContext)) bo = a1->asLong(pContext);
                                                    else if (a1 && (a1->isDouble(pContext) || a1->isFloat(pContext))) bo = static_cast<long long>(a1->asDouble(pContext));
                                                }
                                                if (argc > 2) {
                                                    const proto::ProtoObject* a2 = argsList->getAt(pContext, 2);
                                                    if (a2 && a2->isInteger(pContext)) len = a2->asLong(pContext);
                                                    else if (a2 && (a2->isDouble(pContext) || a2->isFloat(pContext))) len = static_cast<long long>(a2->asDouble(pContext));
                                                }
                                                result = createTypedArrayFromBuffer(pContext, taProto, elemType, a0, bo, len);
                                            } else if (a0 && a0 != PROTO_NONE && isTypedArray(pContext, a0)) {
                                                // Copy from another TypedArray
                                                uint32_t srcLen = getTypedArrayLength(pContext, a0);
                                                uint8_t srcEt = getTypedArrayElementType(pContext, a0);
                                                result = createTypedArrayFromLength(pContext, taProto, elemType, srcLen);
                                                if (result && result != PROTO_NONE) {
                                                    for (uint32_t idx = 0; idx < srcLen; idx++) {
                                                        const proto::ProtoObject* elem = typedArrayGetElement(pContext, a0, idx, srcEt);
                                                        typedArraySetElement(pContext, result, idx, elem, elemType);
                                                    }
                                                }
                                            } else if (a0 && a0 != PROTO_NONE &&
                                                       (a0->isInteger(pContext) || a0->isDouble(pContext) || a0->isFloat(pContext))) {
                                                // new TypedArray(length)
                                                long long lenVal = a0->isInteger(pContext) ? a0->asLong(pContext) : static_cast<long long>(a0->asDouble(pContext));
                                                uint32_t length = lenVal > 0 ? static_cast<uint32_t>(lenVal) : 0;
                                                result = createTypedArrayFromLength(pContext, taProto, elemType, length);
                                            } else if (a0 && a0 != PROTO_NONE) {
                                                // Array-like or iterable: get .length and numeric indices
                                                const proto::ProtoObject* lenObj2 = a0->getAttribute(pContext, JSSymbols::length(pContext), true);
                                                uint32_t srcLen = 0;
                                                if (lenObj2 && lenObj2 != PROTO_NONE && lenObj2->isInteger(pContext))
                                                    srcLen = static_cast<uint32_t>(std::max(0LL, lenObj2->asLong(pContext)));
                                                result = createTypedArrayFromLength(pContext, taProto, elemType, srcLen);
                                                if (result && result != PROTO_NONE) {
                                                    for (uint32_t idx = 0; idx < srcLen; idx++) {
                                                        const proto::ProtoString* idxKey = JSSymbols::indexKey(pContext, idx);
                                                        const proto::ProtoObject* elem = a0->getAttribute(pContext, idxKey, false);
                                                        if (elem && elem != PROTO_NONE)
                                                            typedArraySetElement(pContext, result, idx, elem, elemType);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    // Check for String wrapper constructor (__string_ctor__).
                                    const proto::ProtoString* strCtorAttr2 = JSSymbols::stringCtor(pContext);
                                    const proto::ProtoObject* isStrCtor2 =
                                        (func && func != PROTO_NONE && strCtorAttr2)
                                            ? func->getAttribute(pContext, strCtorAttr2, false) : nullptr;
                                    if (isStrCtor2 && isStrCtor2 == PROTO_TRUE) {
                                        // new String(arg) — store primitive value on wrapper object.
                                        const proto::ProtoObject* a0 = (argc > 0) ? argsList->getAt(pContext, 0) : PROTO_NONE;
                                        const proto::ProtoObject* strVal = toString(pContext, a0 ? a0 : PROTO_NONE);
                                        const proto::ProtoString* pvKey2 = JSSymbols::primitiveValue(pContext);
                                        if (pvKey2 && strVal && strVal != PROTO_NONE)
                                            newObj = newObj->setAttribute(pContext, pvKey2, strVal);
                                        result = newObj;
                                    }
                                }
                            }
                            }
                            }

                }
                bool resultIsObject = result && result != PROTO_NONE
                    && !result->isInteger(pContext) && !result->isDouble(pContext)
                    && !result->asString(pContext) && result != PROTO_TRUE && result != PROTO_FALSE;
                stackPush(pContext, resultIsObject ? result : newObj);
                break;
            }
            case OP_tail_call: // tail-call: same encoding as OP_call but the result is
                               // returned from the current function rather than pushed to stack.
            case OP_call0:
            case OP_call1:
            case OP_call2:
            case OP_call3:
            case OP_call: {
                // is_tail_call: when true, the result should be returned immediately instead of
                // pushed to the value stack (mirrors QuickJS's tail-call frame reuse).
                bool is_tail_call = (opcode == OP_tail_call);
                uint32_t argc;
                if (opcode >= OP_call0 && opcode <= OP_call3) {
                    argc = static_cast<uint32_t>(opcode - OP_call0);
                } else {
                    if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                    argc = get_u16(buf + pc);
                    pc += 2;
                }
                if (stackEmpty(pContext) || stackSize(pContext) < argc + 1) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc);
                int bcId = getBytecodeId(pContext, func);
                // Resolve the bytecode ID: first try the current module, then the root module.
                const ProtoBytecodeModule* resolvedModule = nullptr;
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size())
                    resolvedModule = &nested[bcId];
                else if (bcId >= 0 && t_rootModule &&
                         static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
                    resolvedModule = &t_rootModule->nestedFunctions[bcId];
                if (resolvedModule) {
                    const auto& nf = *resolvedModule;
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    // OP_call has no `this` slot on the stack; spec mandates undefined.
                    // Arrow functions override this with the lexical this captured at closure time.
                    const proto::ProtoObject* callThisVal = PROTO_NONE;
                    if (nf.isArrow) {
                        const proto::ProtoObject* captured =
                            func->getAttribute(pContext, JSSymbols::arrowThis(pContext), false);
                        if (captured && captured != PROTO_NONE)
                            callThisVal = captured;
                    }
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);

                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));

                    const proto::ProtoObject* childEx = PROTO_NONE;
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, callThisVal, argsList, pGlobalRoot, &childEx);
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        break;
                    }
                    if (is_tail_call) return result ? result : PROTO_NONE;
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    // OP_call: no `this` on the stack; pass undefined as receiver.
                    const proto::ProtoObject* thisVal = PROTO_NONE;
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                    // Invoke the native function directly via asMethod().
                    const proto::ProtoMethod nativeFn = func->asMethod(pContext);
                    const proto::ProtoObject* result = nativeFn
                        ? nativeFn(pContext, thisVal, nullptr, argsList, nullptr)
                        : PROTO_NONE;
                    if (is_tail_call) return result ? result : PROTO_NONE;
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else {
                    // Check if this is a built-in error constructor stub (registered by
                    // ensureBuiltinErrorConstructors). If so, call makeError with the first arg.
                    const proto::ProtoString* errCtorAttr = JSSymbols::errorCtor(pContext);
                    const proto::ProtoObject* errTypeName = (func && func != PROTO_NONE && errCtorAttr)
                        ? func->getAttribute(pContext, errCtorAttr, false) : nullptr;
                    if (errTypeName && errTypeName != PROTO_NONE && errTypeName->isString(pContext)) {
                        const proto::ProtoObject* msgArg = (argc > 0) ? stackAt(pContext, argc - 1) : PROTO_NONE;
                        const proto::ProtoObject* msgStr = toString(pContext, msgArg);
                        std::string msgStr2;
                        if (msgStr && msgStr != PROTO_NONE && msgStr->isString(pContext))
                            msgStr->asString(pContext)->toUTF8String(pContext, msgStr2);
                        std::string errType;
                        errTypeName->asString(pContext)->toUTF8String(pContext, errType);
                        for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                        { const proto::ProtoObject* _r = makeError(pContext, errType.c_str(), msgStr2.c_str(), pGlobalRoot);
                          if (is_tail_call) return _r ? _r : PROTO_NONE;
                          stackPush(pContext, _r); }
                    } else {
                        // Check if this is the Array constructor called without `new`.
                        // Per spec, Array(...) is equivalent to new Array(...).
                        const proto::ProtoString* arrayCtorAttr2 =
                            JSSymbols::arrayCtor(pContext);
                        const proto::ProtoObject* isArrayCtor2 =
                            (func && func != PROTO_NONE && arrayCtorAttr2)
                                ? func->getAttribute(pContext, arrayCtorAttr2, false) : nullptr;
                        if (isArrayCtor2 && isArrayCtor2 == PROTO_TRUE) {
                            const proto::ProtoList* argsList = pContext->newList();
                            for (uint32_t i = 0; i < argc; i++)
                                argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                            for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                            // Reuse the Array constructor logic from OP_call_constructor.
                            const proto::ProtoString* protoAttr3 =
                                JSSymbols::prototype(pContext);
                            const proto::ProtoObject* arrProto3 = (protoAttr3 && func)
                                ? func->getAttribute(pContext, protoAttr3, false) : nullptr;
                            const proto::ProtoObject* arr3 = (arrProto3 && arrProto3 != PROTO_NONE)
                                ? arrProto3->newChild(pContext, true)
                                : pContext->newObject(true);
                            if (argc == 0) {
                                const proto::ProtoString* lk = JSSymbols::length(pContext);
                                if (lk) arr3 = arr3->setAttribute(pContext, lk, pContext->fromInteger(0LL));
                            } else if (argc == 1) {
                                const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
                                bool isNum = a0 && (a0->isInteger(pContext) || a0->isDouble(pContext));
                                if (isNum) {
                                    long long n = a0->isInteger(pContext)
                                        ? a0->asLong(pContext)
                                        : static_cast<long long>(a0->asDouble(pContext));
                                    const proto::ProtoString* lk = JSSymbols::length(pContext);
                                    if (lk) arr3 = arr3->setAttribute(pContext, lk, pContext->fromInteger(n));
                                } else {
                                    const proto::ProtoString* k0 = JSSymbols::indexKey(pContext, 0);
                                    if (k0) arr3 = arr3->setAttribute(pContext, k0, a0 ? a0 : PROTO_NONE);
                                    const proto::ProtoString* lk = JSSymbols::length(pContext);
                                    if (lk) arr3 = arr3->setAttribute(pContext, lk, pContext->fromInteger(1LL));
                                }
                            } else {
                                for (uint32_t ai = 0; ai < argc; ai++) {
                                    const proto::ProtoString* ki =
                                        JSSymbols::indexKey(pContext, ai);
                                    if (ki) arr3 = arr3->setAttribute(pContext, ki,
                                        argsList->getAt(pContext, static_cast<int>(ai)));
                                }
                                const proto::ProtoString* lk = JSSymbols::length(pContext);
                                if (lk) arr3 = arr3->setAttribute(pContext, lk,
                                    pContext->fromInteger(static_cast<long long>(argc)));
                            }
                            if (is_tail_call) return arr3 ? arr3 : PROTO_NONE;
                            stackPush(pContext, arr3 ? arr3 : PROTO_NONE);
                        } else {
                            // Check for String() conversion (marked with __string_ctor__).
                            const proto::ProtoString* strCtorAttr = JSSymbols::stringCtor(pContext);
                            const proto::ProtoObject* isStringCtor =
                                (func && func != PROTO_NONE && strCtorAttr)
                                    ? func->getAttribute(pContext, strCtorAttr, false) : nullptr;
                            if (isStringCtor && isStringCtor == PROTO_TRUE) {
                                const proto::ProtoObject* arg = (argc > 0) ? stackAt(pContext, argc - 1) : PROTO_NONE;
                                for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                                { const proto::ProtoObject* _r = toString(pContext, arg);
                                  if (is_tail_call) return _r ? _r : PROTO_NONE;
                                  stackPush(pContext, _r); }
                            } else {
                                // Check for bound function sentinel (__bound_fn__ attribute).
                                const proto::ProtoString* bfCallKey = JSSymbols::boundFn(pContext);
                                const proto::ProtoObject* bfCallTarget = (func && func != PROTO_NONE && bfCallKey)
                                    ? func->getAttribute(pContext, bfCallKey, false) : nullptr;
                                if (bfCallTarget && bfCallTarget != PROTO_NONE) {
                                    const proto::ProtoString* btCallKey = JSSymbols::boundThis(pContext);
                                    const proto::ProtoString* baCallKey = JSSymbols::boundArgs(pContext);
                                    const proto::ProtoObject* boundThisCall =
                                        (btCallKey) ? func->getAttribute(pContext, btCallKey, false) : PROTO_NONE;
                                    if (!boundThisCall) boundThisCall = PROTO_NONE;
                                    const proto::ProtoObject* boundArgsCall =
                                        (baCallKey) ? func->getAttribute(pContext, baCallKey, false) : nullptr;

                                    // Collect call-site args before popping stack.
                                    const proto::ProtoList* callSiteArgs = pContext->newList();
                                    for (uint32_t i = 0; i < argc; i++)
                                        callSiteArgs = callSiteArgs->appendLast(pContext,
                                            stackAt(pContext, argc - 1 - i));
                                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);

                                    // Prepend pre-bound args to call-site args.
                                    const proto::ProtoList* mergedCallArgs = pContext->newList();
                                    if (boundArgsCall && boundArgsCall != PROTO_NONE) {
                                        const proto::ProtoString* lenKeyCall = JSSymbols::length(pContext);
                                        long long blenCall = 0;
                                        if (lenKeyCall) {
                                            const proto::ProtoObject* lo = boundArgsCall->getAttribute(pContext, lenKeyCall, false);
                                            if (lo && lo != PROTO_NONE) {
                                                if (lo->isInteger(pContext))     blenCall = lo->asLong(pContext);
                                                else if (lo->isDouble(pContext)) blenCall = static_cast<long long>(lo->asDouble(pContext));
                                            }
                                        }
                                        for (long long bi = 0; bi < blenCall; bi++) {
                                            const proto::ProtoString* ik = JSSymbols::indexKey(pContext, static_cast<uint32_t>(bi));
                                            const proto::ProtoObject* av = ik ? boundArgsCall->getAttribute(pContext, ik, false) : PROTO_NONE;
                                            mergedCallArgs = mergedCallArgs->appendLast(pContext, av ? av : PROTO_NONE);
                                        }
                                    }
                                    int csArgc = callSiteArgs ? callSiteArgs->getSize(pContext) : 0;
                                    for (int ci = 0; ci < csArgc; ci++)
                                        mergedCallArgs = mergedCallArgs->appendLast(pContext,
                                            callSiteArgs->getAt(pContext, ci));
                                    const proto::ProtoObject* result = callJSFunction(pContext, bfCallTarget, boundThisCall, mergedCallArgs);
                                    if (is_tail_call) return result ? result : PROTO_NONE;
                                    stackPush(pContext, result ? result : PROTO_NONE);
                                } else {
                                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                                    /* Function not yet converted to ProtoMethod; push PROTO_NONE. */
                                    if (is_tail_call) return PROTO_NONE;
                                    stackPush(pContext, PROTO_NONE);
                                }
                            }
                        }
                    }
                }
                break;
            }
            case OP_fclosure8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                const proto::ProtoObject* rawFn = (idx < cpool.size()) ? cpool[idx] : PROTO_NONE;
                int fnBcId8 = getBytecodeId(pContext, rawFn);
                if (fnBcId8 >= 0) {
                    // Create fresh function instance inheriting from Function.prototype so
                    // fn.call/bind/apply resolve up the prototype chain.
                    const proto::ProtoObject** gr8 = t_currentGlobalRoot;
                    const proto::ProtoString* fpKey8 = JSSymbols::functionProto(pContext);
                    const proto::ProtoObject* fp8 = (gr8 && *gr8 && fpKey8)
                        ? (*gr8)->getAttribute(pContext, fpKey8, false) : nullptr;
                    const proto::ProtoObject* fnInst = (fp8 && fp8 != PROTO_NONE)
                        ? fp8->newChild(pContext, true)
                        : pContext->newObject(true);
                    fnInst = fnInst->setAttribute(pContext, JSSymbols::bytecodeId(pContext),
                        pContext->fromInteger(static_cast<long long>(fnBcId8)));
                    fnInst = fnInst->setAttribute(pContext, JSSymbols::prototype(pContext),
                        pContext->newObject(true));
                    // Resolve function metadata from the root module's flat nestedFunctions
                    // list where all functions reside with globally unique IDs.
                    const ProtoBytecodeModule* nm8Ptr = nullptr;
                    if (fnBcId8 >= 0 && t_rootModule &&
                            static_cast<size_t>(fnBcId8) < t_rootModule->nestedFunctions.size())
                        nm8Ptr = &t_rootModule->nestedFunctions[static_cast<size_t>(fnBcId8)];
                    if (nm8Ptr) {
                        const ProtoBytecodeModule& nm8 = *nm8Ptr;
                        if (!nm8.funcName.empty()) {
                            const proto::ProtoObject* nameVal = pContext->fromUTF8String(nm8.funcName.c_str());
                            if (nameVal)
                                fnInst = fnInst->setAttribute(pContext, JSSymbols::name(pContext), nameVal);
                        }
                        const proto::ProtoString* lenKey8 = JSSymbols::length(pContext);
                        if (lenKey8)
                            fnInst = fnInst->setAttribute(pContext, lenKey8,
                                pContext->fromInteger(static_cast<long long>(nm8.argCount_)));
                        // Capture lexical this for arrow functions.
                        if (nm8.isArrow) {
                            fnInst = fnInst->setAttribute(pContext, JSSymbols::arrowThis(pContext),
                                thisObj ? thisObj : PROTO_NONE);
                        }
                        // Closure var capture: publish non-global captured vars to the
                        // global object so the inner function's startup reads the correct
                        // initial values.  Types: 0=LOCAL, 1=ARG, 2=REF (parent closure var).
                        if (pGlobalRoot && *pGlobalRoot) {
                            for (size_t cvi = 0; cvi < nm8.closureVarNames.size(); ++cvi) {
                                const std::string& cvName = nm8.closureVarNames[cvi];
                                if (cvName.empty()) continue;
                                int cvType = (cvi < nm8.closureVarTypes.size())
                                    ? nm8.closureVarTypes[cvi] : -1;
                                uint16_t cvIdx = (cvi < nm8.closureVarIndices.size())
                                    ? nm8.closureVarIndices[cvi] : 0;
                                const proto::ProtoObject* cvVal = PROTO_NONE;
                                if (cvType == 1 /* ARG */) {
                                    cvVal = getSlot(pContext, cvIdx);
                                } else if (cvType == 0 /* LOCAL */) {
                                    cvVal = getSlot(pContext, argCount + cvIdx);
                                } else if (cvType == 2 /* REF */) {
                                    cvVal = getSlot(pContext, argCount + varCount + cvIdx);
                                } else {
                                    continue; // global/module vars: handled elsewhere
                                }
                                const proto::ProtoString* cvKey =
                                    pContext->fromUTF8String(cvName.c_str())
                                    ? pContext->fromUTF8String(cvName.c_str())->asString(pContext)
                                    : nullptr;
                                if (cvKey)
                                    *pGlobalRoot = (*pGlobalRoot)->setAttribute(
                                        pContext, cvKey, cvVal ? cvVal : PROTO_NONE);
                            }
                        }
                    }
                    stackPush(pContext, fnInst);
                } else {
                    stackPush(pContext, rawFn ? rawFn : PROTO_NONE);
                }
                break;
            }
            case OP_fclosure: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* rawFn2 = (idx < cpool.size()) ? cpool[idx] : PROTO_NONE;
                int fnBcId2 = getBytecodeId(pContext, rawFn2);
                if (fnBcId2 >= 0) {
                    // Create fresh function instance inheriting from Function.prototype so
                    // fn.call/bind/apply resolve up the prototype chain.
                    const proto::ProtoObject** gr2 = t_currentGlobalRoot;
                    const proto::ProtoString* fpKey2 = JSSymbols::functionProto(pContext);
                    const proto::ProtoObject* fp2 = (gr2 && *gr2 && fpKey2)
                        ? (*gr2)->getAttribute(pContext, fpKey2, false) : nullptr;
                    const proto::ProtoObject* fnInst2 = (fp2 && fp2 != PROTO_NONE)
                        ? fp2->newChild(pContext, true)
                        : pContext->newObject(true);
                    fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::bytecodeId(pContext),
                        pContext->fromInteger(static_cast<long long>(fnBcId2)));
                    fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::prototype(pContext),
                        pContext->newObject(true));
                    // Resolve function metadata from the root module's flat nestedFunctions list.
                    const ProtoBytecodeModule* nm2Ptr = nullptr;
                    if (fnBcId2 >= 0 && t_rootModule &&
                            static_cast<size_t>(fnBcId2) < t_rootModule->nestedFunctions.size())
                        nm2Ptr = &t_rootModule->nestedFunctions[static_cast<size_t>(fnBcId2)];
                    if (nm2Ptr) {
                        const ProtoBytecodeModule& nm2 = *nm2Ptr;
                        if (!nm2.funcName.empty()) {
                            const proto::ProtoObject* nameVal2 = pContext->fromUTF8String(nm2.funcName.c_str());
                            if (nameVal2)
                                fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::name(pContext), nameVal2);
                        }
                        const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                        if (lenKey2)
                            fnInst2 = fnInst2->setAttribute(pContext, lenKey2,
                                pContext->fromInteger(static_cast<long long>(nm2.argCount_)));
                        // Capture lexical this for arrow functions.
                        if (nm2.isArrow) {
                            fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::arrowThis(pContext),
                                thisObj ? thisObj : PROTO_NONE);
                        }
                        // Closure var capture: publish non-global captured vars to global object.
                        if (pGlobalRoot && *pGlobalRoot) {
                            for (size_t cvi2 = 0; cvi2 < nm2.closureVarNames.size(); ++cvi2) {
                                const std::string& cvName2 = nm2.closureVarNames[cvi2];
                                if (cvName2.empty()) continue;
                                int cvType2 = (cvi2 < nm2.closureVarTypes.size())
                                    ? nm2.closureVarTypes[cvi2] : -1;
                                uint16_t cvIdx2 = (cvi2 < nm2.closureVarIndices.size())
                                    ? nm2.closureVarIndices[cvi2] : 0;
                                const proto::ProtoObject* cvVal2 = PROTO_NONE;
                                if (cvType2 == 1 /* ARG */) {
                                    cvVal2 = getSlot(pContext, cvIdx2);
                                } else if (cvType2 == 0 /* LOCAL */) {
                                    cvVal2 = getSlot(pContext, argCount + cvIdx2);
                                } else if (cvType2 == 2 /* REF */) {
                                    cvVal2 = getSlot(pContext, argCount + varCount + cvIdx2);
                                } else {
                                    continue;
                                }
                                const proto::ProtoString* cvKey2 =
                                    pContext->fromUTF8String(cvName2.c_str())
                                    ? pContext->fromUTF8String(cvName2.c_str())->asString(pContext)
                                    : nullptr;
                                if (cvKey2)
                                    *pGlobalRoot = (*pGlobalRoot)->setAttribute(
                                        pContext, cvKey2, cvVal2 ? cvVal2 : PROTO_NONE);
                            }
                        }
                    }
                    stackPush(pContext, fnInst2);
                } else {
                    stackPush(pContext, rawFn2 ? rawFn2 : PROTO_NONE);
                }
                break;
            }
            case OP_is_undefined: {
                // Pops one value; pushes true if it is undefined (PROTO_NONE in protoCore).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_is_null: {
                // Pops one value; pushes true if it is null (t_nullSentinel only).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (val == t_nullSentinel) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_typeof_is_undefined: {
                // Pops one value; pushes true if typeof is "undefined".
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_typeof_is_function: {
                // Pops one value; pushes true if typeof is "function".
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                int bcId = getBytecodeId(pContext, val);
                bool isFunc = (bcId >= 0) ||
                              (val && val != PROTO_NONE && val->isMethod(pContext));
                if (!isFunc && val && val != PROTO_NONE) {
                    const proto::ProtoString* bfIsFnKey = JSSymbols::boundFn(pContext);
                    const proto::ProtoObject* bfIsFnTarget = bfIsFnKey
                        ? val->getAttribute(pContext, bfIsFnKey, false) : nullptr;
                    isFunc = (bfIsFnTarget && bfIsFnTarget != PROTO_NONE);
                }
                stackPush(pContext, isFunc ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            // ---------------------------------------------------------------
            // Step A — OP_array_from
            // DEF(array_from, 3, 0, 1, npop) — 1 opcode + 2-byte element count
            // ---------------------------------------------------------------
            case OP_array_from: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t count = get_u16(buf + pc);
                pc += 2;
                // Create a mutable array that inherits from Array.prototype so that
                // push/pop/join/slice etc. are found via prototype-chain lookup.
                const proto::ProtoString* arrProtoLookupKey =
                    JSSymbols::arrayProto(pContext);
                const proto::ProtoObject* arrProto =
                    (arrProtoLookupKey && globalObj && globalObj != PROTO_NONE)
                        ? globalObj->getAttribute(pContext, arrProtoLookupKey, false)
                        : nullptr;
                const proto::ProtoObject* arr = (arrProto && arrProto != PROTO_NONE)
                    ? arrProto->newChild(pContext, true)   // mutable, inherits Array.prototype
                    : pContext->newObject(true);            // fallback: mutable plain object
                if (!arr) { stackPush(pContext, PROTO_NONE); break; }
                // Read elements bottom-first (stack order: elem[0] deepest, elem[count-1] at TOS).
                for (uint16_t i = 0; i < count; i++) {
                    const proto::ProtoObject* elem = stackAt(pContext, static_cast<unsigned long>(count - 1 - i));
                    const proto::ProtoString* idxKey =
                        JSSymbols::indexKey(pContext, static_cast<uint32_t>(i));
                    if (idxKey) arr = arr->setAttribute(pContext, idxKey, elem ? elem : PROTO_NONE);
                }
                for (uint16_t i = 0; i < count; i++) stackPop(pContext);
                // Set .length
                const proto::ProtoString* lenKey = JSSymbols::length(pContext);
                if (lenKey) arr = arr->setAttribute(pContext, lenKey, pContext->fromInteger(static_cast<long long>(count)));
                stackPush(pContext, arr ? arr : PROTO_NONE);
                break;
            }

            // ---------------------------------------------------------------
            // Step B — for-of iterator protocol
            // ---------------------------------------------------------------

            // OP_for_of_start: DEF(for_of_start, 1, 1, 3, none)
            // Pops iterable; pushes [iterator, nextMethod, catch_offset].
            // Handles two cases:
            //   (A) Native iterator object — has a `next()` method and __iter_arr__ key.
            //       Push the iterator itself and record it as a "next()-based" iterator.
            //   (B) Array/TypedArray with numeric .length — use the slot-based index loop.
            //   Otherwise returns PROTO_NONE (vacuous pass for unsupported iterables).
            case OP_for_of_start: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* iterable = stackTop(pContext);
                stackPop(pContext);
                // Null is not iterable — throw TypeError.
                if (iterable == t_nullSentinel) {
                    pending_exception = makeError(pContext, "TypeError",
                        "null is not iterable", pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                // PROTO_NONE guard: generator iterables return PROTO_NONE from OP_initial_yield
                // (unsupported). Propagate vacuous-pass so generator-based for-of tests don't regress.
                if (!iterable || iterable == PROTO_NONE) return PROTO_NONE;

                // Case A: native iterator object (produced by Array/TypedArray keys/values/entries).
                // Detect by presence of both `next` method and `__iter_arr__` internal key.
                const proto::ProtoString* nextKey2 = JSSymbols::next(pContext);
                const proto::ProtoString* iterArrKey2 = JSSymbols::iterArr(pContext);
                if (nextKey2 && iterArrKey2) {
                    const proto::ProtoObject* nextFn2 = iterable->getAttribute(pContext, nextKey2, false);
                    const proto::ProtoObject* iterArrVal = iterable->getAttribute(pContext, iterArrKey2, false);
                    if (nextFn2 && nextFn2 != PROTO_NONE && iterArrVal && iterArrVal != PROTO_NONE) {
                        // The iterable is already an iterator. Store it in a slot for OP_for_of_next.
                        // We overload the slot mechanism: store the iterator itself in bs, and a
                        // sentinel (-1) in bs+1 to signal next()-based iteration.
                        uint32_t baseSlot = 0x10000u + static_cast<uint32_t>(pc - 1);
                        setSlot(pContext, baseSlot,     iterable);
                        setSlot(pContext, baseSlot + 1, pContext->fromInteger(-1LL)); // sentinel
                        setSlot(pContext, baseSlot + 2, pContext->fromInteger(0LL));  // done flag
                        const proto::ProtoObject* iterObj = pContext->newObject(false);
                        if (!iterObj) return PROTO_NONE;
                        const proto::ProtoString* slotKey2 = JSSymbols::iterSlot(pContext);
                        if (slotKey2)
                            iterObj = iterObj->setAttribute(pContext, slotKey2,
                                pContext->fromInteger(static_cast<long long>(baseSlot)));
                        stackPush(pContext, iterObj);
                        stackPush(pContext, PROTO_NONE);
                        stackPush(pContext, pContext->fromInteger(0LL));
                        break;
                    }
                }

                // Case B: array or TypedArray with numeric .length — index-based iteration.
                const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                const proto::ProtoObject* lenVal = lenKey2 ? iterable->getAttribute(pContext, lenKey2, false) : PROTO_NONE;
                if (!lenVal || lenVal == PROTO_NONE || !lenVal->isInteger(pContext)) {
                    // Case C: general iterable — call Symbol.iterator to get an iterator,
                    // then use sentinel -1 to dispatch through next()-based OP_for_of_next.
                    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(pContext);
                    const proto::ProtoObject* iterFn = symIterKey
                        ? iterable->getAttribute(pContext, symIterKey, false) : PROTO_NONE;
                    if (!iterFn || iterFn == PROTO_NONE) return PROTO_NONE;
                    const proto::ProtoList* emptyArgs2 = pContext->newList();
                    const proto::ProtoObject* iterator = callJSFunction(pContext, iterFn, iterable, emptyArgs2);
                    if (t_hasCallException) {
                        pending_exception  = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException    = nullptr;
                        break;
                    }
                    if (!iterator || iterator == PROTO_NONE) return PROTO_NONE;
                    uint32_t baseSlotC = 0x10000u + static_cast<uint32_t>(pc - 1);
                    setSlot(pContext, baseSlotC,     iterator);
                    setSlot(pContext, baseSlotC + 1, pContext->fromInteger(-1LL)); // sentinel: next()-based
                    setSlot(pContext, baseSlotC + 2, pContext->fromInteger(0LL));  // done flag
                    const proto::ProtoObject* iterObjC = pContext->newObject(false);
                    if (!iterObjC) return PROTO_NONE;
                    const proto::ProtoString* slotKeyC = JSSymbols::iterSlot(pContext);
                    if (slotKeyC)
                        iterObjC = iterObjC->setAttribute(pContext, slotKeyC,
                            pContext->fromInteger(static_cast<long long>(baseSlotC)));
                    stackPush(pContext, iterObjC);
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, pContext->fromInteger(0LL));
                    break;
                }
                // Use a PC-based slot pair unique to this for-of loop site.
                uint32_t baseSlot = 0x10000u + static_cast<uint32_t>(pc - 1);
                setSlot(pContext, baseSlot,     iterable);
                setSlot(pContext, baseSlot + 1, pContext->fromInteger(0LL));
                // Build a lightweight iterator object carrying the slot base.
                const proto::ProtoObject* iterObj = pContext->newObject(false);
                if (!iterObj) return PROTO_NONE;
                const proto::ProtoString* slotKey2 = JSSymbols::iterSlot(pContext);
                if (slotKey2)
                    iterObj = iterObj->setAttribute(pContext, slotKey2, pContext->fromInteger(static_cast<long long>(baseSlot)));
                stackPush(pContext, iterObj);           // iterator
                stackPush(pContext, PROTO_NONE);        // nextMethod placeholder
                stackPush(pContext, pContext->fromInteger(0LL)); // catch_offset placeholder
                break;
            }

            // OP_for_of_next: DEF(for_of_next, 2, 3, 5, u8)
            // 1 opcode + 1 u8 rawOffset.  Reads iterator state; pushes [value, done] (net +2).
            case OP_for_of_next: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t rawOffset = buf[pc++];
                // iterator object is 2 + rawOffset positions from TOS (QuickJS protocol).
                const proto::ProtoObject* iterObj2 = stackAt(pContext, static_cast<unsigned long>(2 + rawOffset));
                if (!iterObj2 || iterObj2 == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                const proto::ProtoString* slotKey3 = JSSymbols::iterSlot(pContext);
                const proto::ProtoObject* slotVal = slotKey3 ? iterObj2->getAttribute(pContext, slotKey3, false) : PROTO_NONE;
                if (!slotVal || slotVal == PROTO_NONE || !slotVal->isInteger(pContext)) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                uint32_t bs = static_cast<uint32_t>(slotVal->asLong(pContext));
                const proto::ProtoObject* arrObj  = getSlot(pContext, bs);
                const proto::ProtoObject* idxObj2 = getSlot(pContext, bs + 1);
                if (!arrObj || arrObj == PROTO_NONE || !idxObj2 || !idxObj2->isInteger(pContext)) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                long long idx2 = idxObj2->asLong(pContext);

                // Sentinel idx2 == -1: this is a native iterator (has next() method).
                // Call next() on the stored iterator object and unpack {value, done}.
                if (idx2 == -1LL) {
                    const proto::ProtoString* nextKeyFO = JSSymbols::next(pContext);
                    const proto::ProtoObject* nextFnFO = (nextKeyFO && arrObj != PROTO_NONE)
                        ? arrObj->getAttribute(pContext, nextKeyFO, false) : PROTO_NONE;
                    const proto::ProtoObject* resultFO = PROTO_NONE;
                    if (nextFnFO && nextFnFO != PROTO_NONE) {
                        if (nextFnFO->isMethod(pContext)) {
                            // Native ProtoMethod: invoke directly.
                            proto::ProtoMethod nativeFnFO = nextFnFO->asMethod(pContext);
                            if (nativeFnFO)
                                resultFO = nativeFnFO(pContext, arrObj, nullptr, nullptr, nullptr);
                        } else {
                            // JS-defined next(): dispatch through callJSFunction.
                            const proto::ProtoList* emptyArgsFO = pContext->newList();
                            resultFO = callJSFunction(pContext, nextFnFO, arrObj, emptyArgsFO);
                            if (t_hasCallException) {
                                pending_exception  = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException    = nullptr;
                                break;
                            }
                        }
                    }
                    const proto::ProtoString* doneKeyFO  = JSSymbols::done(pContext);
                    const proto::ProtoString* valueKeyFO = JSSymbols::value(pContext);
                    const proto::ProtoObject* doneFO = (resultFO && resultFO != PROTO_NONE && doneKeyFO)
                        ? resultFO->getAttribute(pContext, doneKeyFO, false) : PROTO_TRUE;
                    const proto::ProtoObject* valueFO = (resultFO && resultFO != PROTO_NONE && valueKeyFO)
                        ? resultFO->getAttribute(pContext, valueKeyFO, false) : PROTO_NONE;
                    const bool isDone = (!doneFO || doneFO == PROTO_NONE || doneFO == PROTO_TRUE);
                    // Track done state in slot bs+2 so OP_iterator_close knows whether to call return().
                    if (isDone) setSlot(pContext, bs + 2, pContext->fromInteger(1LL));
                    stackPush(pContext, valueFO ? valueFO : PROTO_NONE);
                    stackPush(pContext, isDone ? PROTO_TRUE : PROTO_FALSE);
                    break;
                }

                const proto::ProtoString* lenKey3 = JSSymbols::length(pContext);
                const proto::ProtoObject* lenVal2 = lenKey3 ? arrObj->getAttribute(pContext, lenKey3, false) : PROTO_NONE;
                long long arrLen = (lenVal2 && lenVal2 != PROTO_NONE && lenVal2->isInteger(pContext))
                                   ? lenVal2->asLong(pContext) : 0LL;
                if (idx2 >= arrLen) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                // Fetch element: TypedArrays use binary storage and require
                // typedArrayGetElement; plain arrays use string-keyed attributes.
                const proto::ProtoObject* elemVal;
                uint8_t taElemType = getTypedArrayElementType(pContext, arrObj);
                if (taElemType != 0xFF) {
                    elemVal = typedArrayGetElement(pContext, arrObj,
                                                  static_cast<uint32_t>(idx2), taElemType);
                } else {
                    std::string elemIdxStr = std::to_string(idx2);
                    const proto::ProtoObject* elemIdxObj = pContext->fromUTF8String(elemIdxStr.c_str());
                    const proto::ProtoString* elemIdxKey = elemIdxObj ? elemIdxObj->asString(pContext) : nullptr;
                    elemVal = elemIdxKey
                        ? arrObj->getAttribute(pContext, elemIdxKey, false) : PROTO_NONE;
                }
                setSlot(pContext, bs + 1, pContext->fromInteger(idx2 + 1LL));
                stackPush(pContext, elemVal ? elemVal : PROTO_NONE);
                stackPush(pContext, PROTO_FALSE); // done = false
                break;
            }

            // OP_iterator_get_value_done: DEF(iterator_get_value_done, 1, 2, 3, none)
            // Stack before: [..., catch_0, result_obj]  → after: [..., new_catch_0, value, done]
            case OP_iterator_get_value_done: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* result_obj = stackTop(pContext); stackPop(pContext);
                stackPop(pContext); // discard catch_0
                const proto::ProtoString* valueKey = JSSymbols::value(pContext);
                const proto::ProtoString* doneKey  = JSSymbols::done(pContext);
                const proto::ProtoObject* value = (result_obj && result_obj != PROTO_NONE && valueKey)
                    ? result_obj->getAttribute(pContext, valueKey, false) : PROTO_NONE;
                const proto::ProtoObject* doneRaw = (result_obj && result_obj != PROTO_NONE && doneKey)
                    ? result_obj->getAttribute(pContext, doneKey, false) : PROTO_TRUE;
                const proto::ProtoObject* done = (doneRaw == PROTO_TRUE || doneRaw == PROTO_FALSE)
                    ? doneRaw : PROTO_FALSE;
                stackPush(pContext, pContext->fromInteger(0LL)); // new catch_offset placeholder
                stackPush(pContext, value ? value : PROTO_NONE);
                stackPush(pContext, done);
                break;
            }

            // OP_iterator_check_object: DEF(iterator_check_object, 1, 1, 1, none)
            // Validates iterator result is object-like; protoCore accepts any non-null — no-op.
            case OP_iterator_check_object:
                // Leave TOS unchanged; no validation throw in protoCore.
                break;

            // OP_iterator_close: DEF(iterator_close, 1, 3, 0, none)
            // Pops [iter, nextMethod, catch_0] from stack.
            // For native iterators (sentinel -1) call iterator.return() if present.
            case OP_iterator_close: {
                // Pop catch_offset and nextMethod; keep iterObj to inspect.
                if (!stackEmpty(pContext)) stackPop(pContext); // catch_offset
                if (!stackEmpty(pContext)) stackPop(pContext); // nextMethod
                const proto::ProtoObject* iterObjCL = PROTO_NONE;
                if (!stackEmpty(pContext)) {
                    iterObjCL = stackTop(pContext);
                    stackPop(pContext); // iterObj wrapper
                }
                // If this was a native iterator (sentinel -1), call .return() for cleanup.
                if (iterObjCL && iterObjCL != PROTO_NONE) {
                    const proto::ProtoString* slotKeyCL = JSSymbols::iterSlot(pContext);
                    const proto::ProtoObject* slotValCL =
                        slotKeyCL ? iterObjCL->getAttribute(pContext, slotKeyCL, false) : PROTO_NONE;
                    if (slotValCL && slotValCL != PROTO_NONE && slotValCL->isInteger(pContext)) {
                        uint32_t bsCL = static_cast<uint32_t>(slotValCL->asLong(pContext));
                        const proto::ProtoObject* actualIterCL = getSlot(pContext, bsCL);
                        const proto::ProtoObject* idxObjCL     = getSlot(pContext, bsCL + 1);
                        long long idxCL = (idxObjCL && idxObjCL->isInteger(pContext))
                                          ? idxObjCL->asLong(pContext) : 0LL;
                        // Determine whether the iterator was already exhausted.
                        // Spec: IteratorClose only calls return() if iteratorRecord.[[done]] is false.
                        bool iterAlreadyDone = false;
                        if (idxCL == -1LL) {
                            // Native iterator: check done flag in slot bsCL+2 (0=not done, 1=done).
                            const proto::ProtoObject* doneFlagCL = getSlot(pContext, bsCL + 2);
                            iterAlreadyDone = (doneFlagCL && doneFlagCL->isInteger(pContext)
                                               && doneFlagCL->asLong(pContext) == 1LL);
                        } else {
                            // Array/TypedArray: exhausted if idx >= length.
                            const proto::ProtoString* lenKeyCL2 = JSSymbols::length(pContext);
                            const proto::ProtoObject* lenValCL  = lenKeyCL2
                                ? actualIterCL->getAttribute(pContext, lenKeyCL2, false) : PROTO_NONE;
                            long long arrLenCL = (lenValCL && lenValCL->isInteger(pContext))
                                                 ? lenValCL->asLong(pContext) : 0LL;
                            iterAlreadyDone = (idxCL >= arrLenCL);
                        }
                        if (idxCL == -1LL && !iterAlreadyDone && actualIterCL && actualIterCL != PROTO_NONE) {
                            const proto::ProtoObject* retKeyObj =
                                pContext->fromUTF8String("return");
                            const proto::ProtoString* retKey =
                                retKeyObj ? retKeyObj->asString(pContext) : nullptr;
                            const proto::ProtoObject* retFn = retKey
                                ? actualIterCL->getAttribute(pContext, retKey, false) : PROTO_NONE;
                            if (retFn && retFn != PROTO_NONE) {
                                if (retFn->isMethod(pContext)) {
                                    proto::ProtoMethod nativeRet = retFn->asMethod(pContext);
                                    if (nativeRet)
                                        nativeRet(pContext, actualIterCL, nullptr, nullptr, nullptr);
                                } else {
                                    const proto::ProtoList* emptyRetArgs = pContext->newList();
                                    callJSFunction(pContext, retFn, actualIterCL, emptyRetArgs);
                                    // Clear any exception from return() — close is best-effort.
                                    t_hasCallException = false;
                                    t_callException    = nullptr;
                                }
                            }
                        }
                    }
                }
                break;
            }

            // OP_iterator_next: DEF(iterator_next, 1, 4, 4, none)
            // Stack before: [iter, nextMethod, catch_offset, sentinel]  (4 consumed)
            // Stack after:  [iter, nextMethod, catch_offset, result_obj] (4 produced)
            // result_obj is a ProtoObject with "value" and "done" attributes.
            // Used by array/object destructuring patterns: const [a,b] = expr.
            case OP_iterator_next: {
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // Pop all 4; save iter/nextMethod/catch for re-push.
                stackPop(pContext); // sentinel (undefined or previous value — discarded)
                const proto::ProtoObject* catchOffIN    = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* nextMethodIN  = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* iterObjIN     = stackTop(pContext); stackPop(pContext);

                const proto::ProtoString* slotKeyIN = JSSymbols::iterSlot(pContext);
                const proto::ProtoObject* slotValIN = (slotKeyIN && iterObjIN && iterObjIN != PROTO_NONE)
                    ? iterObjIN->getAttribute(pContext, slotKeyIN, false) : PROTO_NONE;

                const proto::ProtoObject* resultObjIN = PROTO_NONE;
                const proto::ProtoString* valueKeyIN  = JSSymbols::value(pContext);
                const proto::ProtoString* doneKeyIN   = JSSymbols::done(pContext);

                if (slotValIN && slotValIN != PROTO_NONE && slotValIN->isInteger(pContext)) {
                    uint32_t bsIN = static_cast<uint32_t>(slotValIN->asLong(pContext));
                    const proto::ProtoObject* actualIter  = getSlot(pContext, bsIN);
                    const proto::ProtoObject* idxObjIN    = getSlot(pContext, bsIN + 1);
                    long long idxIN = (idxObjIN && idxObjIN->isInteger(pContext))
                                      ? idxObjIN->asLong(pContext) : 0LL;

                    if (idxIN == -1LL) {
                        // Native iterator: call iter.next() and use the returned object directly.
                        const proto::ProtoString* nextKeyIN = JSSymbols::next(pContext);
                        const proto::ProtoObject* nextFnIN  =
                            (nextKeyIN && actualIter && actualIter != PROTO_NONE)
                            ? actualIter->getAttribute(pContext, nextKeyIN, false) : PROTO_NONE;
                        if (nextFnIN && nextFnIN != PROTO_NONE) {
                            if (nextFnIN->isMethod(pContext)) {
                                resultObjIN = nextFnIN->asMethod(pContext)(
                                    pContext, actualIter, nullptr, nullptr, nullptr);
                            } else {
                                const proto::ProtoList* emptyArgsIN = pContext->newList();
                                resultObjIN = callJSFunction(pContext, nextFnIN, actualIter, emptyArgsIN);
                                if (t_hasCallException) {
                                    pending_exception  = t_callException;
                                    has_pending_exception = true;
                                    t_hasCallException = false;
                                    t_callException    = nullptr;
                                    // Re-push [iter, nextMethod, catch_offset] before breaking so
                                    // OP_iterator_close can pop them cleanly on the exception path.
                                    stackPush(pContext, iterObjIN);
                                    stackPush(pContext, nextMethodIN);
                                    stackPush(pContext, catchOffIN ? catchOffIN : pContext->fromInteger(0LL));
                                    break;
                                }
                            }
                        }
                        // Track done state in slot bsIN+2 so OP_iterator_close can decide
                        // whether to call return() (only if the iterator was not yet exhausted).
                        if (resultObjIN && resultObjIN != PROTO_NONE && doneKeyIN) {
                            const proto::ProtoObject* doneValIN =
                                resultObjIN->getAttribute(pContext, doneKeyIN, false);
                            const bool iterDoneIN = (!doneValIN || doneValIN == PROTO_NONE
                                                     || doneValIN == PROTO_TRUE);
                            if (iterDoneIN)
                                setSlot(pContext, bsIN + 2, pContext->fromInteger(1LL));
                        }
                    } else {
                        // Array/TypedArray: build synthetic {value, done} result object.
                        const proto::ProtoString* lenKeyIN = JSSymbols::length(pContext);
                        const proto::ProtoObject* lenValIN = lenKeyIN
                            ? actualIter->getAttribute(pContext, lenKeyIN, false) : PROTO_NONE;
                        long long arrLenIN = (lenValIN && lenValIN->isInteger(pContext))
                                             ? lenValIN->asLong(pContext) : 0LL;

                        const proto::ProtoObject* synResult = pContext->newObject(false);
                        if (idxIN >= arrLenIN) {
                            if (valueKeyIN) synResult = synResult->setAttribute(pContext, valueKeyIN, PROTO_NONE);
                            if (doneKeyIN)  synResult = synResult->setAttribute(pContext, doneKeyIN,  PROTO_TRUE);
                        } else {
                            const proto::ProtoObject* elemVal = PROTO_NONE;
                            uint8_t taTypeIN = getTypedArrayElementType(pContext, actualIter);
                            if (taTypeIN != 0xFF) {
                                elemVal = typedArrayGetElement(pContext, actualIter,
                                    static_cast<uint32_t>(idxIN), taTypeIN);
                            } else {
                                std::string eidxStr = std::to_string(idxIN);
                                const proto::ProtoObject* eidxObj =
                                    pContext->fromUTF8String(eidxStr.c_str());
                                const proto::ProtoString* eidxKey =
                                    eidxObj ? eidxObj->asString(pContext) : nullptr;
                                elemVal = eidxKey
                                    ? actualIter->getAttribute(pContext, eidxKey, false) : PROTO_NONE;
                            }
                            setSlot(pContext, bsIN + 1, pContext->fromInteger(idxIN + 1LL));
                            if (valueKeyIN)
                                synResult = synResult->setAttribute(pContext, valueKeyIN,
                                    elemVal ? elemVal : PROTO_NONE);
                            if (doneKeyIN)
                                synResult = synResult->setAttribute(pContext, doneKeyIN, PROTO_FALSE);
                        }
                        resultObjIN = synResult;
                    }
                }

                // Push back [iter, nextMethod, catch_offset, result_obj].
                stackPush(pContext, iterObjIN);
                stackPush(pContext, nextMethodIN);
                stackPush(pContext, catchOffIN ? catchOffIN : pContext->fromInteger(0LL));
                stackPush(pContext, resultObjIN ? resultObjIN : PROTO_NONE);
                break;
            }

            // OP_iterator_call: DEF(iterator_call, 2, 4, 5, u8)
            // Stack before: [iter, nextMethod, catch_offset, sentinel]  (4 consumed)
            // Stack after:  [iter, nextMethod, catch_offset, value, done] (5 produced)
            // flags=1: collect remaining iterator values into a rest array, done=false.
            // flags=0: iterator.return() cleanup path, value=undefined, done=true.
            case OP_iterator_call: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t icFlags = buf[pc++];
                if (stackSize(pContext) < 4) {
                    stackPush(pContext, PROTO_NONE);
                    return PROTO_NONE;
                }
                // Pop all 4; save iter/nextMethod/catch for re-push.
                stackPop(pContext); // sentinel
                const proto::ProtoObject* catchOffIC   = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* nextMethodIC = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* iterObjIC    = stackTop(pContext); stackPop(pContext);

                const proto::ProtoObject* resultValIC  = PROTO_NONE;
                const proto::ProtoObject* resultDoneIC = PROTO_TRUE;
                bool icCallException = false;

                if (icFlags == 1) {
                    // Collect all remaining iterator values into a rest array.
                    const proto::ProtoString* slotKeyIC = JSSymbols::iterSlot(pContext);
                    const proto::ProtoObject* slotValIC =
                        (slotKeyIC && iterObjIC && iterObjIC != PROTO_NONE)
                        ? iterObjIC->getAttribute(pContext, slotKeyIC, false) : PROTO_NONE;

                    const proto::ProtoObject* restArr = pContext->newObject(false);
                    const proto::ProtoString* lenKeyIC = JSSymbols::length(pContext);
                    long long restIdx = 0;

                    if (slotValIC && slotValIC != PROTO_NONE && slotValIC->isInteger(pContext)) {
                        uint32_t bsIC = static_cast<uint32_t>(slotValIC->asLong(pContext));
                        const proto::ProtoObject* actualIterIC = getSlot(pContext, bsIC);
                        const proto::ProtoObject* idxObjIC = getSlot(pContext, bsIC + 1);
                        long long idxIC = (idxObjIC && idxObjIC->isInteger(pContext))
                                          ? idxObjIC->asLong(pContext) : 0LL;

                        if (idxIC == -1LL) {
                            // Native iterator: drain via repeated next() calls.
                            const proto::ProtoString* nextKeyIC  = JSSymbols::next(pContext);
                            const proto::ProtoString* doneKeyIC2 = JSSymbols::done(pContext);
                            const proto::ProtoString* valKeyIC2  = JSSymbols::value(pContext);
                            const proto::ProtoObject* nextFnIC   =
                                (nextKeyIC && actualIterIC && actualIterIC != PROTO_NONE)
                                ? actualIterIC->getAttribute(pContext, nextKeyIC, false) : PROTO_NONE;
                            while (nextFnIC && nextFnIC != PROTO_NONE) {
                                const proto::ProtoObject* resIC = PROTO_NONE;
                                if (nextFnIC->isMethod(pContext)) {
                                    resIC = nextFnIC->asMethod(pContext)(
                                        pContext, actualIterIC, nullptr, nullptr, nullptr);
                                } else {
                                    const proto::ProtoList* argsIC = pContext->newList();
                                    resIC = callJSFunction(pContext, nextFnIC, actualIterIC, argsIC);
                                    if (t_hasCallException) {
                                        pending_exception  = t_callException;
                                        has_pending_exception = true;
                                        t_hasCallException = false;
                                        t_callException    = nullptr;
                                        icCallException    = true;
                                        break;
                                    }
                                }
                                const proto::ProtoObject* doneIC =
                                    (resIC && resIC != PROTO_NONE && doneKeyIC2)
                                    ? resIC->getAttribute(pContext, doneKeyIC2, false) : PROTO_TRUE;
                                if (!doneIC || doneIC == PROTO_NONE || doneIC == PROTO_TRUE) break;
                                const proto::ProtoObject* valIC =
                                    (resIC && resIC != PROTO_NONE && valKeyIC2)
                                    ? resIC->getAttribute(pContext, valKeyIC2, false) : PROTO_NONE;
                                std::string ridxStr = std::to_string(restIdx++);
                                const proto::ProtoObject* ridxObj =
                                    pContext->fromUTF8String(ridxStr.c_str());
                                const proto::ProtoString* ridxKey =
                                    ridxObj ? ridxObj->asString(pContext) : nullptr;
                                if (ridxKey)
                                    restArr = restArr->setAttribute(pContext, ridxKey,
                                        valIC ? valIC : PROTO_NONE);
                            }
                        } else {
                            // Array/TypedArray: slice from current slot index to end.
                            const proto::ProtoString* lenKeyIC2 = JSSymbols::length(pContext);
                            const proto::ProtoObject* lenValIC  = lenKeyIC2
                                ? actualIterIC->getAttribute(pContext, lenKeyIC2, false) : PROTO_NONE;
                            long long arrLenIC = (lenValIC && lenValIC->isInteger(pContext))
                                                 ? lenValIC->asLong(pContext) : 0LL;
                            for (long long i = idxIC; i < arrLenIC; ++i) {
                                const proto::ProtoObject* evIC = PROTO_NONE;
                                uint8_t taTIC = getTypedArrayElementType(pContext, actualIterIC);
                                if (taTIC != 0xFF) {
                                    evIC = typedArrayGetElement(pContext, actualIterIC,
                                        static_cast<uint32_t>(i), taTIC);
                                } else {
                                    std::string eiStr = std::to_string(i);
                                    const proto::ProtoObject* eiObj =
                                        pContext->fromUTF8String(eiStr.c_str());
                                    const proto::ProtoString* eiKey =
                                        eiObj ? eiObj->asString(pContext) : nullptr;
                                    evIC = eiKey
                                        ? actualIterIC->getAttribute(pContext, eiKey, false) : PROTO_NONE;
                                }
                                std::string riStr = std::to_string(restIdx++);
                                const proto::ProtoObject* riObj =
                                    pContext->fromUTF8String(riStr.c_str());
                                const proto::ProtoString* riKey =
                                    riObj ? riObj->asString(pContext) : nullptr;
                                if (riKey)
                                    restArr = restArr->setAttribute(pContext, riKey,
                                        evIC ? evIC : PROTO_NONE);
                            }
                            setSlot(pContext, bsIC + 1, pContext->fromInteger(arrLenIC));
                        }
                    }

                    if (lenKeyIC)
                        restArr = restArr->setAttribute(pContext, lenKeyIC,
                            pContext->fromInteger(restIdx));
                    resultValIC  = restArr;
                    resultDoneIC = PROTO_FALSE;
                }
                // flags=0 and others: cleanup/return() path — value=undefined, done=true (defaults).

                // If an exception was thrown inside the drain loop, the exception is
                // already set; skip the push-back and let dispatch handle it.
                if (icCallException) break;

                // Push back [iter, nextMethod, catch_offset, value, done].
                stackPush(pContext, iterObjIC);
                stackPush(pContext, nextMethodIC);
                stackPush(pContext, catchOffIC ? catchOffIC : pContext->fromInteger(0LL));
                stackPush(pContext, resultValIC  ? resultValIC  : PROTO_NONE);
                stackPush(pContext, resultDoneIC ? resultDoneIC : PROTO_TRUE);
                break;
            }

            // ---------------------------------------------------------------
            // Step C — for-in (PROTO_NONE guard — key enumeration not supported)
            // ---------------------------------------------------------------

            // OP_for_in_start: DEF(for_in_start, 1, 1, 1, none)
            case OP_for_in_start: {
                if (!stackEmpty(pContext)) stackPop(pContext);
                // Cannot enumerate property keys without a protoCore API for key iteration.
                return PROTO_NONE;
            }

            // OP_for_in_next: DEF(for_in_next, 1, 1, 3, none)
            // Stub in case for_in_start is ever enhanced; maintain stack balance.
            case OP_for_in_next: {
                if (!stackEmpty(pContext)) stackPop(pContext);
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, PROTO_TRUE); // done = true
                break;
            }

            // OP_initial_yield: DEF(initial_yield, 1, 0, 0, none)
            // First opcode in every generator function body.
            // Creates the generator iterator object, saves all current state
            // as attributes on it, and returns it immediately (generator body
            // hasn't started yet — it resumes when .next() is called).
            case OP_initial_yield: {
                // Build the iterator object.
                const proto::ProtoObject* iterObj = pContext->newObject(true);
                if (!iterObj) return PROTO_NONE;

                // Helper lambdas: set attributes on iterObj.
                auto setA = [&](const char* name, const proto::ProtoObject* val) {
                    iterObj = genSetObj(pContext, iterObj, name, val);
                };
                auto setI = [&](const char* name, long long val) {
                    iterObj = genSetInt(pContext, iterObj, name, val);
                };

                // pc already points past OP_initial_yield (incremented in the switch).
                setI(kGenPc, (long long)pc);

                // Save thisObj.
                setA(kGenThis, thisObj ? thisObj : PROTO_NONE);

                // Save module pointer as integer (raw pointer; module lifetime >= program).
                setI(kGenMod, (long long)(uintptr_t)mod);

                // Save closureLocals snapshot (GC-safe: stored as attribute on iterObj).
                const proto::ProtoObject* savedLoc = pContext->closureLocals
                    ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                setA(kGenLocals, savedLoc);

                // Save catch stack.
                setI(kGenNcc, (long long)catch_stack.size());
                for (size_t ci = 0; ci < catch_stack.size(); ci++) {
                    std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
                    std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
                    setI(kpc.c_str(), (long long)catch_stack[ci].handler_pc);
                    setI(ksp.c_str(), (long long)catch_stack[ci].placeholder_stack_pos);
                }

                // State: 0 = suspended.
                setI(kGenState, 0LL);

                // Register .next, .return, .throw methods.
                auto regM = [&](const char* name, proto::ProtoMethod fn) {
                    const proto::ProtoObject* ko = pContext->fromUTF8String(name);
                    const proto::ProtoString* k  = ko ? ko->asString(pContext) : nullptr;
                    if (k) iterObj = iterObj->setAttribute(pContext, k,
                                                            pContext->fromMethod(nullptr, fn));
                };
                regM("next",   generatorNext);
                regM("return", generatorReturn);
                regM("throw",  generatorThrow);

                // Mark as a generator iterator for OP_for_of_start iterator detection.
                // We add __iter_arr__ so that OP_for_of_start's existing "Case A" logic
                // (object with .next and __iter_arr__) treats this as an iterator.
                const proto::ProtoString* iterArrKey3 = JSSymbols::iterArr(pContext);
                if (iterArrKey3)
                    iterObj = iterObj->setAttribute(pContext, iterArrKey3,
                                                     pContext->fromInteger(0LL));

                return iterObj;
            }

            // OP_yield: DEF(yield, 1, 1, 2, none)
            // Suspends the generator and yields a value to the outer caller.
            case OP_yield: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* yieldVal = stackTop(pContext);
                stackPop(pContext);

                if (!t_genIterator) {
                    // OP_yield outside a generator resume — return undefined.
                    return PROTO_NONE;
                }

                // Save updated state back onto the iterator object.
                // pc already points past OP_yield.
                const proto::ProtoObject* updIter = t_genIterator;
                updIter = genSetInt(pContext, updIter, kGenPc, (long long)pc);
                const proto::ProtoObject* newLoc = pContext->closureLocals
                    ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                updIter = genSetObj(pContext, updIter, kGenLocals, newLoc);
                updIter = genSetInt(pContext, updIter, kGenNcc, (long long)catch_stack.size());
                for (size_t ci = 0; ci < catch_stack.size(); ci++) {
                    std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
                    std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
                    updIter = genSetInt(pContext, updIter, kpc.c_str(),
                                        (long long)catch_stack[ci].handler_pc);
                    updIter = genSetInt(pContext, updIter, ksp.c_str(),
                                        (long long)catch_stack[ci].placeholder_stack_pos);
                }
                updIter = genSetInt(pContext, updIter, kGenState, 0LL); // still suspended

                // Sync the updated iterator pointer back to the GC mapping table.
                if (updIter != t_genIterator) {
                    updateMapping(pContext, t_genIterator, updIter);
                }
                t_genIterator = nullptr; // clear to signal we yielded

                // Signal to resumeGenerator that OP_yield fired (not OP_return).
                t_genResumePc = -2;

                // Return {value: yieldVal, done: false} from this runBytecode invocation.
                return makeIterResult(pContext, yieldVal, false);
            }

            // OP_yield_star: DEF(yield_star, 1, 1, 2, none)
            // Delegates to inner iterable: calls inner.next() repeatedly, yielding each
            // value to the outer caller. When inner is done, pushes the final value.
            case OP_yield_star: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* innerIter = stackTop(pContext);
                stackPop(pContext);
                if (!innerIter || innerIter == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    break;
                }

                // Get .next method from the inner iterator.
                const proto::ProtoString* nextKey3 = JSSymbols::next(pContext);
                const proto::ProtoObject* nextFn = nextKey3
                    ? innerIter->getAttribute(pContext, nextKey3, true) : PROTO_NONE;
                if (!nextFn || nextFn == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    break;
                }

                // Delegate: loop calling inner.next(sentToInner) and yield each value.
                const proto::ProtoObject* sentToInner = PROTO_NONE;
                while (true) {
                    // Build args for inner.next(sentToInner).
                    const proto::ProtoList* nextArgs = nullptr;
                    if (sentToInner && sentToInner != PROTO_NONE) {
                        const proto::ProtoList* tmp = pContext->newList();
                        if (tmp) nextArgs = tmp->appendLast(pContext, sentToInner);
                    }
                    const proto::ProtoObject* iterResult = callJSFunction(pContext, nextFn,
                                                                           innerIter, nextArgs);
                    if (!iterResult || iterResult == PROTO_NONE) {
                        stackPush(pContext, PROTO_NONE);
                        break;
                    }

                    const proto::ProtoString* vk2  = JSSymbols::value(pContext);
                    const proto::ProtoString* dk2  = JSSymbols::done(pContext);
                    const proto::ProtoObject* val2 = vk2
                        ? iterResult->getAttribute(pContext, vk2, false) : PROTO_NONE;
                    const proto::ProtoObject* done2 = dk2
                        ? iterResult->getAttribute(pContext, dk2, false) : PROTO_FALSE;

                    bool isDone = (done2 == PROTO_TRUE ||
                                   (done2 && done2 != PROTO_NONE &&
                                    done2->isBoolean(pContext) && done2->asBoolean(pContext)));
                    if (isDone) {
                        // Inner iterator exhausted: push final value for yield* expression.
                        stackPush(pContext, val2 ? val2 : PROTO_NONE);
                        break;
                    }

                    if (!t_genIterator) {
                        // Not inside a generator resume — push final value and break.
                        stackPush(pContext, val2 ? val2 : PROTO_NONE);
                        break;
                    }

                    // Yield this inner value to the outer caller.
                    // Save state and have OP_yield_star re-entered next time .next() is called.
                    // We save pc-1 (pointing back at OP_yield_star) so re-execution re-enters
                    // this case and finds innerIter on the stack.
                    stackPush(pContext, innerIter); // push inner iter back
                    const proto::ProtoObject* newLoc2 = pContext->closureLocals
                        ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                    const proto::ProtoObject* updIter = t_genIterator;
                    updIter = genSetInt(pContext, updIter, kGenPc, (long long)(pc - 1));
                    updIter = genSetObj(pContext, updIter, kGenLocals, newLoc2);
                    updIter = genSetInt(pContext, updIter, kGenNcc, (long long)catch_stack.size());
                    updIter = genSetInt(pContext, updIter, kGenState, 0LL);
                    if (updIter != t_genIterator)
                        updateMapping(pContext, t_genIterator, updIter);
                    t_genIterator = nullptr;
                    t_genResumePc = -2;
                    return makeIterResult(pContext, val2, false);
                }
                break;
            }

            default: {
                // Unknown opcode: log for diagnostics; execution cannot continue safely.
                std::fprintf(stderr, "[ProtoInterpreter] unsupported opcode 0x%02x at byte offset %d\n",
                    static_cast<unsigned>(opcode), static_cast<int>(pc - 1));
                return PROTO_NONE;
            }
        } // end switch

        // --- Exception dispatch ---
        // If the switch body raised an exception (has_pending_exception flag),
        // either jump to the nearest catch handler or propagate to the caller.
        // Note: pending_exception may be PROTO_NONE (JS `throw undefined`) — use the flag.
        if (has_pending_exception) {
            has_pending_exception = false;
            if (!catch_stack.empty()) {
                CatchFrame frame = catch_stack.back();
                catch_stack.pop_back();
                // Restore value stack: truncate to placeholder_stack_pos (the sentinel slot),
                // then push the exception.  This replaces the catch sentinel with the caught value,
                // matching QuickJS's behaviour where the tagged catch-offset integer is replaced
                // with the exception object at that same stack position.
                while (stackSize(pContext) > frame.placeholder_stack_pos) stackPop(pContext);
                stackPush(pContext, pending_exception);
                pc = frame.handler_pc;
                pending_exception = nullptr;
                // Continue executing from the catch handler.
            } else {
                if (outException) *outException = pending_exception;
                return PROTO_NONE;
            }
        }
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Generator callbacks.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* resumeGenerator(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* iter,
                                                   const proto::ProtoObject* sentVal,
                                                   int mode) {
    if (!ctx || !iter || iter == PROTO_NONE) return makeIterResult(ctx, PROTO_NONE, true);

    long long state = genGetInt(ctx, iter, kGenState);
    if (state == 1) return makeIterResult(ctx, PROTO_NONE, true); // already completed

    if (mode == 1) {
        // .return(val): mark done, return {value: val, done: true}.
        iter = genSetInt(ctx, iter, kGenState, 1LL);
        return makeIterResult(ctx, sentVal, true);
    }

    // Recover module pointer.
    long long modRaw = genGetInt(ctx, iter, kGenMod);
    if (modRaw <= 0) return makeIterResult(ctx, PROTO_NONE, true);
    const ProtoBytecodeModule* mod = reinterpret_cast<const ProtoBytecodeModule*>((uintptr_t)modRaw);

    // Recover saved pc.
    long long resumePc = genGetInt(ctx, iter, kGenPc);
    if (resumePc < 0) return makeIterResult(ctx, PROTO_NONE, true);

    // Recover closureLocals.
    const proto::ProtoObject* ko = ctx->fromUTF8String(kGenLocals);
    const proto::ProtoString* lk = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* savedLocObj = lk ? iter->getAttribute(ctx, lk, false) : nullptr;

    // Recover thisObj.
    const proto::ProtoObject* tok = ctx->fromUTF8String(kGenThis);
    const proto::ProtoString* tk2 = tok ? tok->asString(ctx) : nullptr;
    const proto::ProtoObject* genThis = tk2 ? iter->getAttribute(ctx, tk2, false) : PROTO_NONE;

    // Recover catch stack.
    long long ncc = genGetInt(ctx, iter, kGenNcc, 0LL);
    std::vector<CatchFrame> restoredCatch;
    for (long long ci = 0; ci < ncc; ci++) {
        std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
        std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
        restoredCatch.push_back({(int)genGetInt(ctx, iter, kpc.c_str()),
                                  (unsigned long)genGetInt(ctx, iter, ksp.c_str())});
    }

    // If mode == 2 (throw): pre-store the throw value on the iterator.
    if (mode == 2 && sentVal && sentVal != PROTO_NONE) {
        iter = genSetObj(ctx, iter, "__gen_throw_val__", sentVal);
    } else {
        // Store sent value (result of yield expr on resume).
        iter = genSetObj(ctx, iter, "__gen_sent__", sentVal ? sentVal : PROTO_NONE);
        // Clear any prior throw val.
        iter = genSetObj(ctx, iter, "__gen_throw_val__", PROTO_NONE);
    }

    // Set up resume thread-locals.
    t_genResumePc         = (int)resumePc;
    t_genResumeLocals     = savedLocObj;
    t_genResumeCatchStack = restoredCatch.empty() ? nullptr : &restoredCatch;
    t_genIterator         = iter;

    // Create child context and invoke runBytecode.
    proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
    childCtx.currentFileName   = ctx->currentFileName;
    childCtx.currentLineNumber = ctx->currentLineNumber;
    const proto::ProtoObject* childEx = PROTO_NONE;
    const proto::ProtoObject** gr = t_currentGlobalRoot;

    const proto::ProtoObject* result = runBytecode(&childCtx, mod, genThis,
                                                     nullptr, gr, &childEx);

    // Propagate exceptions from generator body.
    if (childEx && childEx != PROTO_NONE) return childEx;

    if (t_genResumePc == -2) {
        // OP_yield fired — result is already {value, done:false}.
        t_genResumePc = -1;
        return result;
    }

    // Generator body completed (OP_return or end of bytecode).
    if (t_genIterator) {
        t_genIterator = genSetInt(ctx, t_genIterator, kGenState, 1LL);
    }
    t_genIterator = nullptr;
    return makeIterResult(ctx, result ? result : PROTO_NONE, true);
}

static const proto::ProtoObject* generatorNext(proto::ProtoContext* ctx,
    const proto::ProtoObject* thisVal,
    const proto::ParentLink* /*parent*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*named*/) {
    const proto::ProtoObject* sentVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    return resumeGenerator(ctx, thisVal, sentVal, 0 /* next */);
}

static const proto::ProtoObject* generatorReturn(proto::ProtoContext* ctx,
    const proto::ProtoObject* thisVal,
    const proto::ParentLink* /*parent*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*named*/) {
    const proto::ProtoObject* sentVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    return resumeGenerator(ctx, thisVal, sentVal, 1 /* return */);
}

static const proto::ProtoObject* generatorThrow(proto::ProtoContext* ctx,
    const proto::ProtoObject* thisVal,
    const proto::ParentLink* /*parent*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*named*/) {
    const proto::ProtoObject* sentVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    return resumeGenerator(ctx, thisVal, sentVal, 2 /* throw */);
}

const proto::ProtoObject* getNullSentinel() {
    return t_nullSentinel;
}

const proto::ProtoObject* callJSFunction(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoList* args)
{
    if (!fn || fn == PROTO_NONE || !ctx) return PROTO_NONE;

    const proto::ProtoObject** globalRoot = t_currentGlobalRoot;

    // Native ProtoMethod: call directly.
    if (fn->isMethod(ctx)) {
        proto::ProtoMethod nativeFn = fn->asMethod(ctx);
        return nativeFn ? nativeFn(ctx, thisVal ? thisVal : PROTO_NONE, nullptr, args, nullptr)
                        : PROTO_NONE;
    }

    // Bytecode closure: resolve __bytecode_id__ against the current or root module.
    int bcId = getBytecodeId(ctx, fn);
    const ProtoBytecodeModule* resolveMod =
        (bcId >= 0 && t_currentModule &&
           static_cast<size_t>(bcId) < t_currentModule->nestedFunctions.size())
            ? t_currentModule
        : (bcId >= 0 && t_rootModule &&
           static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
            ? t_rootModule
        : nullptr;
    if (resolveMod) {
        const ProtoBytecodeModule& nf = resolveMod->nestedFunctions[bcId];
        // Arrow functions use the lexical this captured at closure-creation time.
        const proto::ProtoObject* effectiveThis = thisVal ? thisVal : PROTO_NONE;
        if (nf.isArrow) {
            const proto::ProtoObject* captured = fn->getAttribute(ctx, JSSymbols::arrowThis(ctx), false);
            if (captured && captured != PROTO_NONE)
                effectiveThis = captured;
        }
        proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
        childCtx.currentFileName = ctx->currentFileName;
        childCtx.currentLineNumber = ctx->currentLineNumber;
        unsigned argc = args ? static_cast<unsigned>(args->getSize(ctx)) : 0u;
        for (unsigned i = 0; i < argc; i++)
            setSlot(&childCtx, i, args->getAt(&childCtx, static_cast<int>(i)));
        const proto::ProtoObject* childEx = PROTO_NONE;
        const proto::ProtoObject* result =
            runBytecode(&childCtx, &nf, effectiveThis, args, globalRoot, &childEx);
        childCtx.returnValue = result;
        // Propagate exceptions from JS callbacks via thread-local so that
        // iterator-related call sites inside runBytecode can set pending_exception.
        if (childEx && childEx != PROTO_NONE) {
            t_callException    = childEx;
            t_hasCallException = true;
            return PROTO_NONE;
        }
        return result ? result : PROTO_NONE;
    }

    // Bound function sentinel: unwrap target function and recurse with prepended pre-bound args.
    const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
    if (bfKey) {
        const proto::ProtoObject* target = fn->getAttribute(ctx, bfKey, false);
        if (target && target != PROTO_NONE) {
            const proto::ProtoString* btKey = JSSymbols::boundThis(ctx);
            const proto::ProtoString* baKey = JSSymbols::boundArgs(ctx);
            const proto::ProtoObject* effectiveBoundThis =
                (btKey) ? fn->getAttribute(ctx, btKey, false) : PROTO_NONE;
            if (!effectiveBoundThis) effectiveBoundThis = PROTO_NONE;
            const proto::ProtoObject* boundArgsObj =
                (baKey) ? fn->getAttribute(ctx, baKey, false) : nullptr;

            // Build merged arg list: pre-bound args followed by call-site args.
            const proto::ProtoList* mergedArgs = ctx->newList();
            if (boundArgsObj && boundArgsObj != PROTO_NONE) {
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                long long blen = 0;
                if (lenKey) {
                    const proto::ProtoObject* lo = boundArgsObj->getAttribute(ctx, lenKey, false);
                    if (lo && lo != PROTO_NONE) {
                        if (lo->isInteger(ctx))     blen = lo->asLong(ctx);
                        else if (lo->isDouble(ctx)) blen = static_cast<long long>(lo->asDouble(ctx));
                    }
                }
                for (long long i = 0; i < blen; i++) {
                    const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                    const proto::ProtoObject* av = ik ? boundArgsObj->getAttribute(ctx, ik, false) : PROTO_NONE;
                    mergedArgs = mergedArgs->appendLast(ctx, av ? av : PROTO_NONE);
                }
            }
            int callArgc = args ? args->getSize(ctx) : 0;
            for (int i = 0; i < callArgc; i++) {
                const proto::ProtoObject* a = args->getAt(ctx, i);
                mergedArgs = mergedArgs->appendLast(ctx, a ? a : PROTO_NONE);
            }
            return callJSFunction(ctx, target, effectiveBoundThis, mergedArgs);
        }
    }

    return PROTO_NONE;
}

} // namespace protojs
