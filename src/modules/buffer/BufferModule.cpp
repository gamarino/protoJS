#include "BufferModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace protojs {

namespace {

const proto::ProtoString* keyByteBuffer(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__byte_buffer__");
}
const proto::ProtoString* keyIsBufferMark(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__is_buffer__");
}

// ---- Argument helpers --------------------------------------------------

bool argInt(proto::ProtoContext* ctx, const proto::ProtoList* args,
             int idx, long long& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isInteger(ctx)) return false;
    out = a->asLong(ctx);
    return true;
}

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

// ---- Encoding helpers --------------------------------------------------

std::vector<uint8_t> decodeString(const std::string& str,
                                    const std::string& encoding) {
    std::vector<uint8_t> out;
    if (encoding == "utf8" || encoding == "utf-8" ||
        encoding == "ascii" || encoding == "latin1") {
        out.assign(str.begin(), str.end());
    } else if (encoding == "hex") {
        for (size_t i = 0; i + 1 < str.size(); i += 2) {
            char hex[3] = {str[i], str[i + 1], '\0'};
            out.push_back(static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)));
        }
    } else if (encoding == "base64") {
        static const char b64[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < str.size(); i += 4) {
            uint32_t v = 0;
            int n = 0;
            for (int j = 0; j < 4 && i + j < str.size(); ++j) {
                if (str[i + j] == '=') break;
                const char* p = std::strchr(b64, str[i + j]);
                if (!p) continue;
                v = (v << 6) | static_cast<uint32_t>(p - b64);
                ++n;
            }
            v <<= (4 - n) * 6;
            if (n >= 2) out.push_back((v >> 16) & 0xFF);
            if (n >= 3) out.push_back((v >> 8)  & 0xFF);
            if (n >= 4) out.push_back(v         & 0xFF);
        }
    } else {
        out.assign(str.begin(), str.end());
    }
    return out;
}

std::string encodeBytes(const char* data, size_t len,
                         const std::string& encoding) {
    if (encoding == "utf8" || encoding == "utf-8" ||
        encoding == "latin1" || encoding == "ascii") {
        std::string s(data, len);
        if (encoding == "ascii") {
            for (auto& c : s) c = static_cast<char>(c & 0x7F);
        }
        return s;
    }
    if (encoding == "hex") {
        std::ostringstream os;
        for (size_t i = 0; i < len; ++i) {
            os << std::hex << std::setw(2) << std::setfill('0')
               << (static_cast<unsigned>(data[i]) & 0xFF);
        }
        return os.str();
    }
    if (encoding == "base64") {
        static const char b64[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < len; i += 3) {
            uint32_t v = static_cast<uint8_t>(data[i]) << 16;
            if (i + 1 < len) v |= static_cast<uint8_t>(data[i + 1]) << 8;
            if (i + 2 < len) v |= static_cast<uint8_t>(data[i + 2]);
            out += b64[(v >> 18) & 63];
            out += b64[(v >> 12) & 63];
            out += (i + 1 < len) ? b64[(v >> 6) & 63] : '=';
            out += (i + 2 < len) ? b64[v & 63]        : '=';
        }
        return out;
    }
    return std::string(data, len);
}

// ---- ProtoByteBuffer access through the instance -----------------------

const proto::ProtoByteBuffer* getByteBuffer(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoObject* attr =
        self->getAttribute(ctx, keyByteBuffer(ctx), false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    return attr->asByteBuffer(ctx);
}

bool isBufferInstance(proto::ProtoContext* ctx,
                       const proto::ProtoObject* val) {
    if (!ctx || !val || val == PROTO_NONE) return false;
    const proto::ProtoObject* mark =
        val->getAttribute(ctx, keyIsBufferMark(ctx), false);
    return mark == PROTO_TRUE;
}

const proto::ProtoObject* getBufferProto(proto::ProtoContext* ctx);

const proto::ProtoObject* makeBufferInstance(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* byteBufferObj,
        long long size) {
    const proto::ProtoObject* proto = getBufferProto(ctx);
    const proto::ProtoObject* inst = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    if (!inst) return PROTO_NONE;
    inst->setAttribute(ctx, keyByteBuffer(ctx), byteBufferObj);
    inst->setAttribute(ctx, keyIsBufferMark(ctx), PROTO_TRUE);
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) inst->setAttribute(ctx, lk, ctx->fromInteger(size));
    const proto::ProtoString* blk =
        ctx->fromUTF8String("byteLength")->asString(ctx);
    if (blk) inst->setAttribute(ctx, blk, ctx->fromInteger(size));
    return inst;
}

// Allocate a fresh ProtoByteBuffer and copy `bytes` into it.
const proto::ProtoObject* makeFromBytes(proto::ProtoContext* ctx,
                                          const std::vector<uint8_t>& bytes) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* bufObj = ctx->newBuffer(bytes.size());
    if (!bufObj) return PROTO_NONE;
    const proto::ProtoByteBuffer* bb = bufObj->asByteBuffer(ctx);
    if (bb && !bytes.empty()) {
        char* dst = bb->getBuffer(ctx);
        if (dst) std::memcpy(dst, bytes.data(), bytes.size());
    }
    return makeBufferInstance(ctx, bufObj,
                                static_cast<long long>(bytes.size()));
}

// ---- Buffer constructor (handles size | string | array | Buffer) ------

const proto::ProtoObject* extractBytes(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* val,
                                         const std::string& encoding,
                                         std::vector<uint8_t>& out);

const proto::ProtoObject* bufferConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (!a0) return PROTO_NONE;

    if (a0->isInteger(ctx)) {
        long long size = a0->asLong(ctx);
        if (size < 0) return PROTO_NONE;
        const proto::ProtoObject* bufObj = ctx->newBuffer(size);
        if (!bufObj) return PROTO_NONE;
        // Zero-fill to match Buffer.alloc behavior (the original QuickJS
        // version left the buffer uninitialised; this is the safer
        // default and matches Node's `new Buffer(size)` behaviour).
        const proto::ProtoByteBuffer* bb = bufObj->asByteBuffer(ctx);
        if (bb && size > 0) std::memset(bb->getBuffer(ctx), 0, size);
        return makeBufferInstance(ctx, bufObj, size);
    }

    std::string encoding = "utf8";
    if (args->getSize(ctx) > 1) argString(ctx, args, 1, encoding);

    std::vector<uint8_t> bytes;
    if (extractBytes(ctx, a0, encoding, bytes)) {
        return makeFromBytes(ctx, bytes);
    }
    return PROTO_NONE;
}

// Coerce a value into a byte vector.  Accepts: existing Buffer, string,
// or Array of integers.  Returns the input value (a non-null marker)
// if a coercion succeeded; nullptr otherwise.  `out` holds the bytes.
const proto::ProtoObject* extractBytes(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* val,
                                         const std::string& encoding,
                                         std::vector<uint8_t>& out) {
    if (!ctx || !val || val == PROTO_NONE) return nullptr;
    if (val->isString(ctx)) {
        std::string s;
        val->asString(ctx)->toUTF8String(ctx, s);
        out = decodeString(s, encoding);
        return val;
    }
    if (isBufferInstance(ctx, val)) {
        const proto::ProtoByteBuffer* bb = getByteBuffer(ctx, val);
        if (!bb) return nullptr;
        unsigned long n = bb->getSize(ctx);
        const char* src = bb->getBuffer(ctx);
        out.assign(src, src + n);
        return val;
    }
    // Array of integers.
    const proto::ProtoList* els = getArrayElements(ctx, val);
    if (els) {
        long long n = static_cast<long long>(els->getSize(ctx));
        out.reserve(static_cast<size_t>(n));
        for (long long i = 0; i < n; ++i) {
            const proto::ProtoObject* e =
                els->getAt(ctx, static_cast<int>(i));
            if (e && e->isInteger(ctx)) {
                out.push_back(static_cast<uint8_t>(e->asLong(ctx) & 0xFF));
            } else {
                out.push_back(0);
            }
        }
        return val;
    }
    return nullptr;
}

// ---- Static factories: from / alloc / concat / isBuffer ----------------

const proto::ProtoObject* bufferFromImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return bufferConstructor(ctx, self, pl, args, kw);
}

const proto::ProtoObject* bufferAllocImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    long long size = 0;
    if (!argInt(ctx, args, 0, size) || size < 0) return PROTO_NONE;
    const proto::ProtoObject* bufObj = ctx->newBuffer(size);
    if (!bufObj) return PROTO_NONE;
    const proto::ProtoByteBuffer* bb = bufObj->asByteBuffer(ctx);
    if (bb && size > 0) {
        char fill = 0;
        if (args && args->getSize(ctx) > 1) {
            const proto::ProtoObject* a1 = args->getAt(ctx, 1);
            if (a1 && a1->isInteger(ctx)) {
                fill = static_cast<char>(a1->asLong(ctx) & 0xFF);
            } else if (a1 && a1->isString(ctx)) {
                std::string s;
                a1->asString(ctx)->toUTF8String(ctx, s);
                if (!s.empty()) fill = s[0];
            }
        }
        std::memset(bb->getBuffer(ctx), fill, size);
    }
    return makeBufferInstance(ctx, bufObj, size);
}

const proto::ProtoObject* bufferConcatImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* arr = args->getAt(ctx, 0);
    const proto::ProtoList* els = arr ? getArrayElements(ctx, arr) : nullptr;
    if (!els) return PROTO_NONE;
    long long n = static_cast<long long>(els->getSize(ctx));
    size_t total = 0;
    std::vector<const proto::ProtoByteBuffer*> bufs;
    bufs.reserve(static_cast<size_t>(n));
    for (long long i = 0; i < n; ++i) {
        const proto::ProtoObject* e = els->getAt(ctx, static_cast<int>(i));
        const proto::ProtoByteBuffer* bb = getByteBuffer(ctx, e);
        if (bb) {
            bufs.push_back(bb);
            total += bb->getSize(ctx);
        }
    }
    // Optional explicit total length (Buffer.concat(arr, totalLength)).
    if (args->getSize(ctx) > 1) {
        long long t = 0;
        if (argInt(ctx, args, 1, t) && t >= 0) total = static_cast<size_t>(t);
    }
    const proto::ProtoObject* bufObj = ctx->newBuffer(total);
    if (!bufObj) return PROTO_NONE;
    const proto::ProtoByteBuffer* out = bufObj->asByteBuffer(ctx);
    if (out && total) {
        char* dst = out->getBuffer(ctx);
        size_t off = 0;
        for (auto* b : bufs) {
            size_t sz = b->getSize(ctx);
            if (off + sz > total) sz = total - off;
            std::memcpy(dst + off, b->getBuffer(ctx), sz);
            off += sz;
            if (off >= total) break;
        }
        if (off < total) std::memset(dst + off, 0, total - off);
    }
    return makeBufferInstance(ctx, bufObj, static_cast<long long>(total));
}

const proto::ProtoObject* bufferIsBufferImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_FALSE;
    return isBufferInstance(ctx, args->getAt(ctx, 0))
        ? PROTO_TRUE : PROTO_FALSE;
}

// ---- Instance methods -------------------------------------------------

const proto::ProtoObject* bufferToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoByteBuffer* bb = getByteBuffer(ctx, self);
    if (!bb) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    unsigned long size = bb->getSize(ctx);
    const char* src = bb->getBuffer(ctx);

    std::string encoding = "utf8";
    if (args && args->getSize(ctx) > 0) argString(ctx, args, 0, encoding);

    long long start = 0, end = static_cast<long long>(size);
    if (args && args->getSize(ctx) > 1) argInt(ctx, args, 1, start);
    if (args && args->getSize(ctx) > 2) argInt(ctx, args, 2, end);
    if (start < 0) start = 0;
    if (end > static_cast<long long>(size)) end = size;
    if (start > end) start = end;

    std::string out = encodeBytes(src + start,
                                    static_cast<size_t>(end - start),
                                    encoding);
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* bufferSlice(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoByteBuffer* bb = getByteBuffer(ctx, self);
    if (!bb) return PROTO_NONE;
    long long size = static_cast<long long>(bb->getSize(ctx));

    long long start = 0, end = size;
    if (args && args->getSize(ctx) > 0) argInt(ctx, args, 0, start);
    if (args && args->getSize(ctx) > 1) argInt(ctx, args, 1, end);
    if (start < 0) start += size;
    if (end < 0) end += size;
    if (start < 0) start = 0;
    if (end > size) end = size;
    if (start > end) start = end;

    long long len = end - start;
    const proto::ProtoObject* outObj = ctx->newBuffer(len);
    if (!outObj) return PROTO_NONE;
    const proto::ProtoByteBuffer* ob = outObj->asByteBuffer(ctx);
    if (ob && len > 0) {
        std::memcpy(ob->getBuffer(ctx), bb->getBuffer(ctx) + start, len);
    }
    return makeBufferInstance(ctx, outObj, len);
}

const proto::ProtoObject* bufferCopy(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoByteBuffer* src = getByteBuffer(ctx, self);
    if (!src || !args || args->getSize(ctx) == 0) return ctx->fromInteger(0);
    const proto::ProtoObject* tgtObj = args->getAt(ctx, 0);
    const proto::ProtoByteBuffer* tgt = getByteBuffer(ctx, tgtObj);
    if (!tgt) return ctx->fromInteger(0);

    long long srcSize = static_cast<long long>(src->getSize(ctx));
    long long tgtSize = static_cast<long long>(tgt->getSize(ctx));
    long long tgtStart = 0, srcStart = 0, srcEnd = srcSize;
    if (args->getSize(ctx) > 1) argInt(ctx, args, 1, tgtStart);
    if (args->getSize(ctx) > 2) argInt(ctx, args, 2, srcStart);
    if (args->getSize(ctx) > 3) argInt(ctx, args, 3, srcEnd);
    if (tgtStart < 0 || srcStart < 0 || srcEnd < 0) return ctx->fromInteger(0);
    if (srcStart > srcSize) srcStart = srcSize;
    if (srcEnd > srcSize) srcEnd = srcSize;
    if (srcStart > srcEnd) srcStart = srcEnd;

    long long n = std::min(srcEnd - srcStart, tgtSize - tgtStart);
    if (n > 0) {
        std::memcpy(tgt->getBuffer(ctx) + tgtStart,
                     src->getBuffer(ctx) + srcStart, n);
    }
    return ctx->fromInteger(n < 0 ? 0 : n);
}

const proto::ProtoObject* bufferFill(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoByteBuffer* bb = getByteBuffer(ctx, self);
    if (!bb || !args || args->getSize(ctx) == 0) return self ? self : PROTO_NONE;
    long long size = static_cast<long long>(bb->getSize(ctx));
    char fill = 0;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (a0 && a0->isInteger(ctx)) fill = static_cast<char>(a0->asLong(ctx) & 0xFF);
    else if (a0 && a0->isString(ctx)) {
        std::string s;
        a0->asString(ctx)->toUTF8String(ctx, s);
        if (!s.empty()) fill = s[0];
    }
    long long off = 0, end = size;
    if (args->getSize(ctx) > 1) argInt(ctx, args, 1, off);
    if (args->getSize(ctx) > 2) argInt(ctx, args, 2, end);
    if (off < 0) off = 0;
    if (end > size) end = size;
    if (off > end) off = end;
    std::memset(bb->getBuffer(ctx) + off, fill,
                  static_cast<size_t>(end - off));
    return self;
}

const proto::ProtoObject* bufferIndexOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoByteBuffer* bb = getByteBuffer(ctx, self);
    if (!bb || !args || args->getSize(ctx) == 0) return ctx->fromInteger(-1);
    long long size = static_cast<long long>(bb->getSize(ctx));
    const char* buf = bb->getBuffer(ctx);

    std::vector<uint8_t> needle;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (a0 && a0->isInteger(ctx)) {
        needle.push_back(static_cast<uint8_t>(a0->asLong(ctx) & 0xFF));
    } else if (a0 && a0->isString(ctx)) {
        std::string s;
        a0->asString(ctx)->toUTF8String(ctx, s);
        std::string enc = "utf8";
        if (args->getSize(ctx) > 2) argString(ctx, args, 2, enc);
        needle = decodeString(s, enc);
    } else if (a0 && isBufferInstance(ctx, a0)) {
        const proto::ProtoByteBuffer* sb = getByteBuffer(ctx, a0);
        if (sb) {
            unsigned long n = sb->getSize(ctx);
            const char* sd = sb->getBuffer(ctx);
            needle.assign(sd, sd + n);
        }
    }
    if (needle.empty()) return ctx->fromInteger(-1);
    long long off = 0;
    if (args->getSize(ctx) > 1) argInt(ctx, args, 1, off);
    if (off < 0) off += size;
    if (off < 0) off = 0;
    if (off >= size) return ctx->fromInteger(-1);

    long long limit = size - static_cast<long long>(needle.size());
    for (long long i = off; i <= limit; ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (static_cast<uint8_t>(buf[i + j]) != needle[j]) {
                match = false; break;
            }
        }
        if (match) return ctx->fromInteger(i);
    }
    return ctx->fromInteger(-1);
}

const proto::ProtoObject* bufferIncludes(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    const proto::ProtoObject* idx =
        bufferIndexOf(ctx, self, pl, args, kw);
    if (!idx || !idx->isInteger(ctx)) return PROTO_FALSE;
    return (idx->asLong(ctx) >= 0) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* getBufferProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"toString", bufferToString},
        {"slice",    bufferSlice},
        {"copy",     bufferCopy},
        {"fill",     bufferFill},
        {"indexOf",  bufferIndexOf},
        {"includes", bufferIncludes},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 6);
    return proto;
}

}  // namespace

const proto::ProtoObject* BufferModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    const proto::ProtoObject* bufferProto = getBufferProto(ctx);

    // Build the Buffer constructor (callable for `new Buffer(...)`).
    const proto::ProtoObject* bufferCtor =
        wrapNativeFunction(ctx, bufferConstructor, "Buffer",
                            /*length=*/2, /*globalRoot=*/nullptr);
    if (!bufferCtor) return globalObj;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey)
        bufferCtor = bufferCtor->setAttribute(ctx, protoKey, bufferProto);
    {
        const proto::ProtoString* ck =
            ctx->fromUTF8String("__construct__")->asString(ctx);
        if (ck) bufferCtor = bufferCtor->setAttribute(ctx, ck,
            ctx->fromMethod(nullptr, bufferConstructor));
    }
    // Static factory methods on the constructor.
    auto installStatic = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* k =
            ctx->fromUTF8String(name)->asString(ctx);
        if (!k) return;
        bufferCtor = bufferCtor->setAttribute(ctx, k,
            ctx->fromMethod(nullptr, fn));
    };
    installStatic("from",     bufferFromImpl);
    installStatic("alloc",    bufferAllocImpl);
    installStatic("concat",   bufferConcatImpl);
    installStatic("isBuffer", bufferIsBufferImpl);

    // Install on the protoCore-native global (keyed by "Buffer", as a
    // top-level global rather than as a `buffer` module — this matches
    // both the original module's behaviour and Node's.)
    const proto::ProtoString* nameKey =
        ctx->fromUTF8String("Buffer")->asString(ctx);
    if (!nameKey) return globalObj;
    return globalObj->setAttribute(ctx, nameKey, bufferCtor);
}

} // namespace protojs
