#include "RegExpPrototype.h"
#include "RegExpStringIterator.h"
#include "ArrayPrototype.h"
#include "JSSymbols.h"
#include "JSContext.h"
#include "TypeBridge.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
extern "C" {
#include "libregexp.h"
}
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Convert any ProtoObject to its string representation. */
static std::string objToStr(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || obj == getUndefinedSentinel()) return "undefined";
    if (obj == getNullSentinel()) return "null";
    std::string r;
    if (obj->isString(ctx)) {
        obj->asString(ctx)->toUTF8String(ctx, r);
        return r;
    }
    if (obj->isInteger(ctx)) return std::to_string(obj->asLong(ctx));
    if (obj->isDouble(ctx)) {
        double d = obj->asDouble(ctx);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", d);
        return buf;
    }
    if (obj->isBoolean(ctx)) return obj->asBoolean(ctx) ? "true" : "false";
    // String wrapper object — unwrap via __primitive_value__ sidecar so
    // `regex[Symbol.split](new String('hello'))` sees 'hello' instead of ''.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = obj->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                pv->asString(ctx)->toUTF8String(ctx, r);
                return r;
            }
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// UTF-8 ↔ UTF-16 conversion
// ---------------------------------------------------------------------------

static std::vector<uint16_t> utf8ToUTF16(const std::string& s) {
    std::vector<uint16_t> out;
    for (size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int n;
        if      (c < 0x80) { cp = c;        n = 1; }
        else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
        else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
        else               { cp = c & 0x07; n = 4; }
        for (int j = 1; j < n && i + j < s.size(); j++)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3F);
        i += n;
        if (cp < 0x10000) {
            out.push_back(static_cast<uint16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

static std::string utf16ToUTF8(const std::vector<uint16_t>& u,
                                size_t from = 0, size_t to = SIZE_MAX) {
    if (to > u.size()) to = u.size();
    std::string out;
    for (size_t i = from; i < to; ) {
        uint32_t cp;
        uint16_t h = u[i++];
        if (h >= 0xD800 && h <= 0xDBFF && i < to &&
                u[i] >= 0xDC00 && u[i] <= 0xDFFF) {
            cp = 0x10000u + (static_cast<uint32_t>(h - 0xD800) << 10)
                           + (u[i++] - 0xDC00);
        } else {
            cp = h;
        }
        if      (cp < 0x80)    { out += static_cast<char>(cp); }
        else if (cp < 0x800)   { out += static_cast<char>(0xC0 | (cp >> 6));
                                  out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12));
                                  out += static_cast<char>(0x80 | ((cp >>  6) & 0x3F));
                                  out += static_cast<char>(0x80 | ( cp        & 0x3F)); }
        else                   { out += static_cast<char>(0xF0 | (cp >> 18));
                                  out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                                  out += static_cast<char>(0x80 | ((cp >>  6) & 0x3F));
                                  out += static_cast<char>(0x80 | ( cp        & 0x3F)); }
    }
    return out;
}

// ---------------------------------------------------------------------------
// RegExp compilation
// ---------------------------------------------------------------------------

static int parseFlags(const std::string& f) {
    int flags = 0;
    for (char c : f) {
        switch (c) {
            case 'g': flags |= LRE_FLAG_GLOBAL; break;
            case 'i': flags |= LRE_FLAG_IGNORECASE; break;
            case 'm': flags |= LRE_FLAG_MULTILINE; break;
            case 's': flags |= LRE_FLAG_DOTALL; break;
            case 'u': flags |= LRE_FLAG_UNICODE; break;
            case 'y': flags |= LRE_FLAG_STICKY; break;
            case 'd': flags |= LRE_FLAG_INDICES; break;
            case 'v': flags |= LRE_FLAG_UNICODE_SETS; break;
            default: break; 
        }
    }
    return flags;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// RegExp.prototype method implementations
// ---------------------------------------------------------------------------

const proto::ProtoObject* regexpExec(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self) return PROTO_NONE;

    const proto::ProtoString* bcKey = JSSymbols::reBytecode(ctx);
    const proto::ProtoObject* bcObj = self->getAttribute(ctx, bcKey, false);
    if (!bcObj || bcObj == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoByteBuffer* bcBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bcObj);
    const uint8_t* bc = reinterpret_cast<const uint8_t*>(bcBuffer->getBuffer(ctx));

    std::string input = "";
    if (args && args->getSize(ctx) > 0) {
        input = objToStr(ctx, args->getAt(ctx, 0));
    }

    auto u16 = utf8ToUTF16(input);
    int capture_count = lre_get_capture_count(bc);
    uint8_t** captures = new uint8_t*[capture_count * 2];

    const proto::ProtoString* lastIndexKey = JSSymbols::lastIndex(ctx);
    long long lastIndex = 0;
    const proto::ProtoObject* liObj = self->getAttribute(ctx, lastIndexKey, false);
    if (liObj && liObj->isInteger(ctx)) lastIndex = liObj->asLong(ctx);


    void* opaque = nullptr;
    if (JSContextWrapper::current()) opaque = JSContextWrapper::current()->getJSContext();

    int ret = lre_exec(captures, bc, reinterpret_cast<const uint8_t*>(u16.data()), 
                       static_cast<int>(lastIndex), static_cast<int>(u16.size()), 
                       1, opaque);


    if (ret == 1) {
        const proto::ProtoObject* result = createNewArray(ctx, nullptr);
        
        for (int i = 0; i < capture_count; i++) {
            uint8_t* start_ptr = captures[2 * i];
            uint8_t* end_ptr = captures[2 * i + 1];
            
            if (start_ptr && end_ptr) {
                size_t start = (start_ptr - reinterpret_cast<uint8_t*>(u16.data())) / 2;
                size_t end = (end_ptr - reinterpret_cast<uint8_t*>(u16.data())) / 2;
                std::string match = utf16ToUTF8(u16, start, end);
                const proto::ProtoString* k = JSSymbols::indexKey(ctx, i);
                result = result->setAttribute(ctx, k, ctx->fromUTF8String(match.c_str()));
            } else {
                const proto::ProtoString* k = JSSymbols::indexKey(ctx, i);
                result = result->setAttribute(ctx, k, PROTO_NONE);
            }
        }
        
        const size_t matchStart = (captures[0] - reinterpret_cast<uint8_t*>(u16.data())) / 2;
        result = result->setAttribute(ctx, JSSymbols::index(ctx),
                                      ctx->fromInteger(static_cast<long long>(matchStart)));
        result = result->setAttribute(ctx, JSSymbols::input(ctx),
                                      ctx->fromUTF8String(input.c_str()));
        result = result->setAttribute(ctx, JSSymbols::length(ctx),
                                      ctx->fromInteger(capture_count));

        if (lre_get_flags(bc) & LRE_FLAG_INDICES) {
            const proto::ProtoObject* indicesArr = createNewArray(ctx, nullptr);
            for (int i = 0; i < capture_count; i++) {
                uint8_t* sp = captures[2 * i];
                uint8_t* ep = captures[2 * i + 1];
                const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                if (sp && ep) {
                    size_t s = (sp - reinterpret_cast<uint8_t*>(u16.data())) / 2;
                    size_t e = (ep - reinterpret_cast<uint8_t*>(u16.data())) / 2;
                    const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
                    pair = pair->setAttribute(ctx, JSSymbols::indexKey(ctx, 0), ctx->fromInteger(static_cast<long long>(s)));
                    pair = pair->setAttribute(ctx, JSSymbols::indexKey(ctx, 1), ctx->fromInteger(static_cast<long long>(e)));
                    pair = pair->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(2));
                    indicesArr = indicesArr->setAttribute(ctx, ik, pair);
                } else {
                    indicesArr = indicesArr->setAttribute(ctx, ik, PROTO_NONE);
                }
            }
            indicesArr = indicesArr->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(capture_count));
            result = result->setAttribute(ctx, JSSymbols::indices(ctx), indicesArr);
        }

        if (lre_get_flags(bc) & (LRE_FLAG_GLOBAL | LRE_FLAG_STICKY)) {
            uint8_t* end = captures[1];
            long long newLastIndex = (end - reinterpret_cast<uint8_t*>(u16.data())) / 2;
            self->setAttribute(ctx, lastIndexKey, ctx->fromInteger(newLastIndex));
        }

        delete[] captures;
        return result;
    } else {
        if (lre_get_flags(bc) & (LRE_FLAG_GLOBAL | LRE_FLAG_STICKY)) {
            self->setAttribute(ctx, lastIndexKey, ctx->fromInteger(0));
        }
        delete[] captures;
        return PROTO_NONE;
    }
}

const proto::ProtoObject* regexpTest(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse)
{
    const proto::ProtoObject* res = regexpExec(ctx, self, parent, args, sparse);
    return (res && res != PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* regexpToString(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!ctx || !self) return ctx->fromUTF8String("/(?:)/");

    const proto::ProtoString* srcKey = JSSymbols::source(ctx);
    const proto::ProtoString* flgKey = JSSymbols::flags(ctx);
    
    std::string src = objToStr(ctx, self->getAttribute(ctx, srcKey, false));
    std::string flg = objToStr(ctx, self->getAttribute(ctx, flgKey, false));
    
    if (src.empty()) src = "(?:)";
    return ctx->fromUTF8String(("/" + src + "/" + flg).c_str());
}

// ---------------------------------------------------------------------------
// RegExp constructor
// ---------------------------------------------------------------------------

const proto::ProtoObject* regexpConstructor(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx) return PROTO_NONE;

    std::string pattern = "(?:)";
    std::string flags_str = "";

    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* p = args->getAt(ctx, 0);
        if (p && p != PROTO_NONE) {
            pattern = objToStr(ctx, p);
        }
    }

    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* f = args->getAt(ctx, 1);
        if (f && f != PROTO_NONE) {
            flags_str = objToStr(ctx, f);
        }
    }

    void* opaque = nullptr;
    if (JSContextWrapper::current()) opaque = JSContextWrapper::current()->getJSContext();

    int re_flags = parseFlags(flags_str);
    int bc_len;
    char error_msg[128];
    uint8_t* bc = lre_compile(&bc_len, error_msg, sizeof(error_msg), 
                             pattern.c_str(), pattern.size(), re_flags, opaque);

    if (!bc) {
        return PROTO_NONE;
    }

    const proto::ProtoObject* obj = (self && self != PROTO_NONE) ? self : ctx->newObject(true);
    const proto::ProtoString* bcKey = JSSymbols::reBytecode(ctx);
    const proto::ProtoString* srcKey = JSSymbols::source(ctx);
    const proto::ProtoString* flgKey = JSSymbols::flags(ctx);
    const proto::ProtoString* liKey = JSSymbols::lastIndex(ctx);

    // lre_compile returns a malloc-allocated buffer, but ProtoByteBuffer's
    // finalize uses delete[]; passing the malloc'd pointer directly with
    // freeOnExit=true mismatched the allocator AND the immediate free(bc)
    // below left the Cell pointing at freed memory.  Result: regexpExec
    // read garbage and returned false for every match.
    // Copy into a new[]-allocated buffer so the Cell's delete[] is sound,
    // then free the original malloc'd block.
    char* bcCopy = new char[bc_len];
    std::memcpy(bcCopy, bc, static_cast<size_t>(bc_len));
    free(bc);
    obj = obj->setAttribute(ctx, bcKey, ctx->fromBuffer(static_cast<unsigned long>(bc_len), bcCopy, true));
    // Per §22.2.3.2.5 EscapeRegExpPattern: the public `source` value is
    // the escaped pattern that round-trips through `/` + src + `/` lexer
    // parsing.  Pre-fix we stored the raw pattern, so `new RegExp('/').source`
    // returned "/" instead of "\\/" and the test262 value-* suite failed.
    std::string escSrc;
    {
        bool inCharClass = false;
        for (size_t i = 0; i < pattern.size(); i++) {
            char c = pattern[i];
            if (c == '\\' && i + 1 < pattern.size()) {
                escSrc.push_back(c);
                escSrc.push_back(pattern[++i]);
                continue;
            }
            if (c == '[') inCharClass = true;
            else if (c == ']') inCharClass = false;
            if (c == '/' && !inCharClass) { escSrc.append("\\/"); continue; }
            if (c == '\n') { escSrc.append("\\n"); continue; }
            if (c == '\r') { escSrc.append("\\r"); continue; }
            if (c == '\xE2' && i + 2 < pattern.size()
                && pattern[i+1] == '\x80'
                && (pattern[i+2] == '\xA8' || pattern[i+2] == '\xA9')) {
                escSrc.append(pattern[i+2] == '\xA8' ? "\\u2028" : "\\u2029");
                i += 2;
                continue;
            }
            escSrc.push_back(c);
        }
        if (escSrc.empty()) escSrc = "(?:)";
    }
    obj = obj->setAttribute(ctx, srcKey, ctx->fromUTF8String(escSrc.c_str()));
    obj = obj->setAttribute(ctx, flgKey, ctx->fromUTF8String(flags_str.c_str()));
    obj = obj->setAttribute(ctx, liKey, ctx->fromInteger(0));

    obj = obj->setAttribute(ctx, JSSymbols::global(ctx),     (re_flags & LRE_FLAG_GLOBAL)     ? PROTO_TRUE : PROTO_FALSE);
    obj = obj->setAttribute(ctx, JSSymbols::ignoreCase(ctx), (re_flags & LRE_FLAG_IGNORECASE)  ? PROTO_TRUE : PROTO_FALSE);
    obj = obj->setAttribute(ctx, JSSymbols::multiline(ctx),  (re_flags & LRE_FLAG_MULTILINE)   ? PROTO_TRUE : PROTO_FALSE);
    obj = obj->setAttribute(ctx, JSSymbols::dotAll(ctx),     (re_flags & LRE_FLAG_DOTALL)      ? PROTO_TRUE : PROTO_FALSE);
    obj = obj->setAttribute(ctx, JSSymbols::unicode(ctx),    (re_flags & LRE_FLAG_UNICODE)     ? PROTO_TRUE : PROTO_FALSE);
    obj = obj->setAttribute(ctx, JSSymbols::sticky(ctx),     (re_flags & LRE_FLAG_STICKY)      ? PROTO_TRUE : PROTO_FALSE);
    obj = obj->setAttribute(ctx, JSSymbols::hasIndices(ctx), (re_flags & LRE_FLAG_INDICES)     ? PROTO_TRUE : PROTO_FALSE);

    return obj;
}

// ---------------------------------------------------------------------------
// Well-known Symbol implementations (as methods)
// ---------------------------------------------------------------------------

const proto::ProtoObject* regexpSymbolMatch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse)
{
    if (!ctx || !self || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    
    const proto::ProtoString* bcKey = JSSymbols::reBytecode(ctx);
    const proto::ProtoObject* bcObj = self->getAttribute(ctx, bcKey, false);
    if (!bcObj || bcObj == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoByteBuffer* bcBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bcObj);
    int flags = lre_get_flags(reinterpret_cast<const uint8_t*>(bcBuffer->getBuffer(ctx)));

    if (!(flags & LRE_FLAG_GLOBAL)) {
        return regexpExec(ctx, self, parent, args, sparse);
    }

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    self->setAttribute(ctx, JSSymbols::lastIndex(ctx), ctx->fromInteger(0));
    
    int count = 0;
    while (true) {
        const proto::ProtoObject* match = regexpExec(ctx, self, parent, args, sparse);
        if (!match || match == PROTO_NONE) break;
        
        const proto::ProtoObject* firstMatch = match->getAttribute(ctx, JSSymbols::indexKey(ctx, 0), true);
        result = result->setAttribute(ctx, JSSymbols::indexKey(ctx, count++), firstMatch);

        const proto::ProtoObject* liObj = self->getAttribute(ctx, JSSymbols::lastIndex(ctx), false);
        if (liObj && liObj->isInteger(ctx) && liObj->asLong(ctx) == 0) {
             break; 
        }
    }
    
    if (count == 0) return PROTO_NONE;
    result = result->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(count));
    return result;
}

const proto::ProtoObject* regexpSymbolReplace(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || !args || args->getSize(ctx) < 2) return PROTO_NONE;

    std::string str = objToStr(ctx, args->getAt(ctx, 0));
    const proto::ProtoObject* replaceValue = args->getAt(ctx, 1);

    const proto::ProtoObject* bcObj = self->getAttribute(ctx, JSSymbols::reBytecode(ctx), false);
    if (!bcObj || bcObj == PROTO_NONE) return ctx->fromUTF8String(str.c_str());

    const proto::ProtoByteBuffer* bcBuf = reinterpret_cast<const proto::ProtoByteBuffer*>(bcObj);
    const uint8_t* bc = reinterpret_cast<const uint8_t*>(bcBuf->getBuffer(ctx));
    int flags = lre_get_flags(bc);
    bool isGlobal = (flags & LRE_FLAG_GLOBAL) != 0;

    if (isGlobal) {
        self->setAttribute(ctx, JSSymbols::lastIndex(ctx), ctx->fromInteger(0));
    }

    auto u16 = utf8ToUTF16(str);

    // Callable replacement: identified by a non-PROTO_NONE bytecodeId
    // sidecar (user closures) OR a native method marker.  Spec
    // §22.2.6.11 step 6 demands ToString(replaceValue) ONLY when not
    // callable — so the undefined sentinel must coerce to "undefined",
    // not the function-handler short-circuit.
    bool isFnReplace = false;
    if (replaceValue && replaceValue != PROTO_NONE
        && replaceValue != getUndefinedSentinel() && replaceValue != getNullSentinel()) {
        const proto::ProtoObject* bcVal =
            replaceValue->getAttribute(ctx, JSSymbols::bytecodeId(ctx), false);
        if (bcVal && bcVal != PROTO_NONE) isFnReplace = true;
        if (!isFnReplace && replaceValue->isMethod(ctx)) isFnReplace = true;
        if (!isFnReplace) {
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
            if (nfKey && replaceValue->hasAttribute(ctx, nfKey) == PROTO_TRUE)
                isFnReplace = true;
        }
    }
    std::string replStr = isFnReplace ? "" : objToStr(ctx, replaceValue);

    std::string result;
    size_t lastMatchEnd = 0;

    const proto::ProtoObject* strArg = ctx->fromUTF8String(str.c_str());
    const proto::ProtoList* execArgs = ctx->newList();
    execArgs = execArgs->appendLast(ctx, strArg);

    while (true) {
        const proto::ProtoObject* match = regexpExec(ctx, self, nullptr, execArgs, nullptr);
        if (!match || match == PROTO_NONE) break;

        const proto::ProtoObject* fullMatchObj = match->getAttribute(ctx, JSSymbols::indexKey(ctx, 0), false);
        const proto::ProtoObject* matchIdxObj  = match->getAttribute(ctx, JSSymbols::index(ctx), false);
        long long matchStart = (matchIdxObj && matchIdxObj->isInteger(ctx)) ? matchIdxObj->asLong(ctx) : 0;
        std::string fullMatch = (fullMatchObj && fullMatchObj != PROTO_NONE) ? objToStr(ctx, fullMatchObj) : "";
        auto u16Match = utf8ToUTF16(fullMatch);
        size_t matchEnd = static_cast<size_t>(matchStart) + u16Match.size();

        result += utf16ToUTF8(u16, lastMatchEnd, static_cast<size_t>(matchStart));

        std::string replacement;
        if (!isFnReplace) {
            replacement = replStr;
            size_t pos = 0;
            while (pos < replacement.size()) {
                if (replacement[pos] == '$' && pos + 1 < replacement.size()) {
                    char next = replacement[pos + 1];
                    if (next == '&') {
                        replacement.replace(pos, 2, fullMatch);
                        pos += fullMatch.size();
                    } else if (next == '`') {
                        std::string pre = utf16ToUTF8(u16, 0, static_cast<size_t>(matchStart));
                        replacement.replace(pos, 2, pre);
                        pos += pre.size();
                    } else if (next == '\'') {
                        std::string suf = utf16ToUTF8(u16, matchEnd);
                        replacement.replace(pos, 2, suf);
                        pos += suf.size();
                    } else if (next == '$') {
                        replacement.replace(pos, 2, "$");
                        pos += 1;
                    } else if (next >= '1' && next <= '9') {
                        int capIdx = next - '0';
                        const proto::ProtoObject* cap =
                            match->getAttribute(ctx, JSSymbols::indexKey(ctx, static_cast<uint32_t>(capIdx)), false);
                        std::string capStr = (cap && cap != PROTO_NONE) ? objToStr(ctx, cap) : "";
                        replacement.replace(pos, 2, capStr);
                        pos += capStr.size();
                    } else {
                        pos++;
                    }
                } else {
                    pos++;
                }
            }
        } else {
            // §22.2.6.11 step 14.k: build (match, ...captures, position,
            // string) and call the replacer.  Pre-fix the function branch
            // dropped its return value and used `fullMatch` as the
            // replacement, so every regex /.../ + function callback was a
            // no-op (Sputnik S15.5.4.11_A4_T1..4).
            const proto::ProtoList* callArgs = ctx->newList();
            callArgs = callArgs->appendLast(ctx, ctx->fromUTF8String(fullMatch.c_str()));
            // Walk numbered captures sequentially via indexKey(1..N).
            // Stop on the first absent slot — the match exposes captures
            // as own integer-indexed attrs starting at 1, with the
            // total count not directly stored, so probing-until-absent
            // is the cheapest portable shape.
            const proto::ProtoString* lenKey = JSSymbols::length(ctx);
            long long captureCount = 0;
            if (lenKey) {
                const proto::ProtoObject* lenVal = match->getAttribute(ctx, lenKey, false);
                if (lenVal && lenVal->isInteger(ctx)) captureCount = lenVal->asLong(ctx);
            }
            for (long long ci = 1; ci < captureCount; ci++) {
                const proto::ProtoObject* cap =
                    match->getAttribute(ctx, JSSymbols::indexKey(ctx, static_cast<uint32_t>(ci)), false);
                if (!cap || cap == PROTO_NONE) {
                    callArgs = callArgs->appendLast(ctx, getUndefinedSentinel());
                } else {
                    callArgs = callArgs->appendLast(ctx, cap);
                }
            }
            callArgs = callArgs->appendLast(ctx, ctx->fromInteger(matchStart));
            callArgs = callArgs->appendLast(ctx, strArg);
            const proto::ProtoObject* fnRes = callJSFunction(ctx, replaceValue, PROTO_NONE, callArgs);
            if (hasCallException()) return PROTO_NONE;
            replacement = objToStr(ctx, fnRes);
        }

        result += replacement;
        lastMatchEnd = matchEnd;

        if (!isGlobal) break;

        const proto::ProtoObject* liObj = self->getAttribute(ctx, JSSymbols::lastIndex(ctx), false);
        if (liObj && liObj->isInteger(ctx) &&
            static_cast<size_t>(liObj->asLong(ctx)) == lastMatchEnd) {
            self->setAttribute(ctx, JSSymbols::lastIndex(ctx),
                               ctx->fromInteger(static_cast<long long>(lastMatchEnd + 1)));
        }
    }

    result += utf16ToUTF8(u16, lastMatchEnd);
    return ctx->fromUTF8String(result.c_str());
}

const proto::ProtoObject* regexpSymbolSearch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse)
{
    const proto::ProtoObject* match = regexpExec(ctx, self, parent, args, sparse);
    if (!match || match == PROTO_NONE) return ctx->fromInteger(-1LL);
    
    return match->getAttribute(ctx, JSSymbols::index(ctx), true);
}

const proto::ProtoObject* regexpSymbolSplit(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || !args || args->getSize(ctx) < 1) {
        const proto::ProtoObject* r = createNewArray(ctx, nullptr);
        r = r->setAttribute(ctx, JSSymbols::indexKey(ctx, 0), ctx->fromUTF8String(""));
        r = r->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(1));
        return r;
    }

    std::string str = objToStr(ctx, args->getAt(ctx, 0));
    long long limit = -1;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* limitObj = args->getAt(ctx, 1);
        if (limitObj && limitObj != PROTO_NONE && limitObj->isInteger(ctx)) {
            limit = limitObj->asLong(ctx);
            if (limit < 0) limit = 0;
        }
    }
    if (limit == 0) {
        return createNewArray(ctx, nullptr);
    }

    const proto::ProtoObject* bcObj = self->getAttribute(ctx, JSSymbols::reBytecode(ctx), false);
    if (!bcObj || bcObj == PROTO_NONE) {
        const proto::ProtoObject* r = createNewArray(ctx, nullptr);
        r = r->setAttribute(ctx, JSSymbols::indexKey(ctx, 0), ctx->fromUTF8String(str.c_str()));
        r = r->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(1));
        return r;
    }

    const proto::ProtoByteBuffer* bcBuf = reinterpret_cast<const proto::ProtoByteBuffer*>(bcObj);
    const uint8_t* bc = reinterpret_cast<const uint8_t*>(bcBuf->getBuffer(ctx));

    std::string flagsStr = objToStr(ctx, self->getAttribute(ctx, JSSymbols::flags(ctx), false));
    if (flagsStr.find('y') == std::string::npos) flagsStr += 'y';

    std::string patternStr = objToStr(ctx, self->getAttribute(ctx, JSSymbols::source(ctx), false));
    void* opaque = nullptr;
    if (JSContextWrapper::current()) opaque = JSContextWrapper::current()->getJSContext();
    int stickyFlags = parseFlags(flagsStr);
    int bc_len;
    char errmsg[128];
    uint8_t* stickyBc = lre_compile(&bc_len, errmsg, sizeof(errmsg),
                                     patternStr.c_str(), patternStr.size(), stickyFlags, opaque);
    if (!stickyBc) {
        const proto::ProtoObject* r = createNewArray(ctx, nullptr);
        r = r->setAttribute(ctx, JSSymbols::indexKey(ctx, 0), ctx->fromUTF8String(str.c_str()));
        r = r->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(1));
        return r;
    }

    auto u16 = utf8ToUTF16(str);
    const size_t strLen = u16.size();
    int captureCount = lre_get_capture_count(stickyBc);
    uint8_t** captures = new uint8_t*[captureCount * 2];

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    long long resultLen = 0;
    size_t lastEnd = 0;

    for (size_t pos = 0; pos <= strLen; ) {
        int ret = lre_exec(captures, stickyBc,
                           reinterpret_cast<const uint8_t*>(u16.data()),
                           static_cast<int>(pos), static_cast<int>(strLen), 1, opaque);

        if (ret != 1) {
            pos++;
            continue;
        }

        size_t matchStart = (captures[0] - reinterpret_cast<uint8_t*>(u16.data())) / 2;
        size_t matchEnd   = (captures[1] - reinterpret_cast<uint8_t*>(u16.data())) / 2;

        if (matchEnd == lastEnd && matchStart == lastEnd) {
            pos++;
            continue;
        }

        std::string piece = utf16ToUTF8(u16, lastEnd, matchStart);
        result = result->setAttribute(ctx, JSSymbols::indexKey(ctx, static_cast<uint32_t>(resultLen++)),
                                      ctx->fromUTF8String(piece.c_str()));
        if (limit != -1 && resultLen >= limit) goto done;

        for (int i = 1; i < captureCount; i++) {
            uint8_t* cs = captures[2 * i];
            uint8_t* ce = captures[2 * i + 1];
            const proto::ProtoObject* capVal = PROTO_NONE;
            if (cs && ce) {
                size_t cs16 = (cs - reinterpret_cast<uint8_t*>(u16.data())) / 2;
                size_t ce16 = (ce - reinterpret_cast<uint8_t*>(u16.data())) / 2;
                std::string capStr = utf16ToUTF8(u16, cs16, ce16);
                capVal = ctx->fromUTF8String(capStr.c_str());
            }
            result = result->setAttribute(ctx, JSSymbols::indexKey(ctx, static_cast<uint32_t>(resultLen++)),
                                          capVal);
            if (limit != -1 && resultLen >= limit) goto done;
        }

        lastEnd = matchEnd;
        pos = matchEnd;
        if (matchEnd == matchStart) pos++;
    }

done:
    if (limit == -1 || resultLen < limit) {
        std::string tail = utf16ToUTF8(u16, lastEnd);
        result = result->setAttribute(ctx, JSSymbols::indexKey(ctx, static_cast<uint32_t>(resultLen++)),
                                      ctx->fromUTF8String(tail.c_str()));
    }

    result = result->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(resultLen));
    free(stickyBc);
    delete[] captures;
    return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Helper: ToBoolean on a getAttribute result.  null/undefined/PROTO_NONE,
// the boolean false, 0/NaN integers/doubles, and the empty string yield false.
static bool reFlagToBoolean(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel() || v == getNullSentinel())
        return false;
    if (v == PROTO_FALSE) return false;
    if (v == PROTO_TRUE)  return true;
    if (v->isBoolean(ctx)) return v->asBoolean(ctx);
    if (v->isInteger(ctx)) return v->asLong(ctx) != 0;
    if (v->isDouble(ctx))  { double d = v->asDouble(ctx); return d != 0.0 && d == d; }
    if (v->isString(ctx))  return v->asString(ctx) && v->asString(ctx)->getSize(ctx) > 0;
    return true;
}

// Cached pointer to RegExp.prototype itself so the §22.2.6.x step 3.a
// carve-out can recognise the prototype receiver.  Set once during
// ensureRegExpConstructor and immutable thereafter.
static thread_local const proto::ProtoObject* t_regexpPrototype = nullptr;

// Reject primitives + null/undefined per §22.2.6.x step 1-2 (RequireObject).
static bool reRequireObjectThis(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()
        || self->isString(ctx) || self->isInteger(ctx) || self->isDouble(ctx)
        || self->isFloat(ctx) || self->isBoolean(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "RegExp flag getter called on non-Object"));
        return false;
    }
    const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
    if (symK && self->getAttribute(ctx, symK, true) == PROTO_TRUE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "RegExp flag getter called on Symbol"));
        return false;
    }
    const proto::ProtoString* bigK = JSSymbols::isBigInt(ctx);
    if (bigK && self->getAttribute(ctx, bigK, true) == PROTO_TRUE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "RegExp flag getter called on BigInt"));
        return false;
    }
    return true;
}

// §22.2.6.x step 3: if R lacks [[OriginalFlags]], the getter returns
// undefined when R is RegExp.prototype and throws TypeError otherwise.
// We detect [[OriginalFlags]] via the __re_bytecode__ data attribute
// installed only on real RegExp instances.
static bool reHasInternalSlot(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoString* bcK = JSSymbols::reBytecode(ctx);
    if (!bcK) return false;
    return self->hasAttribute(ctx, bcK) == PROTO_TRUE;
}

// Generate a Boolean-coercing getter for a single flag attribute key.
#define DEFINE_FLAG_GETTER(getterName, jsAttr)                                          \
static const proto::ProtoObject* getterName(                                            \
    proto::ProtoContext* ctx, const proto::ProtoObject* self,                           \
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)   \
{                                                                                       \
    if (!reRequireObjectThis(ctx, self)) return PROTO_NONE;                             \
    if (!reHasInternalSlot(ctx, self)) {                                                \
        if (self == t_regexpPrototype) return getUndefinedSentinel();                   \
        signalNativeException(makeNativeError(ctx, "TypeError",                         \
            "RegExp flag getter called on non-RegExp"));                                \
        return PROTO_NONE;                                                              \
    }                                                                                   \
    const proto::ProtoObject* v = self->getAttribute(ctx, jsAttr(ctx), true);           \
    return reFlagToBoolean(ctx, v) ? PROTO_TRUE : PROTO_FALSE;                          \
}
DEFINE_FLAG_GETTER(reGlobalGetter,     JSSymbols::global)
DEFINE_FLAG_GETTER(reIgnoreCaseGetter, JSSymbols::ignoreCase)
DEFINE_FLAG_GETTER(reMultilineGetter,  JSSymbols::multiline)
DEFINE_FLAG_GETTER(reDotAllGetter,     JSSymbols::dotAll)
DEFINE_FLAG_GETTER(reStickyGetter,     JSSymbols::sticky)
DEFINE_FLAG_GETTER(reUnicodeGetter,    JSSymbols::unicode)
DEFINE_FLAG_GETTER(reHasIndicesGetter, JSSymbols::hasIndices)
#undef DEFINE_FLAG_GETTER

// §22.2.6.5 RegExp.prototype.flags — assemble the canonical character
// string from each individual ToBoolean(get(R, name)) probe so user
// overrides on subclasses are honoured per spec.
static const proto::ProtoObject* reFlagsGetter(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!reRequireObjectThis(ctx, self)) return PROTO_NONE;
    std::string result;
    auto probe = [&](const proto::ProtoString* key, char letter) {
        if (!key) return;
        const proto::ProtoObject* v = self->getAttribute(ctx, key, true);
        if (reFlagToBoolean(ctx, v)) result.push_back(letter);
    };
    probe(JSSymbols::hasIndices(ctx), 'd');
    probe(JSSymbols::global(ctx),     'g');
    probe(JSSymbols::ignoreCase(ctx), 'i');
    probe(JSSymbols::multiline(ctx),  'm');
    probe(JSSymbols::dotAll(ctx),     's');
    probe(JSSymbols::unicode(ctx),    'u');
    // unicodeSets ("v") falls through silently; not all builds expose it.
    probe(JSSymbols::sticky(ctx),     'y');
    return ctx->fromUTF8String(result.c_str());
}

// §22.2.6.10 RegExp.prototype.source getter — returns the pattern text
// stored on construction (data attribute "source").  The prototype's
// receiver yields the literal "(?:)"; non-RegExp receivers throw.
static const proto::ProtoObject* reSourceGetter(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!reRequireObjectThis(ctx, self)) return PROTO_NONE;
    if (!reHasInternalSlot(ctx, self)) {
        if (self == t_regexpPrototype) return ctx->fromUTF8String("(?:)");
        signalNativeException(makeNativeError(ctx, "TypeError",
            "RegExp.prototype.source getter called on non-RegExp"));
        return PROTO_NONE;
    }
    const proto::ProtoString* srcK = JSSymbols::source(ctx);
    if (!srcK) return ctx->fromUTF8String("(?:)");
    const proto::ProtoObject* v = self->getAttribute(ctx, srcK, true);
    if (!v || v == PROTO_NONE) return ctx->fromUTF8String("(?:)");
    return v;
}

// Install an accessor getter on RegExp.prototype.  Mirrors the
// Symbol.species install pattern in this file.
static const proto::ProtoObject* installRegExpGetter(
    proto::ProtoContext* ctx, const proto::ProtoObject* sp,
    const char* nameStr, proto::ProtoMethod fn)
{
    const proto::ProtoObject* parent =
        (ctx->space && ctx->space->methodPrototype)
        ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* getter = parent
        ? parent->newChild(ctx, true) : ctx->newObject(true);
    if (!getter) return sp;
    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, fn);
    if (raw) getter = getter->setAttribute(ctx, JSSymbols::nativeFn(ctx), raw);
    getter = getter->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(0LL));
    const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
    const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
    if (pdlk) getter = getter->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    std::string nm = std::string("get ") + nameStr;
    getter = getter->setAttribute(ctx, JSSymbols::name(ctx), ctx->fromUTF8String(nm.c_str()));
    const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
    const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
    if (pdnk) getter = getter->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    getter = getter->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);

    // Install __get_<name>__ on the prototype + descriptor 0x2.  The
    // data slot is intentionally left PROTO_NONE so accessor probes
    // win on getOwnPropertyDescriptor(RegExp.prototype, name).
    std::string gkStr = "__get_" + std::string(nameStr) + "__";
    const proto::ProtoString* gk =
        ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
    if (gk) sp = sp->setAttribute(ctx, gk, getter);
    const proto::ProtoString* nk = ctx->fromUTF8String(nameStr)->asString(ctx);
    if (nk) sp = sp->setAttribute(ctx, nk, PROTO_NONE);
    std::string pdStr = "__pd_" + std::string(nameStr) + "__";
    const proto::ProtoString* pdk =
        ctx->fromUTF8String(pdStr.c_str())->asString(ctx);
    if (pdk) sp = sp->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    return sp;
}

const proto::ProtoObject* BuildRegExpPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                                                const proto::ProtoObject* regexpProto) {
    if (!space || !ctx || !regexpProto) return regexpProto;

    const proto::ProtoObject* sp = regexpProto;

    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* mObj = ctx->fromMethod(nullptr, fn);
            if (mObj && mObj != PROTO_NONE) {
                mObj = mObj->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(length));
                mObj = mObj->setAttribute(ctx, JSSymbols::name(ctx),   ctx->fromUTF8String(name));
            }
            sp = sp->setAttribute(ctx, key, mObj);
        }
    };

    reg("exec",     regexpExec,     1);
    reg("test",     regexpTest,     1);
    reg("toString", regexpToString, 0);
    reg("Symbol.match",   regexpSymbolMatch,   1);
    reg("Symbol.replace", regexpSymbolReplace, 2);
    reg("Symbol.search",  regexpSymbolSearch,  1);
    reg("Symbol.split",    regexpSymbolSplit,    2);
    reg("Symbol.matchAll", regexpSymbolMatchAll, 1);

    // §22.2.6.{1,5,6,7,8,9,10,11,12} accessor getters on RegExp.prototype.
    // Pre-fix the flag/source slots lived only as data attributes on
    // instances, so getOwnPropertyDescriptor(RegExp.prototype, "flags")
    // surfaced undefined and the test262 property-shape suite failed.
    sp = installRegExpGetter(ctx, sp, "flags",      reFlagsGetter);
    sp = installRegExpGetter(ctx, sp, "global",     reGlobalGetter);
    sp = installRegExpGetter(ctx, sp, "ignoreCase", reIgnoreCaseGetter);
    sp = installRegExpGetter(ctx, sp, "multiline",  reMultilineGetter);
    sp = installRegExpGetter(ctx, sp, "dotAll",     reDotAllGetter);
    sp = installRegExpGetter(ctx, sp, "sticky",     reStickyGetter);
    sp = installRegExpGetter(ctx, sp, "unicode",    reUnicodeGetter);
    sp = installRegExpGetter(ctx, sp, "hasIndices", reHasIndicesGetter);
    sp = installRegExpGetter(ctx, sp, "source",     reSourceGetter);

    // Set Symbol.toStringTag so Object.prototype.toString.call(new RegExp())
    // === "[object RegExp]". Install under both internal and user-visible
    // keys (see the Map / Set / Promise / Math / JSON fixes).
    {
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey)
            sp = sp->setAttribute(ctx, tagKey, ctx->fromUTF8String("RegExp"));
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            sp = sp->setAttribute(ctx, userKey, ctx->fromUTF8String("RegExp"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) sp = sp->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }

    return sp;
}

void ensureRegExpConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyRegExp = JSSymbols::RegExp(ctx);
    if (!keyRegExp) return;

    // Use objectPrototype as parent for the new RegExp.prototype
    const proto::ProtoObject* objectProto = ctx->space->objectPrototype;
    // Mutable so RegExp.prototype.x = y mutates in place; the
    // pre-fix newChild(ctx, false) forked the identity on every
    // builtin-install setAttribute and silently swallowed user
    // writes (same shape as the R28 Object.prototype bug).
    const proto::ProtoObject* regexpProto = objectProto
        ? objectProto->newChild(ctx, true)
        : ctx->newObject(true);

    regexpProto = BuildRegExpPrototype(ctx->space, ctx, regexpProto);
    t_regexpPrototype = regexpProto;

    const proto::ProtoObject* ctor = ctx->newObject(true);
    if (!ctor) return;

    ctor = ctor->setAttribute(ctx, JSSymbols::regexpCtor(ctx), PROTO_TRUE);
    // §22.2.3.1: RegExp has [[Construct]] — stamp the generic marker so
    // Reflect.construct / test262's isConstructor harness recognise it.
    {
        const proto::ProtoString* icK = JSSymbols::isConstructor(ctx);
        if (icK) ctor = ctor->setAttribute(ctx, icK, PROTO_TRUE);
    }
    ctor = ctor->setAttribute(ctx, JSSymbols::name(ctx),       ctx->fromUTF8String("RegExp"));
    // §17 + §22.2.3: name descriptor 0x2 (!writable, !enumerable,
    // configurable).  Pre-fix the name slot defaulted to fully
    // enumerable, failing the prop-desc check.
    {
        const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
        if (pdns) ctor = ctor->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
    }
    // §22.2.3.1: RegExp.length === 2 with §17 descriptor 0x2.  Pre-fix
    // the slot was absent so verifyProperty(RegExp, "length", ...)
    // failed with "obj should have an own property length".
    {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(2LL));
            const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
            if (pdls) ctor = ctor->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
        }
    }
    // Hot-path hint: name + length descriptors above both encode
    // writable=false.  Mirror the wrapNativeFunction / addMethod /
    // Boolean-ctor pattern from the same round so resolvePutFieldOOP
    // actually consults the __pd_<key>__ sidecars on writes.  Without
    // the flag, `RegExp.name = "X"` silently succeeded.
    {
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) ctor = ctor->setAttribute(ctx, hnw, PROTO_TRUE);
    }

    // Set .prototype on constructor
    ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), regexpProto);

    // Set .constructor on prototype
    regexpProto = regexpProto->setAttribute(ctx, JSSymbols::constructor(ctx), ctor);
    // Re-set .prototype on constructor because regexpProto was updated!
    ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), regexpProto);
    // §22.2.2.1 / §17: RegExp.prototype descriptor bits 0x0.
    {
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }

    // §22.2.4.2 get RegExp[@@species]: a getter returning `this`,
    // with descriptor {enumerable:false, configurable:true} → 0x2.
    // The getter function itself has length 0 and name
    // "get [Symbol.species]" ({!writable, !enumerable, configurable}
    // → 0x2).  Pre-fix RegExp had no species accessor, so the
    // test262 propertyHelper-based built-ins/RegExp/Symbol.species/*
    // suite reported the slot as missing.  Mirrors the Array / Set /
    // Map / Promise install pattern.
    {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* getter = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (getter) {
                static const auto regexpSpeciesGetter = [](
                    proto::ProtoContext* /*ctx*/, const proto::ProtoObject* self,
                    const proto::ParentLink*,
                    const proto::ProtoList*, const proto::ProtoSparseList*)
                    -> const proto::ProtoObject* { return self; };
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, regexpSpeciesGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get [Symbol.species]"));
                    const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* hnwSp = JSSymbols::hasNonWritableProps(ctx);
                if (hnwSp) getter = getter->setAttribute(ctx, hnwSp, PROTO_TRUE);
                const proto::ProtoString* gksSym =
                    ctx->fromUTF8String("__get_Symbol.species__")->asString(ctx);
                if (gksSym) ctor = ctor->setAttribute(ctx, gksSym, getter);
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.species__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            }
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyRegExp, ctor);
    // §17: every built-in constructor on globalThis is
    // { writable: true, enumerable: false, configurable: true }.
    // Pre-fix the bare setAttribute fell through to default enumerable=true,
    // and Object.getOwnPropertyDescriptor(globalThis, "RegExp").enumerable
    // surfaced as true (built-ins/RegExp/prop-desc.js).
    const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_RegExp__");
    const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
    if (pdks) *globalRoot = (*globalRoot)->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
}

} // namespace protojs
