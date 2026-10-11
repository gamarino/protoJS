#pragma once

// PropertyDictionary -- the dictionary mode of JavaScript objects.
//
// protoCore keeps an object's attributes in a tree keyed by the interned symbol
// of each name: ProtoObject::setAttribute interns every heap-string key in the
// process-wide SymbolTable, whose entries are never collected. That is bounded
// by the program's vocabulary for identifiers, and unbounded for data used as
// keys -- request ids, UUIDs, words, the keys of every JSON object a server
// parses (docs/MEMORY_SAFETY_PROPOSALS.md, section 2, option (b), approved
// 2026-10-11). protoCore is not changed: protoJS keeps such keys out of the
// attribute tree.
//
// An object enters dictionary mode at its first DYNAMIC key: a key that is a
// heap string (neither an interned symbol nor an inline string -- ASCII strings
// of up to six bytes live in the pointer and cost nothing), is not program
// vocabulary (an identifier or literal of the loaded code, a protoJS built-in
// name), and is not already an attribute of the object or of its prototypes.
// Such keys are stored in a hidden ordinary-attribute record holding a
// ProtoMap index keyed by string content (protoCore's hashed-map helpers with
// the SameValueZero KeySemantics that Map and Set use, src/HashedCollection.h)
// and the entries in insertion order. The keys are ordinary GC-managed
// strings: they die with the object. Vocabulary keys stay named attributes, so
// objects with identifier keys keep the attribute fast path. The threshold is
// N = 0 dynamic keys; docs/MEMORY_SAFETY_PROPOSALS.md records the
// measurements that chose it.
//
// Invariants:
//   - A key is in at most one place on an object: its named attributes or its
//     dictionary. Writes keep it so (a named write to a dictionary object
//     updates the dictionary entry when it holds the key).
//   - Dictionary entries are plain data properties: writable, enumerable and
//     configurable, except as the object's integrity level says (frozen:
//     not writable, not configurable; sealed: not configurable). Giving a key
//     an accessor or a non-default descriptor moves it to the named
//     attributes first (evict), which interns it; that is an explicit act on
//     one key, not data flowing through a loop.
//   - Only ordinary extensible objects get new dictionary entries: not arrays,
//     typed arrays, functions, proxies, String wrappers or the global object.
//
// Order: the named string keys (protoCore's attribute order, the documented
// deviation), then the dictionary keys in insertion order; when the object has
// dictionary keys, every integer index among them all comes first, ascending.
//
// Concurrency: every change publishes one new record with
// ProtoObject::setAttributeIfEqual and retries on a lost race, so concurrent
// writers on several Deferred threads never lose a key (each write is atomic,
// as a named attribute write is).

#include <protoCore.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace protojs {
namespace dict {

/** True when `key` is a heap string: neither an interned symbol nor an inline
 *  string. Only such a key costs a perennial SymbolTable entry when it becomes
 *  an attribute name. */
inline bool isHeapStringKey(const proto::ProtoString* key) {
    return key && (reinterpret_cast<std::uintptr_t>(key) & 0x3F) == 6;
}

// ---- Program vocabulary ------------------------------------------------------
//
// The spellings the loaded code names (bytecode atoms, constant-pool strings)
// and protoJS's own symbol names. They are interned anyway, so storing them as
// named attributes costs nothing new, and code reads them by name. Process-wide
// and thread-safe; bounded by the program text.

/** Record `utf8` as vocabulary. */
void noteVocabulary(const char* utf8, std::size_t len);
inline void noteVocabulary(const std::string& s) { noteVocabulary(s.data(), s.size()); }

/** True when `utf8` was recorded as vocabulary. */
bool isVocabulary(const std::string& utf8);

// ---- Dictionary access (own properties) --------------------------------------

/** True when `obj` holds a dictionary (it may be empty). */
bool hasDictionary(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** The value of own dictionary key `key` (compared by content), or nullptr. */
const proto::ProtoObject* getOwn(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const proto::ProtoString* key);

/** True when `key` is an own dictionary key of `obj`. */
inline bool hasOwn(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                   const proto::ProtoString* key) {
    return getOwn(ctx, obj, key) != nullptr;
}

/** The value of `key` in the dictionary of `obj` or of the first object on its
 *  prototype chain (JS overrides included) whose dictionary holds it; nullptr
 *  when none does. Stops at an object that has `key` as a named attribute.
 *  `holder`, when given, receives the object that held it. */
const proto::ProtoObject* findInChain(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                      const proto::ProtoString* key,
                                      const proto::ProtoObject** holder = nullptr);

/** Number of own dictionary keys. */
std::size_t size(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** The own dictionary entries in insertion order. */
void entries(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
             std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>>& out);

/** The own dictionary keys in insertion order, as UTF-8. */
void keys(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::vector<std::string>& out);

/** [[Get]] of a data property: an own dictionary key of `obj` shadows the
 *  prototype chain; otherwise protoCore's chain lookup, then the dictionaries
 *  of the prototypes. Accessors are the caller's concern. PROTO_NONE when
 *  absent. */
const proto::ProtoObject* getDataProperty(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                          const proto::ProtoString* key);

// ---- Writes ------------------------------------------------------------------

/** Store a plain data property `key` = `val` in the dictionary when it belongs
 *  there: `obj` already holds `key` in its dictionary (the value is replaced),
 *  or wantsNewKey(obj, key). Returns the resulting object -- `obj` itself
 *  unless `obj` is immutable (an object under construction), in which case the
 *  caller must use the returned handle, as with setAttribute -- or nullptr when
 *  the dictionary does not take the key and the caller must store it as a
 *  named attribute. The caller has already applied the parts of [[Set]] that
 *  concern accessors and read-only inherited properties. A frozen `obj` keeps
 *  its value (the call returns `obj` unchanged); the caller throws in strict
 *  code, as for a named property. */
const proto::ProtoObject* put(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                              const proto::ProtoString* key, const proto::ProtoObject* val);

/** CreateDataProperty on an ordinary object: put() when the dictionary holds
 *  or takes `key`, else a named attribute. Returns the resulting object. */
const proto::ProtoObject* createDataProperty(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                             const proto::ProtoString* key,
                                             const proto::ProtoObject* val);

/** Replace the value of an existing own dictionary key. nullptr when `obj` does
 *  not hold `key` in its dictionary. */
const proto::ProtoObject* update(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const proto::ProtoString* key, const proto::ProtoObject* val);

/** Remove own dictionary key `key`. Returns the resulting object, or nullptr
 *  when the dictionary does not hold it. */
const proto::ProtoObject* remove(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                 const proto::ProtoString* key);

/** Move `key` from the dictionary to the named attributes (interning it), for
 *  a caller about to give it an accessor or a non-default descriptor. Returns
 *  the resulting object (`obj` when the dictionary does not hold `key`). */
const proto::ProtoObject* evict(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                const proto::ProtoString* key);

/** True when `obj` may take NEW dictionary keys (an ordinary extensible
 *  object). */
bool acceptsNewKeys(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** True when a write of `key` to `obj` should create a dictionary entry: `key`
 *  is a dynamic key and `obj` accepts new keys, and neither `obj` nor its
 *  prototype chain has `key` as a named attribute. */
bool wantsNewKey(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                 const proto::ProtoString* key);

/** True when `key` would be a dictionary key of a fresh ordinary object whose
 *  prototype is `proto`: a heap string that is neither internal, vocabulary,
 *  nor a named attribute of `proto` or its prototypes. */
bool isDynamicKeyFor(proto::ProtoContext* ctx, const proto::ProtoObject* proto,
                     const proto::ProtoString* key);

/** Builds the dictionary of an object under construction (JSON.parse) with one
 *  publication: add() each key -- distinct, and dynamic per isDynamicKeyFor --
 *  then finish(). */
class Builder {
public:
    explicit Builder(proto::ProtoContext* ctx) : ctx_(ctx) {}
    void add(const proto::ProtoString* key, const proto::ProtoObject* value);
    bool empty() const { return !started_; }
    /** Installs the dictionary on `obj` and returns the resulting object. */
    const proto::ProtoObject* finish(const proto::ProtoObject* obj);

private:
    proto::ProtoContext* ctx_;
    bool started_ = false;
    const proto::ProtoSparseList* entries_ = nullptr;
    const proto::ProtoMap* index_ = nullptr;
    proto::proto_ulong next_ = 0;
};

// ---- Keys --------------------------------------------------------------------

/** The property key of integer index `i` without interning: the cached symbol
 *  below 4096, the inline string below 1,000,000 (six digits), else a heap
 *  string (seven or more digits would be a perennial symbol). */
const proto::ProtoString* indexKeyNoIntern(proto::ProtoContext* ctx, std::uint32_t i);

}  // namespace dict
}  // namespace protojs
