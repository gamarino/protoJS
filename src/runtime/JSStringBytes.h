#pragma once

// Length-aware construction of protoCore strings from UTF-8 bytes.
//
// A JavaScript string may contain U+0000. ProtoContext::fromUTF8String and
// ProtoString::createSymbol take a zero-terminated C string and stop at the
// first NUL byte, so "a\0b" built through them became "a". These helpers take
// the byte count explicitly. A buffer without a NUL byte (nearly every string)
// takes the same constructor as before, so its result is unchanged; one that
// carries a NUL is built with ProtoString::fromUTF8Buffer, which honours the
// length, and is interned with ProtoString::intern (protoCore 3.2.0 and later)
// when a symbol is needed.
//
// Both helpers also keep the canonical form of JSStringUnits.h: the three-byte
// encodings of a high surrogate followed by a low one (ED A0..AF xx ED B0..BF
// yy, as QuickJS hands over "😀") become the four-byte encoding of
// the code point they make. A buffer without the byte 0xED skips this.
//
// The helpers are out of line (JSStringBytes.cpp): a call site costs one call,
// as the fromUTF8String call it replaced did. Inlined, the NUL test and both
// constructor paths at the 88 sites of runBytecodeImpl moved its hot loop and
// cost the numeric_loop benchmark about 10 % more cycles (perf stat).

#include <protoCore.h>

#include <cstddef>
#include <string>

namespace protojs {

// The string whose UTF-8 encoding is bytes[0, len). bytes[len] must be a NUL
// (a std::string's data(), or a QuickJS C string).
const proto::ProtoObject* jsStringFromUTF8(proto::ProtoContext* ctx,
                                           const char* bytes, std::size_t len);

inline const proto::ProtoObject* jsStringFromUTF8(proto::ProtoContext* ctx,
                                                   const std::string& utf8) {
    return jsStringFromUTF8(ctx, utf8.c_str(), utf8.size());
}

// The interned symbol whose UTF-8 encoding is bytes[0, len); bytes[len] must
// be a NUL.
const proto::ProtoString* jsSymbolFromUTF8(proto::ProtoContext* ctx,
                                           const char* bytes, std::size_t len);

inline const proto::ProtoString* jsSymbolFromUTF8(proto::ProtoContext* ctx,
                                                   const std::string& utf8) {
    return jsSymbolFromUTF8(ctx, utf8.c_str(), utf8.size());
}

}  // namespace protojs
