#pragma once

// The UTF-16 view of a protoCore string.
//
// A JavaScript string is a sequence of 16-bit code units (ECMA-262 §6.1.4): a
// code point above U+FFFF is two units (a surrogate pair) and a lone
// surrogate is one unit. protoCore stores a string as UTF-8 and indexes it by
// code point (getAt, getSize, getSlice). Every JavaScript-visible index and
// length goes through the functions below, which present the code-unit view
// on top of that storage.
//
// Representation (agent, pending review):
//
// * A lone surrogate is stored as the code point of the same value (its
//   three-byte generalized UTF-8 form), as protoCore stores any BMP code
//   point.
// * Canonical form: a string never holds a lone high surrogate immediately
//   followed by a lone low surrogate; such a pair is the one supplementary
//   code point they encode. "\uD83D" + "\uDE00" must be the same string as
//   "\u{1F600}" (same length, ===, same Map key, same property key), so every
//   operation that joins strings keeps this form: jsConcatStrings joins two
//   strings and repairs the seam, and jsStringFromUTF8 (JSStringBytes.h)
//   combines a three-byte high surrogate followed by a three-byte low one.
//   Slicing a canonical string cannot create a pair, so it needs no repair.
//
// Cost: the view of a string longer than kUtf16DirectLimit code points is
// built by one scan (about 1.3 ns per character) and memoised per thread: an
// array of its UTF-16 code units plus the unit positions of its supplementary
// characters. Every later length, s[i], charAt, charCodeAt, codePointAt, at,
// slice and Array-method read of that string is O(1) (O(log s) for a slice
// boundary, s the number of supplementary characters). A string with no
// supplementary character maps unit index i to code point index i, so its
// slices are protoCore slices with no adjustment. Short strings are decoded
// directly, without the memo. The length (and the first and last units) of
// a string made by jsConcatStrings or utf16Slice from strings of known length
// is recorded without a scan, so a loop that builds a new string per
// iteration and reads its length stays linear. See JSStringUnits.cpp for the
// memo's bounds and its GC safety.

#include <protoCore.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace protojs {

// Strings of at most this many code points are decoded directly on each
// access instead of being memoised.
constexpr long long kUtf16DirectLimit = 32;

// The number of UTF-16 code units of `str`.
long long utf16Length(proto::ProtoContext* ctx, const proto::ProtoString* str);

// The code unit at `unitIndex` of `str` (a surrogate half for a code point
// above U+FFFF). False when the index is out of range.
bool utf16UnitAt(proto::ProtoContext* ctx, const proto::ProtoString* str,
                 long long unitIndex, uint16_t& unit);

// The one-code-unit string at `unitIndex` (s[i], charAt): a lone surrogate
// when the index falls on half of a surrogate pair. nullptr when the index is
// out of range. An inline string: no cell is allocated.
const proto::ProtoObject* utf16CharAt(proto::ProtoContext* ctx,
                                      const proto::ProtoString* str,
                                      long long unitIndex);

// The code point starting at `unitIndex` (String.prototype.codePointAt): a
// surrogate pair gives its code point, a lone surrogate or the low half of a
// pair gives the unit. False when the index is out of range.
bool utf16CodePointAt(proto::ProtoContext* ctx, const proto::ProtoString* str,
                      long long unitIndex, uint32_t& cp);

// The substring of code units [from, to) of `str`; 0 <= from <= to <= length.
// A boundary inside a surrogate pair leaves the half it cuts as a lone
// surrogate. Shares the rope of `str` (protoCore getSlice).
const proto::ProtoString* utf16Slice(proto::ProtoContext* ctx,
                                     const proto::ProtoString* str,
                                     long long from, long long to);

// The code-point index of code unit `unitIndex` (0 <= unitIndex <= length),
// rounded down when it falls on the low half of a pair; `inside` is set when
// it does.
long long utf16ToCodePointIndex(proto::ProtoContext* ctx, const proto::ProtoString* str,
                                long long unitIndex, bool& inside);

// The code-unit index of code point `cpIndex` (0 <= cpIndex <= getSize).
long long codePointToUtf16Index(proto::ProtoContext* ctx, const proto::ProtoString* str,
                                long long cpIndex);

// All the code units of `str`.
void utf16Units(proto::ProtoContext* ctx, const proto::ProtoString* str, std::u16string& out);

// The string of code units units[0, n): adjacent surrogate halves become the
// code point they encode, a lone surrogate stays a code point of its own.
const proto::ProtoObject* jsStringFromUtf16(proto::ProtoContext* ctx,
                                            const char16_t* units, std::size_t n);
inline const proto::ProtoObject* jsStringFromUtf16(proto::ProtoContext* ctx,
                                                   const std::u16string& units) {
    return jsStringFromUtf16(ctx, units.data(), units.size());
}

// Appends the generalized UTF-8 encoding of the code units units[0, n) to
// `out`: pairs as four bytes, lone surrogates as three.
void appendUtf16AsUTF8(std::string& out, const char16_t* units, std::size_t n);

// Appends the code units of the generalized UTF-8 bytes utf8[0, n).
void appendUTF8AsUtf16(std::u16string& out, const char* utf8, std::size_t n);

// a + b in canonical form: a lone high surrogate ending `a` and a lone low
// surrogate starting `b` become one code point.
const proto::ProtoString* jsConcatStrings(proto::ProtoContext* ctx,
                                          const proto::ProtoString* a,
                                          const proto::ProtoString* b);

// Compares two strings by their UTF-16 code units (§7.2.13 IsLessThan, the
// default Array.prototype.sort order): negative, zero or positive. Code-point
// order differs from it when a supplementary character meets a BMP character
// at or above U+D800.
int utf16Compare(proto::ProtoContext* ctx, const proto::ProtoString* a,
                 const proto::ProtoString* b);

// utf16Compare over two generalized UTF-8 byte strings in canonical form.
int utf16CompareUTF8(const std::string& a, const std::string& b);

// True when `str` has no supplementary code point: unit indices are code
// point indices.
bool utf16IsBmpOnly(proto::ProtoContext* ctx, const proto::ProtoString* str);

}  // namespace protojs
