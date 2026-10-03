#include "HashedCollection.h"
#include "JSSymbols.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace protojs {
namespace coll {

namespace {

// BigInt values are wrapper objects carrying the integer under a hidden
// key; SameValueZero compares them by value.
const proto::ProtoObject* bigIntValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || proto::isSmallInt(v) || v->isString(ctx) || v->isDouble(ctx))
        return nullptr;
    const proto::ProtoString* isK = JSSymbols::isBigInt(ctx);
    if (!isK || v->getAttribute(ctx, isK, false) != PROTO_TRUE) return nullptr;
    const proto::ProtoString* vk = JSSymbols::bigIntValue(ctx);
    return vk ? v->getAttribute(ctx, vk, false) : nullptr;
}

bool numberValue(proto::ProtoContext* ctx, const proto::ProtoObject* v, double& out) {
    if (proto::isSmallInt(v)) { out = static_cast<double>(proto::asSmallInt(v)); return true; }
    if (!v || v == PROTO_NONE) return false;
    if (v->isDouble(ctx) || v->isInteger(ctx)) { out = v->asDouble(ctx); return true; }
    return false;
}

proto::proto_ulong mix(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<proto::proto_ulong>(x);
}

proto::proto_ulong numberHash(double d) {
    if (std::isnan(d)) return mix(0x7ff8000000000000ULL);
    if (d == 0.0) d = 0.0;  // -0 and +0 hash alike
    // An integral value hashes as an integer, whichever representation holds it.
    if (d >= -9007199254740991.0 && d <= 9007199254740991.0 && d == std::floor(d))
        return mix(static_cast<uint64_t>(static_cast<int64_t>(d)) ^ 0x9e3779b97f4a7c15ULL);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    return mix(bits);
}

bool isIdentityKey(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    if (!key || key == PROTO_NONE) return true;
    if (key->isString(ctx) || key->isDouble(ctx) || key->isInteger(ctx)) return false;
    return bigIntValue(ctx, key) == nullptr;
}

proto::proto_ulong keyHash(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    double d;
    if (numberValue(ctx, key, d)) return numberHash(d);
    if (key && key != PROTO_NONE && key->isString(ctx)) {
        const proto::ProtoString* s = key->asString(ctx);
        return s ? s->getHash(ctx) : 0;
    }
    if (const proto::ProtoObject* big = bigIntValue(ctx, key)) {
        if (proto::isSmallInt(big)) return mix(static_cast<uint64_t>(proto::asSmallInt(big)) ^ 0xb16b16b16ULL);
        return big->getHash(ctx) ^ 0xb16b16b16ULL;
    }
    return mix(reinterpret_cast<uintptr_t>(key));
}

bool keyEquals(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b) {
    return sameValueZero(ctx, a, b);
}


}  // namespace

bool sameValueZero(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                   const proto::ProtoObject* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    double da, db;
    if (numberValue(ctx, a, da) && numberValue(ctx, b, db)) {
        if (std::isnan(da) && std::isnan(db)) return true;
        return da == db;
    }
    if (a->isString(ctx) && b->isString(ctx)) return a->compare(ctx, b) == 0;
    const proto::ProtoObject* ba = bigIntValue(ctx, a);
    const proto::ProtoObject* bb = ba ? bigIntValue(ctx, b) : nullptr;
    if (ba && bb) return ba->compare(ctx, bb) == 0;
    return false;
}

const proto::ProtoObject* normalizeKey(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    if (key && !proto::isSmallInt(key) && key != PROTO_NONE && key->isDouble(ctx)) {
        const double d = key->asDouble(ctx);
        if (d == 0.0 && std::signbit(d)) return proto::makeSmallInt(0);
    }
    return key;
}

const proto::KeySemantics& jsKeySemantics() {
    static const proto::KeySemantics semantics{&isIdentityKey, &keyHash, &keyEquals};
    return semantics;
}

State emptyState(proto::ProtoContext* ctx) {
    State st;
    st.entries = ctx->newSparseList();
    st.index = ctx->newMap();
    return st;
}

bool load(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
          const proto::ProtoString* slot, State& out) {
    if (!obj || obj == PROTO_NONE || !slot || proto::isSmallInt(obj)) return false;
    const proto::ProtoObject* rec = obj->getAttribute(ctx, slot, false);
    if (!rec || rec == PROTO_NONE) return false;
    const proto::ProtoList* l = rec->asList(ctx);
    if (!l || l->getSize(ctx) != 4) return false;
    const proto::ProtoObject* e = l->getAt(ctx, 0);
    const proto::ProtoObject* i = l->getAt(ctx, 1);
    const proto::ProtoObject* n = l->getAt(ctx, 2);
    const proto::ProtoObject* s = l->getAt(ctx, 3);
    out.entries = e ? e->asSparseList(ctx) : nullptr;
    out.index = i ? i->asMap(ctx) : nullptr;
    if (!out.entries || !out.index) return false;
    out.next = proto::isSmallInt(n) ? static_cast<proto::proto_ulong>(proto::asSmallInt(n)) : 0;
    out.size = proto::isSmallInt(s) ? static_cast<proto::proto_ulong>(proto::asSmallInt(s)) : 0;
    return true;
}

void store(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
           const proto::ProtoString* slot, const State& st) {
    if (!obj || !slot) return;
    // One cell: newList(n, items) builds a list of up to five in place.
    const proto::ProtoObject* items[4] = {
        st.entries->asObject(ctx), st.index->asObject(ctx),
        proto::makeSmallInt(static_cast<long long>(st.next)),
        proto::makeSmallInt(static_cast<long long>(st.size))};
    obj->setAttribute(ctx, slot, ctx->newList(4, items)->asObject(ctx));
}

bool find(proto::ProtoContext* ctx, const State& st, const proto::ProtoObject* key,
          proto::proto_ulong& slot) {
    if (!st.index || st.size == 0) return false;
    const proto::ProtoObject* s = proto::hashedGet(ctx, st.index, jsKeySemantics(),
                                                   key ? key : PROTO_NONE);
    if (!s || !proto::isSmallInt(s)) return false;
    slot = static_cast<proto::proto_ulong>(proto::asSmallInt(s));
    return true;
}

void insert(proto::ProtoContext* ctx, State& st, const proto::ProtoObject* key,
            const proto::ProtoObject* entry) {
    const proto::proto_ulong slot = st.next++;
    st.entries = st.entries->setAt(ctx, slot, entry ? entry : PROTO_NONE);
    st.index = proto::hashedPut(ctx, st.index, jsKeySemantics(), key ? key : PROTO_NONE,
                                proto::makeSmallInt(static_cast<long long>(slot)));
    st.size++;
}

void replace(proto::ProtoContext* ctx, State& st, proto::proto_ulong slot,
             const proto::ProtoObject* entry) {
    st.entries = st.entries->setAt(ctx, slot, entry ? entry : PROTO_NONE);
}

void remove(proto::ProtoContext* ctx, State& st, const proto::ProtoObject* key,
            proto::proto_ulong slot) {
    st.entries = st.entries->removeAt(ctx, slot);
    st.index = proto::hashedRemove(ctx, st.index, jsKeySemantics(), key ? key : PROTO_NONE);
    if (st.size > 0) st.size--;
}

bool nextUsedSlot(proto::ProtoContext* ctx, const State& st, proto::proto_ulong from,
                  proto::proto_ulong& slot) {
    if (st.size == 0) return false;
    for (proto::proto_ulong s = from; s < st.next; ++s) {
        if (st.entries->has(ctx, s)) { slot = s; return true; }
    }
    return false;
}

const proto::ProtoObject* makePair(proto::ProtoContext* ctx, const proto::ProtoObject* key,
                                   const proto::ProtoObject* value) {
    const proto::ProtoObject* items[2] = {key ? key : PROTO_NONE, value ? value : PROTO_NONE};
    return ctx->newList(2, items)->asObject(ctx);
}

const proto::ProtoObject* pairKey(proto::ProtoContext* ctx, const proto::ProtoObject* pair) {
    const proto::ProtoList* l = pair ? pair->asList(ctx) : nullptr;
    const proto::ProtoObject* k = l ? l->getAt(ctx, 0) : nullptr;
    return k ? k : PROTO_NONE;
}

const proto::ProtoObject* pairValue(proto::ProtoContext* ctx, const proto::ProtoObject* pair) {
    const proto::ProtoList* l = pair ? pair->asList(ctx) : nullptr;
    const proto::ProtoObject* v = l ? l->getAt(ctx, 1) : nullptr;
    return v ? v : PROTO_NONE;
}

}  // namespace coll
}  // namespace protojs
