#include "../ProtoCoreTypes.h"
#include "PropertyDictionary.h"

#include "../HashedCollection.h"
#include "../JSSymbols.h"
#include "../ObjectPrototype.h"
#include "../ProxyBuiltin.h"
#include "../TypedArrayPrototype.h"
#include "ProtoInterpreter.h"

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace protojs {
namespace dict {

// ---- Vocabulary ----------------------------------------------------------------

namespace {

constexpr std::size_t kVocabularyShards = 16;

struct VocabularyShard {
    std::mutex mutex;
    std::unordered_set<std::string> words;
};

std::array<VocabularyShard, kVocabularyShards>& vocabulary() {
    // Never destroyed: noteVocabulary may run from static initialisers and
    // lookups from threads that outlive main's static destruction.
    static auto* shards = new std::array<VocabularyShard, kVocabularyShards>();
    return *shards;
}

VocabularyShard& shardFor(const std::string& s) {
    return vocabulary()[std::hash<std::string>{}(s) % kVocabularyShards];
}

}  // namespace

// ---- Spellings named after they were data -------------------------------------

namespace {

// 2^20 bits (128 KiB), two probes per spelling. It saturates after about a
// million distinct dictionary spellings; past that every spelling named later
// is taken for "named after data" and pays the dictionary probe on its
// lookups, which is slower and still correct.
constexpr std::size_t kFilterWords = (1u << 20) / 64;
std::atomic<std::uint64_t> g_dataSpellings[kFilterWords];

inline void filterProbes(const std::string& s, std::size_t& a, std::size_t& b) {
    const std::uint64_t h = std::hash<std::string>{}(s);
    const std::uint64_t h2 = h * 0x9E3779B97F4A7C15ull;
    a = static_cast<std::size_t>(h & ((1u << 20) - 1));
    b = static_cast<std::size_t>((h2 >> 40) & ((1u << 20) - 1));
}

void noteDataSpelling(const std::string& s) {
    std::size_t a, b;
    filterProbes(s, a, b);
    g_dataSpellings[a / 64].fetch_or(1ull << (a % 64), std::memory_order_seq_cst);
    g_dataSpellings[b / 64].fetch_or(1ull << (b % 64), std::memory_order_seq_cst);
}

bool mayBeDataSpelling(const std::string& s) {
    std::size_t a, b;
    filterProbes(s, a, b);
    return (g_dataSpellings[a / 64].load(std::memory_order_seq_cst) & (1ull << (a % 64)))
        && (g_dataSpellings[b / 64].load(std::memory_order_seq_cst) & (1ull << (b % 64)));
}

struct NamedAfterData {
    std::mutex mutex;
    std::unordered_set<std::string> spellings;
};

NamedAfterData& namedAfterData() {
    static auto* n = new NamedAfterData();  // never destroyed, like the vocabulary
    return *n;
}

// Bumped whenever a spelling is added: the per-thread answers below are kept
// for one generation.
std::atomic<std::uint64_t> g_namedAfterDataGeneration{0};

// A spelling has become a name. When a dictionary may have held it, from now
// on lookups of that name consult the dictionaries first.
void spellingBecameName(const std::string& s) {
    // Pairs with the fence in isDynamicFrom: either that thread sees the
    // spelling as vocabulary and stores the key as a named attribute, or this
    // thread sees its filter bits.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!mayBeDataSpelling(s)) return;
    {
        NamedAfterData& n = namedAfterData();
        std::lock_guard<std::mutex> lock(n.mutex);
        if (!n.spellings.insert(s).second) return;
    }
    g_namedAfterDataGeneration.fetch_add(1, std::memory_order_release);
    detail::g_anyNamedAfterData.store(true, std::memory_order_release);
}

}  // namespace

namespace detail {

std::atomic<bool> g_anyNamedAfterData{false};

bool isNamedAfterData(proto::ProtoContext* ctx, const proto::ProtoString* key) {
    // Interned names are perennial, so their answer can be cached per thread
    // by address until the set grows.
    struct Memo {
        std::uint64_t generation = ~0ull;
        std::unordered_map<const proto::ProtoString*, bool> answers;
    };
    static thread_local Memo memo;
    const bool interned = key->isSymbol();
    if (interned) {
        const std::uint64_t gen = g_namedAfterDataGeneration.load(std::memory_order_acquire);
        if (memo.generation != gen) {
            memo.answers.clear();
            memo.generation = gen;
        }
        auto it = memo.answers.find(key);
        if (it != memo.answers.end()) return it->second;
    } else {
        // An inline string (ASCII of up to six bytes) is never a dictionary
        // key; heap strings were answered by mayBeDictionaryKey.
        return false;
    }
    std::string s;
    key->toUTF8String(ctx, s);
    bool named;
    {
        NamedAfterData& n = namedAfterData();
        std::lock_guard<std::mutex> lock(n.mutex);
        named = n.spellings.count(s) != 0;
    }
    memo.answers[key] = named;
    return named;
}

}  // namespace detail

void noteNamedSpelling(const std::string& utf8) {
    if (utf8.size() <= 6) return;
    spellingBecameName(utf8);
}

void noteVocabulary(const char* utf8, std::size_t len) {
    // Spellings of up to six bytes are inline strings when ASCII and never
    // reach the dictionary; they need no entry.
    if (!utf8 || len <= 6) return;
    std::string s(utf8, len);
    bool added;
    {
        VocabularyShard& shard = shardFor(s);
        std::lock_guard<std::mutex> lock(shard.mutex);
        added = shard.words.insert(s).second;
    }
    if (added) spellingBecameName(s);
}

bool isVocabulary(const std::string& utf8) {
    if (utf8.size() <= 6) return false;
    VocabularyShard& shard = shardFor(utf8);
    std::lock_guard<std::mutex> lock(shard.mutex);
    return shard.words.count(utf8) != 0;
}

// ---- The record --------------------------------------------------------------

namespace {

// An object cell (pointer tag 0): the only kind of value with attributes of its
// own that a dictionary can live in.
inline bool isObjectCell(const proto::ProtoObject* o) {
    return o && o != PROTO_NONE && (reinterpret_cast<std::uintptr_t>(o) & 0x3F) == 0;
}

const proto::ProtoString* dictKey(proto::ProtoContext* ctx) {
    return JSSymbols::propsDict(ctx);
}

// The own record, or nullptr.
const proto::ProtoObject* ownRecord(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || proto::isSmallInt(obj)) return nullptr;
    if (!isObjectCell(obj)) return nullptr;
    const proto::ProtoObject* rec = obj->getOwnAttributeDirect(ctx, dictKey(ctx));
    return (rec && rec != PROTO_NONE) ? rec : nullptr;
}

bool loadRecord(proto::ProtoContext* ctx, const proto::ProtoObject* rec, coll::State& st) {
    const proto::ProtoList* l = rec ? rec->asList(ctx) : nullptr;
    if (!l || l->getSize(ctx) != 4) return false;
    const proto::ProtoObject* e = l->getAt(ctx, 0);
    const proto::ProtoObject* i = l->getAt(ctx, 1);
    const proto::ProtoObject* n = l->getAt(ctx, 2);
    const proto::ProtoObject* s = l->getAt(ctx, 3);
    st.entries = e ? e->asSparseList(ctx) : nullptr;
    st.index = i ? i->asMap(ctx) : nullptr;
    if (!st.entries || !st.index) return false;
    st.next = proto::isSmallInt(n) ? static_cast<proto::proto_ulong>(proto::asSmallInt(n)) : 0;
    st.size = proto::isSmallInt(s) ? static_cast<proto::proto_ulong>(proto::asSmallInt(s)) : 0;
    return true;
}

const proto::ProtoObject* makeRecord(proto::ProtoContext* ctx, const coll::State& st) {
    const proto::ProtoObject* items[4] = {
        st.entries->asObject(ctx), st.index->asObject(ctx),
        proto::makeSmallInt(static_cast<long long>(st.next)),
        proto::makeSmallInt(static_cast<long long>(st.size))};
    return ctx->newList(4, items)->asObject(ctx);
}

// Read-modify-write of obj's record. `change` edits the state and returns
// false to abandon the write (then nullptr is returned). Returns the resulting
// object: obj when it is mutable; a new handle when it is immutable.
const proto::ProtoObject* modify(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const std::function<bool(coll::State&)>& change) {
    const proto::ProtoString* k = dictKey(ctx);
    for (int attempt = 0;; ++attempt) {
        const proto::ProtoObject* rec = ownRecord(ctx, obj);
        coll::State st;
        if (!rec || !loadRecord(ctx, rec, st)) st = coll::emptyState(ctx);
        if (!change(st)) return nullptr;
        const proto::ProtoObject* next = makeRecord(ctx, st);
        if (obj->setAttributeIfEqual(ctx, k, rec, next)) return obj;
        // An immutable receiver (an object literal under construction, a
        // JSON object being built) never swaps; a mutable one failed because
        // another thread published first, so re-read and retry.
        if (ownRecord(ctx, obj) == rec) return obj->setAttribute(ctx, k, next);
        (void)attempt;
    }
}

bool find(proto::ProtoContext* ctx, const coll::State& st, const proto::ProtoString* key,
          proto::proto_ulong& slot) {
    return coll::find(ctx, st, key->asObject(ctx), slot);
}

const proto::ProtoObject* advance(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    const proto::ProtoObject* over = getJSProtoOverride(ctx, o);
    if (over && over != PROTO_NONE && over != getNullSentinel()) return over;
    const proto::ProtoObject* p = o->getPrototype(ctx);
    return (p && p != PROTO_NONE && p != o) ? p : nullptr;
}

}  // namespace

bool hasDictionary(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return ownRecord(ctx, obj) != nullptr;
}

const proto::ProtoObject* getOwn(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const proto::ProtoString* key) {
    if (!key) return nullptr;
    const proto::ProtoObject* rec = ownRecord(ctx, obj);
    if (!rec) return nullptr;
    coll::State st;
    proto::proto_ulong slot = 0;
    if (!loadRecord(ctx, rec, st) || !find(ctx, st, key, slot)) return nullptr;
    if (!st.entries->has(ctx, slot)) return nullptr;
    return coll::pairValue(ctx, st.entries->getAt(ctx, slot));
}

const proto::ProtoObject* findInChain(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                      const proto::ProtoString* key,
                                      const proto::ProtoObject** holder) {
    if (!key || !obj || obj == PROTO_NONE || proto::isSmallInt(obj)) return nullptr;
    const proto::ProtoObject* cur = obj;
    for (int depth = 0; cur && depth < 100; ++depth) {
        if (!isObjectCell(cur)) return nullptr;
        if (cur != obj
            && (cur->hasOwnAttribute(ctx, key) == PROTO_TRUE || isProxy(ctx, cur)))
            return nullptr;
        if (const proto::ProtoObject* v = getOwn(ctx, cur, key)) {
            if (holder) *holder = cur;
            return v;
        }
        cur = advance(ctx, cur);
    }
    return nullptr;
}

const proto::ProtoObject* getDataProperty(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                          const proto::ProtoString* key) {
    if (!obj || !key || obj == PROTO_NONE) return PROTO_NONE;
    // A key is in at most one place, so an own dictionary hit is the answer;
    // probing the dictionary first spares a by-content lookup of a heap-string
    // name that is not interned.
    // A key a dictionary may hold below a prototype that names it: the
    // nearest dictionary entry, else the named lookup decides.
    if (mayBeDictionaryKey(ctx, key)) {
        if (const proto::ProtoObject* dv = findInChain(ctx, obj, key)) return dv;
        const proto::ProtoObject* val = obj->getAttribute(ctx, key, true);
        return val ? val : PROTO_NONE;
    }
    if (ownRecord(ctx, obj)) {
        if (const proto::ProtoObject* dv = getOwn(ctx, obj, key)) return dv;
    }
    const proto::ProtoObject* val = obj->getAttribute(ctx, key, true);
    if (!val || val == PROTO_NONE) {
        if (const proto::ProtoObject* dv = findInChain(ctx, obj, key)) return dv;
    }
    return val ? val : PROTO_NONE;
}

std::size_t size(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    const proto::ProtoObject* rec = ownRecord(ctx, obj);
    coll::State st;
    if (!rec || !loadRecord(ctx, rec, st)) return 0;
    return static_cast<std::size_t>(st.size);
}

void entries(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
             std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>>& out) {
    const proto::ProtoObject* rec = ownRecord(ctx, obj);
    coll::State st;
    if (!rec || !loadRecord(ctx, rec, st) || st.size == 0) return;
    struct Collect {
        std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>>* out;
    } c{&out};
    // The entries list is keyed by insertion slot: ascending order is
    // insertion order.
    st.entries->processElements(ctx, &c,
        [](proto::ProtoContext* cctx, void* self, proto::proto_ulong, const proto::ProtoObject* pair) {
            auto* cs = static_cast<Collect*>(self);
            const proto::ProtoObject* k = coll::pairKey(cctx, pair);
            const proto::ProtoString* ks = (k && k != PROTO_NONE) ? k->asString(cctx) : nullptr;
            if (ks) cs->out->emplace_back(ks, coll::pairValue(cctx, pair));
        });
}

void keys(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::vector<std::string>& out) {
    std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>> es;
    entries(ctx, obj, es);
    for (const auto& e : es) {
        std::string s;
        e.first->toUTF8String(ctx, s);
        out.push_back(std::move(s));
    }
}

// ---- Writes ------------------------------------------------------------------

bool acceptsNewKeys(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || proto::isSmallInt(obj) || !isObjectCell(obj))
        return false;
    if (obj == getNullSentinel() || obj == getUndefinedSentinel()) return false;
    if (obj->isMethod(ctx)) return false;
    // Exotic or special objects keep every key as a named attribute: their
    // own paths (array length, typed-array indices, proxy traps, string
    // indices, global bindings, function properties) read attributes.
    static const proto::ProtoString* const globalThisKey =
        proto::ProtoString::createSymbol(ctx, "globalThis");
    static const proto::ProtoString* const moduleScopedKey =
        proto::ProtoString::createSymbol(ctx, "__module_scoped__");
    const proto::ProtoString* exotic[] = {
        JSSymbols::isArray(ctx), JSSymbols::bytecodeId(ctx), JSSymbols::nativeFn(ctx),
        JSSymbols::primitiveValue(ctx), globalThisKey, moduleScopedKey};
    for (const proto::ProtoString* k : exotic) {
        if (k && obj->getOwnAttributeDirect(ctx, k)) return false;
    }
    if (isProxy(ctx, obj) || isTypedArrayInstance(ctx, obj)) return false;
    if (jsIntegrityBits(ctx, obj) != 0) return false;
    return true;
}

namespace {

// The checks of wantsNewKey that do not depend on the receiver itself: the
// spelling, and the named attributes of `from` and its prototypes.
bool isDynamicFrom(proto::ProtoContext* ctx, const proto::ProtoObject* from,
                   const proto::ProtoString* key) {
    if (!isHeapStringKey(key)) return false;
    // A named attribute of the object or of a prototype keeps the key named:
    // a read finds it by content, and an inherited one must keep its
    // accessor or read-only semantics. hasOwnAttribute looks a heap-string
    // name up without interning it.
    for (const proto::ProtoObject* cur = from; cur; cur = advance(ctx, cur)) {
        if (!isObjectCell(cur)) break;
        if (cur->hasOwnAttribute(ctx, key) == PROTO_TRUE) return false;
    }
    std::string s;
    key->toUTF8String(ctx, s);
    // Internal spellings (__x__, @@sym#n) and program vocabulary stay named.
    if (s.size() >= 4 && s[0] == '_' && s[1] == '_' && s[s.size() - 1] == '_'
        && s[s.size() - 2] == '_')
        return false;
    if (s.compare(0, 2, "@@") == 0 || s.compare(0, 7, "Symbol.") == 0
        || s.compare(0, 7, "Symbol(") == 0)
        return false;
    // Record the spelling before reading the vocabulary; spellingBecameName
    // does the converse, so a spelling named concurrently is either seen here
    // as vocabulary or seen there as data.
    noteDataSpelling(s);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return !isVocabulary(s);
}

}  // namespace

bool wantsNewKey(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                 const proto::ProtoString* key) {
    return isDynamicFrom(ctx, obj, key) && acceptsNewKeys(ctx, obj);
}

bool isDynamicKeyFor(proto::ProtoContext* ctx, const proto::ProtoObject* proto,
                     const proto::ProtoString* key) {
    return isDynamicFrom(ctx, proto, key);
}

void Builder::add(const proto::ProtoString* key, const proto::ProtoObject* value) {
    if (!key) return;
    if (!started_) {
        entries_ = ctx_->newSparseList();
        index_ = ctx_->newMap();
        started_ = true;
    }
    coll::State st;
    st.entries = entries_;
    st.index = index_;
    st.next = next_;
    st.size = next_;
    coll::insert(ctx_, st, key->asObject(ctx_),
                 coll::makePair(ctx_, key->asObject(ctx_), value ? value : PROTO_NONE));
    entries_ = st.entries;
    index_ = st.index;
    next_ = st.next;
}

const proto::ProtoObject* Builder::finish(const proto::ProtoObject* obj) {
    if (!started_ || !obj) return obj;
    coll::State st;
    st.entries = entries_;
    st.index = index_;
    st.next = next_;
    st.size = next_;
    return obj->setAttribute(ctx_, dictKey(ctx_), makeRecord(ctx_, st));
}

const proto::ProtoObject* update(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const proto::ProtoString* key, const proto::ProtoObject* val) {
    if (!key || !ownRecord(ctx, obj)) return nullptr;
    return modify(ctx, obj, [&](coll::State& st) {
        proto::proto_ulong slot = 0;
        if (!find(ctx, st, key, slot)) return false;
        const proto::ProtoObject* stored = coll::pairKey(ctx, st.entries->getAt(ctx, slot));
        coll::replace(ctx, st, slot, coll::makePair(ctx, stored, val ? val : PROTO_NONE));
        return true;
    });
}

const proto::ProtoObject* put(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                              const proto::ProtoString* key, const proto::ProtoObject* val) {
    if (!key || !obj) return nullptr;
    if (ownRecord(ctx, obj) && hasOwn(ctx, obj, key)) {
        if (jsIsFrozen(ctx, obj)) return obj;
        if (const proto::ProtoObject* r = update(ctx, obj, key, val)) return r;
    }
    if (!wantsNewKey(ctx, obj, key)) return nullptr;
    // A heap string is stored as it is: it is a GC-managed cell that the
    // entry keeps alive, and the index compares it by content.
    return modify(ctx, obj, [&](coll::State& st) {
        proto::proto_ulong slot = 0;
        if (find(ctx, st, key, slot)) {
            const proto::ProtoObject* stored = coll::pairKey(ctx, st.entries->getAt(ctx, slot));
            coll::replace(ctx, st, slot, coll::makePair(ctx, stored, val ? val : PROTO_NONE));
        } else {
            coll::insert(ctx, st, key->asObject(ctx),
                         coll::makePair(ctx, key->asObject(ctx), val ? val : PROTO_NONE));
        }
        return true;
    });
}

const proto::ProtoObject* createDataProperty(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                             const proto::ProtoString* key,
                                             const proto::ProtoObject* val) {
    if (const proto::ProtoObject* r = put(ctx, obj, key, val)) return r;
    return obj->setAttribute(ctx, key, val ? val : PROTO_NONE);
}

const proto::ProtoObject* remove(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const proto::ProtoString* key) {
    if (!key || !ownRecord(ctx, obj)) return nullptr;
    return modify(ctx, obj, [&](coll::State& st) {
        proto::proto_ulong slot = 0;
        if (!find(ctx, st, key, slot)) return false;
        const proto::ProtoObject* stored = coll::pairKey(ctx, st.entries->getAt(ctx, slot));
        coll::remove(ctx, st, stored, slot);
        return true;
    });
}

const proto::ProtoObject* evict(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                const proto::ProtoString* key) {
    const proto::ProtoObject* v = getOwn(ctx, obj, key);
    if (!v) return obj;
    const proto::ProtoObject* r = remove(ctx, obj, key);
    if (r) obj = r;
    {
        // The spelling is now a named attribute: objects below `obj` that
        // hold it in their dictionaries must still shadow it.
        std::string ks;
        key->toUTF8String(ctx, ks);
        noteNamedSpelling(ks);
    }
    obj = obj->setAttribute(ctx, key, v);
    // A sealed or frozen object's keys are not configurable (and, frozen,
    // not writable): a named attribute records that in its __pd_<key>__
    // sidecar, which the dictionary entry did not need.
    const long long bits = jsIntegrityBits(ctx, obj);
    if (bits & (kIntegritySealed | kIntegrityFrozen)) {
        std::string ks;
        key->toUTF8String(ctx, ks);
        const std::string pd = "__pd_" + ks + "__";
        const proto::ProtoString* pdk = proto::ProtoString::createSymbol(ctx, pd.c_str());
        const bool frozen = (bits & kIntegrityFrozen) != 0;
        obj = obj->setAttribute(ctx, pdk, ctx->fromInteger(frozen ? 0x4LL : 0x5LL));
        if (frozen) obj = obj->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);
    }
    return obj;
}

// ---- Keys ------------------------------------------------------------------------

const proto::ProtoString* indexKeyNoIntern(proto::ProtoContext* ctx, std::uint32_t i) {
    if (i < 1000000u) return JSSymbols::indexKey(ctx, i);
    const proto::ProtoObject* o = ctx->fromUTF8String(std::to_string(i).c_str());
    return o ? o->asString(ctx) : nullptr;
}

}  // namespace dict
}  // namespace protojs
