#include "../../ProtoCoreTypes.h"
#include "../../runtime/JSStringBytes.h"
#include "../../runtime/PinnedBuiltin.h"
#include "BufferModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../JSContext.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../ArrayBufferPrototype.h"
#include "../../TypedArrayPrototype.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace protojs {

namespace {

// ---- Argument helpers --------------------------------------------------

bool argInt(proto::ProtoContext* ctx, const proto::ProtoArguments* args,
             int idx, long long& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isInteger(ctx)) return false;
    out = a->asLong(ctx);
    return true;
}

bool argString(proto::ProtoContext* ctx, const proto::ProtoArguments* args,
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
    if (encoding == "latin1" || encoding == "binary") {
        // One byte per code point: its low eight bits (Node.js).
        for (size_t i = 0; i < str.size();) {
            const unsigned char c = static_cast<unsigned char>(str[i]);
            uint32_t cp = c;
            int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            if (n > 1) cp = c & (0xFF >> (n + 1));
            for (int j = 1; j < n && i + j < str.size(); ++j)
                cp = (cp << 6) | (static_cast<unsigned char>(str[i + j]) & 0x3F);
            out.push_back(static_cast<uint8_t>(cp & 0xFF));
            i += n;
        }
    } else if (encoding == "utf8" || encoding == "utf-8" || encoding == "ascii") {
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
    if (encoding == "latin1" || encoding == "binary") {
        // Each byte is the code point of the same value (Node.js); it was
        // copied as UTF-8, which dropped every byte above 0x7F.
        std::string s;
        s.reserve(len * 2);
        for (size_t i = 0; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(data[i]);
            if (c < 0x80) {
                s += static_cast<char>(c);
            } else {
                s += static_cast<char>(0xC0 | (c >> 6));
                s += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return s;
    }
    if (encoding == "utf8" || encoding == "utf-8" || encoding == "ascii") {
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

// ---- Bytes of a Buffer --------------------------------------------------
//
// A Buffer is a Uint8Array, as in Node: Buffer.prototype inherits from
// Uint8Array.prototype and each Buffer is a typed array (an ArrayBuffer, a
// byte offset and a length) made with Buffer.prototype.  Until 2026-10 a
// Buffer held a ProtoByteBuffer in a hidden attribute: no indexing, not an
// instance of Uint8Array, and "[object Object]" for Object.prototype.toString.

// The bytes a byte-element typed array (a Buffer, a Uint8Array, an
// Int8Array or a Uint8ClampedArray) views.
struct ByteView {
    char* data = nullptr;
    size_t size = 0;
};

bool byteView(proto::ProtoContext* ctx, const proto::ProtoObject* val, ByteView& out) {
    out = ByteView{};
    if (!ctx || !val || val == PROTO_NONE || !isTypedArrayInstance(ctx, val)) return false;
    const uint8_t et = getTypedArrayElementType(ctx, val);
    if (et > static_cast<uint8_t>(TAElementType::Uint8Clamped)) return false;
    const proto::ProtoObject* ab = val->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    char* base = ab ? static_cast<char*>(getArrayBufferRawPtr(ctx, ab)) : nullptr;
    const proto::ProtoObject* off = val->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    const long long byteOffset = (off && off->isInteger(ctx)) ? off->asLong(ctx) : 0;
    out.size = getTypedArrayLength(ctx, val);
    out.data = base ? base + byteOffset : nullptr;
    return out.data != nullptr || out.size == 0;
}

const proto::ProtoObject* getBufferProto(proto::ProtoContext* ctx);

bool isBufferInstance(proto::ProtoContext* ctx,
                       const proto::ProtoObject* val) {
    if (!ctx || !val || val == PROTO_NONE || !isTypedArrayInstance(ctx, val)) return false;
    const proto::ProtoObject* proto = getBufferProto(ctx);
    return proto && val->isInstanceOf(ctx, proto) == PROTO_TRUE;
}

// A new zero-filled Buffer of `size` bytes, or PROTO_NONE.
const proto::ProtoObject* newZeroedBuffer(proto::ProtoContext* ctx, long long size) {
    if (!ctx || size < 0 || size > 0x7FFFFFFFLL) return PROTO_NONE;
    return createTypedArrayFromLength(ctx, getBufferProto(ctx),
                                      static_cast<uint8_t>(TAElementType::Uint8),
                                      static_cast<uint32_t>(size));
}

// A Buffer holding a copy of `bytes`.
const proto::ProtoObject* makeFromBytes(proto::ProtoContext* ctx,
                                          const std::vector<uint8_t>& bytes) {
    const proto::ProtoObject* buf =
        newZeroedBuffer(ctx, static_cast<long long>(bytes.size()));
    ByteView v;
    if (buf != PROTO_NONE && !bytes.empty() && byteView(ctx, buf, v))
        std::memcpy(v.data, bytes.data(), bytes.size());
    return buf;
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
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (!a0) return PROTO_NONE;

    if (a0->isInteger(ctx)) {
        // Zero-filled, as Buffer.alloc (Node's `new Buffer(size)` is too).
        return newZeroedBuffer(ctx, a0->asLong(ctx));
    }

    // Buffer.from(arrayBuffer[, byteOffset[, length]]): a view over the same
    // memory, as in Node.
    if (isArrayBuffer(ctx, a0)) {
        const long long abLen = static_cast<long long>(getArrayBufferByteLength(ctx, a0));
        long long off = 0, len = -1;
        if (args->getSize(ctx) > 1) argInt(ctx, args, 1, off);
        if (args->getSize(ctx) > 2) argInt(ctx, args, 2, len);
        if (off < 0 || off > abLen) {
            signalNativeException(makeNativeError(ctx, "RangeError",
                "\"offset\" is outside of buffer bounds"));
        }
        if (len < 0) len = abLen - off;
        if (off + len > abLen) {
            signalNativeException(makeNativeError(ctx, "RangeError",
                "\"length\" is outside of buffer bounds"));
        }
        return createTypedArrayFromBuffer(ctx, getBufferProto(ctx),
                                          static_cast<uint8_t>(TAElementType::Uint8),
                                          a0, off, len);
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
    // A Buffer or any other typed array: its elements, each taken modulo
    // 256 (Node's Buffer.from(typedArray)).
    if (isTypedArrayInstance(ctx, val)) {
        ByteView v;
        if (byteView(ctx, val, v)) {
            out.assign(v.data, v.data + v.size);
            return val;
        }
        const uint8_t et = getTypedArrayElementType(ctx, val);
        const uint32_t n = getTypedArrayLength(ctx, val);
        out.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            const proto::ProtoObject* e = typedArrayGetElement(ctx, val, i, et);
            long long iv = 0;
            if (e && e->isInteger(ctx)) iv = e->asLong(ctx);
            else if (e && e->isDouble(ctx)) iv = static_cast<long long>(e->asDouble(ctx));
            out.push_back(static_cast<uint8_t>(iv & 0xFF));
        }
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
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kw) {
    return bufferConstructor(ctx, self, pl, args, kw);
}

const proto::ProtoObject* bufferAllocImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    long long size = 0;
    if (!argInt(ctx, args, 0, size) || size < 0) return PROTO_NONE;
    const proto::ProtoObject* buf = newZeroedBuffer(ctx, size);
    ByteView bb;
    if (buf != PROTO_NONE && byteView(ctx, buf, bb) && size > 0) {
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
        std::memset(bb.data, fill, static_cast<size_t>(size));
    }
    return buf;
}

const proto::ProtoObject* bufferConcatImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* arr = args->getAt(ctx, 0);
    const proto::ProtoList* els = arr ? getArrayElements(ctx, arr) : nullptr;
    if (!els) return PROTO_NONE;
    long long n = static_cast<long long>(els->getSize(ctx));
    size_t total = 0;
    std::vector<ByteView> bufs;
    bufs.reserve(static_cast<size_t>(n));
    for (long long i = 0; i < n; ++i) {
        const proto::ProtoObject* e = els->getAt(ctx, static_cast<int>(i));
        ByteView bb;
        if (byteView(ctx, e, bb)) {
            bufs.push_back(bb);
            total += bb.size;
        }
    }
    // Optional explicit total length (Buffer.concat(arr, totalLength)).
    if (args->getSize(ctx) > 1) {
        long long t = 0;
        if (argInt(ctx, args, 1, t) && t >= 0) total = static_cast<size_t>(t);
    }
    const proto::ProtoObject* result = newZeroedBuffer(ctx, static_cast<long long>(total));
    ByteView out;
    if (result != PROTO_NONE && total && byteView(ctx, result, out)) {
        char* dst = out.data;
        size_t off = 0;
        for (const ByteView& b : bufs) {
            size_t sz = b.size;
            if (off + sz > total) sz = total - off;
            if (sz) std::memcpy(dst + off, b.data, sz);
            off += sz;
            if (off >= total) break;
        }
    }
    return result;
}

const proto::ProtoObject* bufferIsBufferImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_FALSE;
    return isBufferInstance(ctx, args->getAt(ctx, 0))
        ? PROTO_TRUE : PROTO_FALSE;
}

// ---- Instance methods -------------------------------------------------

const proto::ProtoObject* bufferToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    ByteView bb;
    if (!byteView(ctx, self, bb)) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    proto::proto_ulong size = bb.size;
    const char* src = bb.data;

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
    return protojs::jsStringFromUTF8(ctx, out);
}

// buf.slice(start, end) and buf.subarray(start, end): a Buffer over the same
// memory (Node; until 2026-10 slice copied the bytes).
const proto::ProtoObject* bufferSlice(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    ByteView bb;
    if (!byteView(ctx, self, bb)) return PROTO_NONE;
    long long size = static_cast<long long>(bb.size);

    long long start = 0, end = size;
    if (args && args->getSize(ctx) > 0) argInt(ctx, args, 0, start);
    if (args && args->getSize(ctx) > 1) argInt(ctx, args, 1, end);
    if (start < 0) start += size;
    if (end < 0) end += size;
    if (start < 0) start = 0;
    if (end > size) end = size;
    if (start > end) start = end;

    const proto::ProtoObject* ab = self->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    const proto::ProtoObject* off = self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    const long long byteOffset = (off && off->isInteger(ctx)) ? off->asLong(ctx) : 0;
    return createTypedArrayFromBuffer(ctx, getBufferProto(ctx),
                                      static_cast<uint8_t>(TAElementType::Uint8),
                                      ab, byteOffset + start, end - start);
}

const proto::ProtoObject* bufferCopy(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    ByteView src, tgt;
    if (!byteView(ctx, self, src) || !args || args->getSize(ctx) == 0) return ctx->fromInteger(0);
    if (!byteView(ctx, args->getAt(ctx, 0), tgt)) return ctx->fromInteger(0);

    long long srcSize = static_cast<long long>(src.size);
    long long tgtSize = static_cast<long long>(tgt.size);
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
        std::memmove(tgt.data + tgtStart, src.data + srcStart, static_cast<size_t>(n));
    }
    return ctx->fromInteger(n < 0 ? 0 : n);
}

const proto::ProtoObject* bufferFill(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    ByteView bb;
    if (!byteView(ctx, self, bb) || !args || args->getSize(ctx) == 0) return self ? self : PROTO_NONE;
    long long size = static_cast<long long>(bb.size);
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
    std::memset(bb.data + off, fill,
                  static_cast<size_t>(end - off));
    return self;
}

const proto::ProtoObject* bufferIndexOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    ByteView bb;
    if (!byteView(ctx, self, bb) || !args || args->getSize(ctx) == 0) return ctx->fromInteger(-1);
    long long size = static_cast<long long>(bb.size);
    const char* buf = bb.data;

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
    } else {
        ByteView sb;
        if (byteView(ctx, a0, sb)) needle.assign(sb.data, sb.data + sb.size);
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
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kw) {
    const proto::ProtoObject* idx =
        bufferIndexOf(ctx, self, pl, args, kw);
    if (!idx || !idx->isInteger(ctx)) return PROTO_FALSE;
    return (idx->asLong(ctx) >= 0) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* getBufferProto(proto::ProtoContext* ctx) {
    // Made on first use; pinned for the wrapper's life (runtime/PinnedBuiltin.h).
    static PinnedBuiltin protoCache;
    if (const proto::ProtoObject* cached = protoCache.get(ctx)) return cached;
    static const NativeEntry entries[] = {
        {"toString", bufferToString},
        {"slice",    bufferSlice},
        {"subarray", bufferSlice},
        {"copy",     bufferCopy},
        {"fill",     bufferFill},
        {"indexOf",  bufferIndexOf},
        {"includes", bufferIncludes},
        NATIVE_MODULE_END
    };
    // Buffer.prototype inherits from Uint8Array.prototype (Node), and so from
    // %TypedArray%.prototype and Object.prototype.  Without the typed-array
    // constructors (they are installed before the modules) it falls back to
    // the realm's Object.prototype, so a Buffer is never a foreign object.
    const proto::ProtoObject* parent =
        getTypedArrayConcreteProto(ctx, static_cast<uint8_t>(TAElementType::Uint8));
    if (!parent || parent == PROTO_NONE) {
        JSContextWrapper* w = JSContextWrapper::current();
        parent = w ? w->getJSObjectPrototype() : nullptr;
    }
    const proto::ProtoObject* bufferProto = parent
        ? parent->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    for (const NativeEntry* e = entries; e->name; ++e)
        bufferProto = ProtoNativeModule::addMethod(ctx, bufferProto, e->name, e->fn);
    return protoCache.keep(ctx, bufferProto);
}

}  // namespace

const proto::ProtoObject* BufferModule::fromBytes(proto::ProtoContext* ctx,
                                                  const char* data, size_t len) {
    const proto::ProtoObject* buf = newZeroedBuffer(ctx, static_cast<long long>(len));
    ByteView v;
    if (buf != PROTO_NONE && len && data && byteView(ctx, buf, v))
        std::memcpy(v.data, data, len);
    return buf;
}

bool BufferModule::bytesOf(proto::ProtoContext* ctx, const proto::ProtoObject* val,
                           const char*& data, size_t& len) {
    ByteView v;
    if (!byteView(ctx, val, v)) return false;
    data = v.data;
    len = v.size;
    return true;
}

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
