// The UTF-16 view of a protoCore string (JSStringUnits.h).

#include "JSStringUnits.h"

#include "JSStringBytes.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Generalized UTF-8 <-> UTF-16
// ---------------------------------------------------------------------------

inline bool isHigh(uint32_t u) { return u >= 0xD800u && u <= 0xDBFFu; }
inline bool isLow(uint32_t u) { return u >= 0xDC00u && u <= 0xDFFFu; }

// Decodes the code point at utf8[i] and advances i. protoCore's strings are
// well formed in the generalized sense (lone surrogates are three-byte
// sequences), so no validation is needed beyond the bounds.
inline uint32_t decodeAt(const unsigned char* p, std::size_t n, std::size_t& i) {
    const unsigned char c = p[i];
    uint32_t cp;
    int len;
    if (c < 0x80)      { cp = c;        len = 1; }
    else if (c < 0xE0) { cp = c & 0x1F; len = 2; }
    else if (c < 0xF0) { cp = c & 0x0F; len = 3; }
    else               { cp = c & 0x07; len = 4; }
    for (int k = 1; k < len && i + k < n; ++k) cp = (cp << 6) | (p[i + k] & 0x3F);
    i += static_cast<std::size_t>(len);
    return cp;
}

inline void appendCodePointUTF8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The one-code-point string of `cp`; inline (no cell).
const proto::ProtoObject* stringOfCp(proto::ProtoContext* ctx, uint32_t cp) {
    std::string b;
    appendCodePointUTF8(b, cp);
    return jsStringFromUTF8(ctx, b.data(), b.size());
}

uint32_t cpAt(proto::ProtoContext* ctx, const proto::ProtoString* str, long long i) {
    const proto::ProtoObject* o = str->getAt(ctx, static_cast<int>(i));
    return static_cast<uint32_t>(o ? o->asLong(ctx) : 0);
}

// ---------------------------------------------------------------------------
// The view and its memo
// ---------------------------------------------------------------------------
//
// A view holds the code units of one string and the unit positions of its
// supplementary characters (the high half of each pair), in order. `units`
// is left empty when the string alone exceeds the memo's unit budget; such a
// string is then read through `supp` and ProtoString::getAt (O(log n)).
//
// The memo is per thread and keyed by the string's address. It holds no cell,
// only addresses, so it keeps nothing alive; it is emptied whenever a
// collection has started since the last access, before a dead key's address
// can be reused (the rule of GcScopedCache.h), and whenever it holds more than
// kMemoEntries strings or kMemoUnits code units. A front entry (the last view
// used) makes a loop over one string one compare per access. (Agent, pending
// review: pinning the memoised strings in one holder would let entries survive
// collections, at the price of keeping up to kMemoEntries strings alive; the
// rebuild after a collection costs one scan per string still in use.)

struct View {
    std::vector<char16_t> units;
    std::vector<uint32_t> supp;
    long long length = 0;      // in code units
    long long cps = 0;         // in code points
    bool materialised = false; // units is filled
};

// What jsConcatStrings needs of an operand, and what `length` needs of a
// string: kept for short strings once decoded, and for long strings made by
// jsConcatStrings or utf16Slice, so that neither has to be scanned. The
// UTF-16 units of a + b are the units of a followed by those of b (a seam
// that joins a pair keeps both halves as units), so every field of a
// concatenation follows from its operands'.
struct LengthInfo {
    long long length = 0;      // in code units
    bool bmpOnly = true;       // no supplementary code point
    bool edges = false;        // first and last are set
    uint16_t first = 0;        // the first and last code units
    uint16_t last = 0;
};

// A slot of the length table: direct mapped by the string's pointer (a cell
// address or an inline value), overwritten on a collision, valid while its
// generation is the memo's.
struct LengthSlot {
    const proto::ProtoString* key = nullptr;
    LengthInfo info;
    std::uint32_t generation = 0;
};

constexpr std::size_t kMemoEntries = 64;
constexpr unsigned kLengthBits = 12;   // 4096 slots, 128 KiB per thread
constexpr std::size_t kLengthSlots = std::size_t(1) << kLengthBits;
constexpr std::size_t kMemoUnits = std::size_t(1) << 24;   // 32 MiB of units

struct Memo {
    std::unordered_map<const proto::ProtoString*, View> map;
    std::unique_ptr<LengthSlot[]> lengths;   // allocated on first use
    std::uint32_t generation = 1;   // clear() invalidates every LengthSlot
    std::size_t units = 0;
    const proto::ProtoSpace* space = nullptr;
    std::uint64_t cycle = 0;
    const proto::ProtoString* frontKey = nullptr;
    const View* front = nullptr;
    const proto::ProtoString* derivedKey = nullptr;   // see derivedBmpLength
    unsigned derivedHits = 0;
    View scratch;              // the view of a short string, rebuilt per access
    std::string bytes;         // scratch UTF-8

    void clear() {
        map.clear();
        ++generation;
        units = 0;
        frontKey = nullptr;
        front = nullptr;
        derivedKey = nullptr;
        derivedHits = 0;
    }
};

thread_local Memo t_memo;

void buildView(proto::ProtoContext* ctx, const proto::ProtoString* str, View& v,
               std::string& bytes, bool materialise) {
    bytes.clear();
    str->toUTF8String(ctx, bytes);
    v.units.clear();
    v.supp.clear();
    v.materialised = materialise;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t n = bytes.size();
    if (materialise) v.units.reserve(n);
    long long u = 0, cps = 0;
    for (std::size_t i = 0; i < n;) {
        if (p[i] < 0xF0) {
            // One unit. A fast skip when the units are not kept.
            if (materialise) {
                v.units.push_back(static_cast<char16_t>(decodeAt(p, n, i)));
            } else {
                const unsigned char c = p[i];
                i += c < 0x80 ? 1 : c < 0xE0 ? 2 : 3;
            }
            ++u;
        } else {
            const uint32_t cp = decodeAt(p, n, i) - 0x10000u;
            v.supp.push_back(static_cast<uint32_t>(u));
            if (materialise) {
                v.units.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
                v.units.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
            }
            u += 2;
        }
        ++cps;
    }
    v.length = u;
    v.cps = cps;
}

// Empties the memo when a collection has started since its last use (or the
// space changed); true when it is still valid.
inline bool syncMemo(Memo& m, proto::ProtoContext* ctx) {
    const proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
    const std::uint64_t cycle = space ? space->getGCCycleCount() : 0;
    if (space == m.space && cycle == m.cycle) return true;
    m.clear();
    m.space = space;
    m.cycle = cycle;
    return false;
}

const View& viewOf(proto::ProtoContext* ctx, const proto::ProtoString* str) {
    Memo& m = t_memo;
    if (syncMemo(m, ctx) && m.frontKey == str && m.front) return *m.front;
    const long long n = static_cast<long long>(str->getSize(ctx));
    if (n <= kUtf16DirectLimit) {
        buildView(ctx, str, m.scratch, m.bytes, true);
        m.frontKey = str;
        m.front = &m.scratch;
        return m.scratch;
    }
    auto it = m.map.find(str);
    if (it == m.map.end()) {
        if (m.map.size() >= kMemoEntries || m.units >= kMemoUnits) m.clear();
        View v;
        // A string over the whole budget keeps only its supplementary positions.
        const bool materialise = static_cast<std::size_t>(n) * 2 <= kMemoUnits;
        buildView(ctx, str, v, m.bytes, materialise);
        m.units += v.units.size() + v.supp.size() / 2;
        it = m.map.emplace(str, std::move(v)).first;
    }
    m.frontKey = str;
    m.front = &it->second;
    return it->second;
}

// The number of supplementary characters wholly before unit u: supp[j] + 2 <= u.
inline std::size_t suppBefore(const View& v, long long u) {
    std::size_t lo = 0, hi = v.supp.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (static_cast<long long>(v.supp[mid]) + 2 <= u) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

inline uint16_t unitOfCp(uint32_t cp, int half) {
    if (cp < 0x10000u) return static_cast<uint16_t>(cp);
    cp -= 0x10000u;
    return half == 0 ? static_cast<uint16_t>(0xD800u + (cp >> 10))
                     : static_cast<uint16_t>(0xDC00u + (cp & 0x3FFu));
}

// The unit at u of a view (0 <= u < length).
inline uint16_t unitIn(proto::ProtoContext* ctx, const proto::ProtoString* str,
                       const View& v, long long u) {
    if (v.materialised) return v.units[static_cast<std::size_t>(u)];
    const std::size_t k = suppBefore(v, u);
    if (k < v.supp.size() && static_cast<long long>(v.supp[k]) <= u) {
        const long long cpIndex = static_cast<long long>(v.supp[k]) - static_cast<long long>(k);
        return unitOfCp(cpAt(ctx, str, cpIndex), static_cast<int>(u - v.supp[k]));
    }
    return static_cast<uint16_t>(cpAt(ctx, str, u - static_cast<long long>(k)));
}

inline LengthSlot& lengthSlot(Memo& m, const proto::ProtoString* str) {
    if (!m.lengths) m.lengths.reset(new LengthSlot[kLengthSlots]);
    // Fibonacci hashing: inline strings carry their bytes in the pointer.
    const std::uint64_t h = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(str))
                            * 0x9E3779B97F4A7C15ull;
    return m.lengths[h >> (64 - kLengthBits)];
}

inline const LengthInfo* derivedLength(Memo& m, const proto::ProtoString* str) {
    if (!m.lengths) return nullptr;
    const LengthSlot& sl = lengthSlot(m, str);
    return sl.key == str && sl.generation == m.generation ? &sl.info : nullptr;
}

void recordLength(const proto::ProtoString* r, const LengthInfo& info) {
    Memo& m = t_memo;
    LengthSlot& sl = lengthSlot(m, r);
    sl.key = r;
    sl.info = info;
    sl.generation = m.generation;
}

inline void fillFromView(const View& v, LengthInfo& out) {
    out.length = v.length;
    out.bmpOnly = v.supp.empty();
    out.edges = v.materialised && !v.units.empty();
    if (out.edges) {
        out.first = v.units.front();
        out.last = v.units.back();
    }
}

// The LengthInfo of `str` when it is known without a scan of a long string:
// a recorded one, a short string (decoded once, then recorded), the front
// view or a memoised view. `size` is str->getSize(ctx), the code-point count.
bool knownLength(proto::ProtoContext* ctx, const proto::ProtoString* str, long long size,
                 LengthInfo& out) {
    Memo& m = t_memo;
    syncMemo(m, ctx);
    if (const LengthInfo* li = derivedLength(m, str)) { out = *li; return true; }
    if (size <= kUtf16DirectLimit) {
        fillFromView(viewOf(ctx, str), out);
        recordLength(str, out);
        return true;
    }
    if (m.frontKey == str && m.front) { fillFromView(*m.front, out); return true; }
    const auto vi = m.map.find(str);
    if (vi != m.map.end()) { fillFromView(vi->second, out); return true; }
    return false;
}

// Accesses to one string served by derivedBmpLength before its view is built.
constexpr unsigned kDerivedReads = 64;

// True, with the length, when `str` has a derived length (no view yet) and no
// supplementary character: unit i is then code point i, read with
// ProtoString::getAt (O(log n)) instead of a scan of the whole string. After
// kDerivedReads accesses in a row to the same string this answers false, so a
// loop over one string builds its view once and reads it in O(1).
bool derivedBmpLength(proto::ProtoContext* ctx, const proto::ProtoString* str, long long& len) {
    Memo& m = t_memo;
    if (!syncMemo(m, ctx) || m.frontKey == str) return false;
    const LengthInfo* li = derivedLength(m, str);
    if (!li || !li->bmpOnly) return false;
    if (m.derivedKey == str) {
        if (++m.derivedHits > kDerivedReads) return false;
    } else {
        m.derivedKey = str;
        m.derivedHits = 1;
    }
    len = li->length;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

long long utf16Length(proto::ProtoContext* ctx, const proto::ProtoString* str) {
    if (!str) return 0;
    LengthInfo li;
    if (knownLength(ctx, str, static_cast<long long>(str->getSize(ctx)), li)) return li.length;
    return viewOf(ctx, str).length;
}

bool utf16IsBmpOnly(proto::ProtoContext* ctx, const proto::ProtoString* str) {
    if (!str) return true;
    LengthInfo li;
    if (knownLength(ctx, str, static_cast<long long>(str->getSize(ctx)), li)) return li.bmpOnly;
    return viewOf(ctx, str).supp.empty();
}

bool utf16UnitAt(proto::ProtoContext* ctx, const proto::ProtoString* str,
                 long long unitIndex, uint16_t& unit) {
    if (!str || unitIndex < 0) return false;
    long long len = 0;
    if (derivedBmpLength(ctx, str, len)) {
        if (unitIndex >= len) return false;
        unit = static_cast<uint16_t>(cpAt(ctx, str, unitIndex));
        return true;
    }
    const View& v = viewOf(ctx, str);
    if (unitIndex >= v.length) return false;
    unit = unitIn(ctx, str, v, unitIndex);
    return true;
}

const proto::ProtoObject* utf16CharAt(proto::ProtoContext* ctx,
                                      const proto::ProtoString* str,
                                      long long unitIndex) {
    uint16_t unit = 0;
    if (!utf16UnitAt(ctx, str, unitIndex, unit)) return nullptr;
    return stringOfCp(ctx, unit);
}

bool utf16CodePointAt(proto::ProtoContext* ctx, const proto::ProtoString* str,
                      long long unitIndex, uint32_t& cp) {
    if (!str || unitIndex < 0) return false;
    long long len = 0;
    if (derivedBmpLength(ctx, str, len)) {
        // No pair: every unit is a code point (a lone surrogate included).
        if (unitIndex >= len) return false;
        cp = cpAt(ctx, str, unitIndex);
        return true;
    }
    const View& v = viewOf(ctx, str);
    if (unitIndex >= v.length) return false;
    const uint16_t h = unitIn(ctx, str, v, unitIndex);
    cp = h;
    if (isHigh(h) && unitIndex + 1 < v.length) {
        const uint16_t l = unitIn(ctx, str, v, unitIndex + 1);
        if (isLow(l)) cp = 0x10000u + ((static_cast<uint32_t>(h) - 0xD800u) << 10) + (l - 0xDC00u);
    }
    return true;
}

long long utf16ToCodePointIndex(proto::ProtoContext* ctx, const proto::ProtoString* str,
                                long long unitIndex, bool& inside) {
    inside = false;
    if (!str || unitIndex <= 0) return 0;
    const View& v = viewOf(ctx, str);
    if (v.supp.empty()) return unitIndex;
    const std::size_t k = suppBefore(v, unitIndex);
    if (k < v.supp.size() && static_cast<long long>(v.supp[k]) + 1 == unitIndex) {
        inside = true;
        return static_cast<long long>(v.supp[k]) - static_cast<long long>(k);
    }
    return unitIndex - static_cast<long long>(k);
}

long long codePointToUtf16Index(proto::ProtoContext* ctx, const proto::ProtoString* str,
                                long long cpIndex) {
    if (!str || cpIndex <= 0) return 0;
    const View& v = viewOf(ctx, str);
    if (v.supp.empty()) return cpIndex;
    // supp[j] - j is the code-point index of the j-th supplementary character;
    // count those before cpIndex.
    std::size_t lo = 0, hi = v.supp.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (static_cast<long long>(v.supp[mid]) - static_cast<long long>(mid) < cpIndex) lo = mid + 1;
        else hi = mid;
    }
    return cpIndex + static_cast<long long>(lo);
}

const proto::ProtoString* utf16Slice(proto::ProtoContext* ctx,
                                     const proto::ProtoString* str,
                                     long long from, long long to) {
    const proto::ProtoString* empty = ctx->fromUTF8String("")->asString(ctx);
    if (!str || to <= from) return empty;
    if (from < 0) from = 0;
    long long len = 0;
    const bool derived = derivedBmpLength(ctx, str, len);
    const View* v = derived ? nullptr : &viewOf(ctx, str);
    if (!derived) len = v->length;
    if (to > len) to = len;
    if (to <= from) return empty;
    if (derived || v->supp.empty()) {
        // Unit indices are code-point indices. The slice's length is known,
        // so a loop such as `s = s.slice(1)` does not scan each new string.
        if (from == 0 && to == len) return str;
        const proto::ProtoString* s = str->getSlice(ctx, static_cast<int>(from), static_cast<int>(to));
        if (!s) return empty;
        if (to - from > kUtf16DirectLimit) {
            LengthInfo info;
            info.length = to - from;
            recordLength(s, info);
        }
        return s;
    }
    // Pairs wholly inside [from, to): supp[j] >= from and supp[j] + 2 <= to.
    const bool resultBmpOnly = suppBefore(*v, to) == suppBefore(*v, from + 1);
    bool fromInside = false, toInside = false;
    long long cpFrom = utf16ToCodePointIndex(ctx, str, from, fromInside);
    const long long cpTo = utf16ToCodePointIndex(ctx, str, to, toInside);
    const proto::ProtoString* head = nullptr;
    if (fromInside) {
        // `from` is the low half of code point cpFrom: it starts the result alone.
        uint16_t low = 0;
        utf16UnitAt(ctx, str, from, low);
        head = stringOfCp(ctx, low)->asString(ctx);
        ++cpFrom;
    }
    const proto::ProtoString* mid = nullptr;
    if (cpTo > cpFrom) {
        if (cpFrom == 0 && cpTo == v->cps) mid = str;
        else mid = str->getSlice(ctx, static_cast<int>(cpFrom), static_cast<int>(cpTo));
    }
    const proto::ProtoString* tail = nullptr;
    if (toInside) {
        // `to` cuts code point cpTo after its high half.
        uint16_t high = 0;
        utf16UnitAt(ctx, str, to - 1, high);
        tail = stringOfCp(ctx, high)->asString(ctx);
    }
    // No seam can pair: a low half starts the result, a high half ends it.
    const proto::ProtoString* r = head;
    if (mid) r = r ? r->appendLast(ctx, mid) : mid;
    if (tail) r = r ? r->appendLast(ctx, tail) : tail;
    if (!r) return empty;
    if (to - from > kUtf16DirectLimit) {
        LengthInfo info;
        info.length = to - from;
        info.bmpOnly = resultBmpOnly;
        recordLength(r, info);
    }
    return r;
}

void utf16Units(proto::ProtoContext* ctx, const proto::ProtoString* str, std::u16string& out) {
    out.clear();
    if (!str) return;
    const View& v = viewOf(ctx, str);
    if (v.materialised) {
        out.assign(v.units.begin(), v.units.end());
        return;
    }
    std::string bytes;
    str->toUTF8String(ctx, bytes);
    appendUTF8AsUtf16(out, bytes.data(), bytes.size());
}

void appendUTF8AsUtf16(std::u16string& out, const char* utf8, std::size_t n) {
    const auto* p = reinterpret_cast<const unsigned char*>(utf8);
    out.reserve(out.size() + n);
    for (std::size_t i = 0; i < n;) {
        uint32_t cp = decodeAt(p, n, i);
        if (cp < 0x10000u) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000u;
            out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        }
    }
}

void appendUtf16AsUTF8(std::string& out, const char16_t* u, std::size_t n) {
    out.reserve(out.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        uint32_t c = u[i];
        if (c < 0x80) { out += static_cast<char>(c); continue; }
        if (isHigh(c) && i + 1 < n && isLow(u[i + 1])) {
            c = 0x10000u + ((c - 0xD800u) << 10) + (static_cast<uint32_t>(u[i + 1]) - 0xDC00u);
            ++i;
        }
        appendCodePointUTF8(out, c);
    }
}

const proto::ProtoObject* jsStringFromUtf16(proto::ProtoContext* ctx,
                                            const char16_t* units, std::size_t n) {
    std::string bytes;
    appendUtf16AsUTF8(bytes, units, n);
    return jsStringFromUTF8(ctx, bytes);
}

const proto::ProtoString* jsConcatStrings(proto::ProtoContext* ctx,
                                          const proto::ProtoString* a,
                                          const proto::ProtoString* b) {
    if (!a) return b;
    if (!b) return a;
    // b's LengthInfo is wanted only when a's is known (the result's length
    // can then be recorded); a short b is decoded once and recorded. A
    // recorded operand needs no getSize. The seam needs the last unit of a
    // and the first unit of b: a known last unit that is not a high
    // surrogate settles it without a getAt.
    Memo& m = t_memo;
    syncMemo(m, ctx);
    LengthInfo la, lb;
    long long na = -1, nb = -1;
    bool knownA, knownB = false;
    if (const LengthInfo* r = derivedLength(m, a)) {
        la = *r;
        knownA = true;
        if (la.length == 0) return b;
    } else {
        na = static_cast<long long>(a->getSize(ctx));
        if (na == 0) return b;
        knownA = knownLength(ctx, a, na, la);
    }
    if (const LengthInfo* r = knownA ? derivedLength(m, b) : nullptr) {
        lb = *r;
        knownB = true;
        if (lb.length == 0) return a;
    } else {
        nb = static_cast<long long>(b->getSize(ctx));
        if (nb == 0) return a;
        if (knownA) knownB = knownLength(ctx, b, nb, lb);
    }
    uint32_t first = 0, last = 0;
    if (!(knownA && la.edges && !isHigh(la.last))) {
        first = (knownB && lb.edges) ? lb.first : cpAt(ctx, b, 0);
        if (isLow(first)) {
            if (knownA && la.edges) {
                last = la.last;
            } else {
                if (na < 0) na = static_cast<long long>(a->getSize(ctx));
                last = cpAt(ctx, a, na - 1);
            }
        }
    }
    const proto::ProtoString* r;
    bool seamPair = false;
    if (isHigh(last) && isLow(first)) {
        // The seam is a surrogate pair: one code point.
        const uint32_t cp = 0x10000u + ((last - 0xD800u) << 10) + (first - 0xDC00u);
        const proto::ProtoString* pair = stringOfCp(ctx, cp)->asString(ctx);
        if (na < 0) na = static_cast<long long>(a->getSize(ctx));
        if (nb < 0) nb = static_cast<long long>(b->getSize(ctx));
        const proto::ProtoString* left = na > 1
            ? a->getSlice(ctx, 0, static_cast<int>(na - 1)) : nullptr;
        const proto::ProtoString* right = nb > 1
            ? b->getSlice(ctx, 1, static_cast<int>(nb)) : nullptr;
        r = left ? left->appendLast(ctx, pair) : pair;
        if (right) r = r->appendLast(ctx, right);
        seamPair = true;
    } else {
        r = a->appendLast(ctx, b);
    }
    // Record the result's length when both operands' lengths are known
    // (UTF-16 lengths add up, even when the seam joins a pair). A loop such as
    // `while (s.length < n) s += x` then reads each new string's length
    // without scanning it, as it did when the length was protoCore's
    // code-point count.
    if (knownA && knownB) {
        LengthInfo info;
        info.length = la.length + lb.length;
        info.bmpOnly = la.bmpOnly && lb.bmpOnly && !seamPair;
        info.edges = la.edges && lb.edges;
        info.first = la.first;
        info.last = lb.last;
        recordLength(r, info);
    }
    return r;
}

int utf16Compare(proto::ProtoContext* ctx, const proto::ProtoString* a,
                 const proto::ProtoString* b) {
    if (a == b) return 0;
    if (!a || !b) return a ? 1 : -1;
    // Code-point order is UTF-16 order unless a supplementary character is
    // involved: answer with protoCore's comparison when neither side has one.
    const long long na = static_cast<long long>(a->getSize(ctx));
    const long long nb = static_cast<long long>(b->getSize(ctx));
    // Short strings take the byte comparison below directly: it is as cheap.
    if (na > kUtf16DirectLimit && nb > kUtf16DirectLimit
        && utf16IsBmpOnly(ctx, a) && utf16IsBmpOnly(ctx, b)) {
        const int c = a->cmp_to_string(ctx, b);
        return c < 0 ? -1 : c > 0 ? 1 : 0;
    }
    std::string sa, sb;
    a->toUTF8String(ctx, sa);
    b->toUTF8String(ctx, sb);
    return utf16CompareUTF8(sa, sb);
}

int utf16CompareUTF8(const std::string& sa, const std::string& sb) {
    const auto* pa = reinterpret_cast<const unsigned char*>(sa.data());
    const auto* pb = reinterpret_cast<const unsigned char*>(sb.data());
    const std::size_t la = sa.size(), lb = sb.size();
    std::size_t i = 0;
    const std::size_t lmin = std::min(la, lb);
    while (i < lmin && pa[i] == pb[i]) ++i;
    if (i == lmin) return la == lb ? 0 : (la < lb ? -1 : 1);
    // Back up to the start of the differing code point (both sides share the
    // bytes before it, so they share its start).
    while (i > 0 && (pa[i] & 0xC0) == 0x80) --i;
    std::size_t ia = i, ib = i;
    const uint32_t ca = decodeAt(pa, la, ia);
    const uint32_t cb = decodeAt(pb, lb, ib);
    const bool supA = ca >= 0x10000u, supB = cb >= 0x10000u;
    if (supA == supB) return ca < cb ? -1 : 1;
    // One side is a pair, the other one BMP unit: compare the units.
    const uint32_t bmp = supA ? cb : ca;
    const uint32_t sup = supA ? ca : cb;
    const uint32_t hi = unitOfCp(sup, 0);
    int r;   // sign of (BMP side - pair side)
    if (bmp != hi) {
        r = bmp < hi ? -1 : 1;
    } else {
        // A lone high surrogate equal to the pair's high half: the next unit
        // of the BMP side meets the pair's low half. In canonical form that
        // unit is not a low surrogate, so the two cannot be equal.
        const unsigned char* p = supA ? pb : pa;
        const std::size_t len = supA ? lb : la;
        std::size_t j = supA ? ib : ia;
        if (j >= len) {
            r = -1;
        } else {
            const uint32_t next = unitOfCp(decodeAt(p, len, j), 0);
            r = next < unitOfCp(sup, 1) ? -1 : 1;
        }
    }
    return supA ? -r : r;
}

}  // namespace protojs
