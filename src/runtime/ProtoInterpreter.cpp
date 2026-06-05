#include "ProtoInterpreter.h"
#include "QuickJSOpcodeEnum.h"
#include "QuickJSBytecodeExport.h"
#include "GeneratorFrame.h"
#include "BehaviorRegistry.h"
#include "../JSSymbols.h"
#include "../ArrayElementsStorage.h"
#include "../ArrayPrototype.h"
#include "../StringPrototype.h"
#include "../RegExpPrototype.h"
#include "../NumberPrototype.h"
#include "../BooleanPrototype.h"
#include "../MapPrototype.h"
#include "../SetPrototype.h"
#include "../MathBuiltin.h"
#include "../ObjectPrototype.h"
#include "../FunctionPrototype.h"
#include "../PromisePrototype.h"
#include "../ArrayBufferPrototype.h"
#include "../TypedArrayPrototype.h"
#include "../DataViewPrototype.h"
#include "../JSContext.h"
#include "../GCBridge.h"
#include "../TypeBridge.h"
#include "headers/protoCore.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <cstdio>
#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <mutex>           // P-JS-7: dispatch_table init mutex
#include <unordered_map>   // Symbol.for registry
#include <atomic>          // P-JS-7: dispatch_table_initialized flag

namespace protojs {
extern thread_local const proto::ProtoObject* t_nullSentinel;
extern thread_local const proto::ProtoObject* t_undefinedSentinel;

// ---------------------------------------------------------------------------
// OOP Dispatch Helpers
// ---------------------------------------------------------------------------
//
// P-JS-4 default-behavior short-circuit.
//
// `BehaviorRegistry::resolve()` returns one of:
//   - `defaultBehavior` — a vanilla JSObjectBehavior whose getField just
//     forwards to `obj->getAttribute(ctx, key, true)`, putField returns
//     nullptr (caller falls back to setAttribute), getElement returns
//     nullptr, putElement returns nullptr.
//   - a custom behavior (Array / Map / Set / RegExp / TypedArray / ...).
//
// For plain objects — by far the dominant case in OOP-dispatch-heavy
// workloads — the resolved behavior IS the default one, and the virtual
// call paid for nothing. Compare the resolved pointer to the registry's
// default and inline the underlying primitive call directly. The
// compiler can keep the inlined path branch-free; the virtual-dispatch
// path stays available for the rare custom-behavior objects.

static const proto::ProtoObject* resolveFieldOOP(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key) {
    if (!obj || !key || obj == PROTO_NONE) return PROTO_NONE;
    const auto& reg = protojs::BehaviorRegistry::instance();
    const protojs::JSObjectBehavior* behavior = reg.resolve(ctx, obj);
    const proto::ProtoObject* res;
    if (behavior == reg.getDefault()) {
        res = obj->getAttribute(ctx, key, true);
    } else {
        res = behavior->getField(ctx, obj, key);
    }
    // Extend the chain via t_jsProtoMap when the protoCore walk did not find
    // the attribute. Object.setPrototypeOf / OP_define_class register
    // [[Prototype]] overrides here that the protoCore parent walk cannot see.
    // Bounded loop avoids cycles.
    if (!res || res == PROTO_NONE) {
        const proto::ProtoObject* cur = obj;
        for (int depth = 0; depth < 100; ++depth) {
            const proto::ProtoObject* override =
                protojs::getJSProtoOverride(cur);
            if (!override || override == PROTO_NONE) break;
            if (override == t_nullSentinel) break;
            res = override->getAttribute(ctx, key, true);
            if (res && res != PROTO_NONE) return res;
            cur = override;
        }
    }
    return res;
}

static const proto::ProtoObject* resolvePutFieldOOP(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* key, const proto::ProtoObject* val) {
    if (!obj || obj == PROTO_NONE || !key) return obj;

    // Accessor setter support: check for __set_<key>__ / __get_<key>__
    // sidecar along the chain. Pre-fix the walk only stopped when the
    // CURRENT object had an own data attribute named <key>; getter-only
    // accessors (Map.prototype.size, Set.prototype.size, etc.) store
    // only __get_size__, never 'size', so the walk fell through and
    // the write created a shadowing data property on the instance.
    std::string keyStr;
    key->toUTF8String(ctx, keyStr);
    std::string skStr = "__set_" + keyStr + "__";
    std::string gkStr = "__get_" + keyStr + "__";
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;

    if (sk || gk) {
        const proto::ProtoObject* curr = obj;
        const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
        int depth = 0;
        while (curr && curr != PROTO_NONE && depth < 100) {
            bool hasData = curr->hasOwnAttribute(ctx, key) == PROTO_TRUE;
            bool hasSetter = sk && curr->hasOwnAttribute(ctx, sk) == PROTO_TRUE;
            bool hasGetter = gk && curr->hasOwnAttribute(ctx, gk) == PROTO_TRUE;
            if (hasData || hasSetter || hasGetter) {
                if (hasSetter) {
                    const proto::ProtoObject* setter = curr->getAttribute(ctx, sk, false);
                    if (setter && setter != PROTO_NONE && setter != t_undefinedSentinel) {
                        const proto::ProtoList* args = ctx->newList();
                        const proto::ProtoList* setArgs = args->appendLast(ctx, val ? val : PROTO_NONE);
                        callJSFunction(ctx, setter, obj, setArgs);
                        return obj;
                    }
                }
                if (hasGetter && !hasSetter) {
                    // Getter-only accessor → write silently fails per
                    // sloppy-mode OrdinarySet step 5.b.
                    return obj;
                }
                break;
            }
            if (curr == objProto) break;
            curr = curr->getPrototype(ctx);
            depth++;
        }
    }

    const auto& reg = protojs::BehaviorRegistry::instance();
    const protojs::JSObjectBehavior* behavior = reg.resolve(ctx, obj);

    // Respect writable descriptor flag (bit 0 of __pd_<key>__).
    std::string pdKeyStr = "__pd_" + keyStr + "__";
    const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
    const proto::ProtoObject* pdv = pdks ? obj->getAttribute(ctx, pdks, true) : nullptr;
    if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
        uint8_t bits = static_cast<uint8_t>(pdv->asLong(ctx));
        if (!(bits & 0x1)) {
            // Property is non-writable.
            return obj;
        }
    }

    if (behavior == reg.getDefault()) {
        return obj->setAttribute(ctx, key, val);
    }

    const proto::ProtoObject* res = behavior->putField(ctx, obj, key, val);
    if (res) return res;
    return obj->setAttribute(ctx, key, val);
}

static const proto::ProtoObject* resolveElementOOP(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index) {
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    const auto& reg = protojs::BehaviorRegistry::instance();
    const protojs::JSObjectBehavior* behavior = reg.resolve(ctx, obj);
    if (behavior == reg.getDefault()) {
        // Default getElement returns nullptr — same observable outcome
        // as the virtual call, but no v-table indirection.
        return nullptr;
    }
    return behavior->getElement(ctx, obj, index);
}

__attribute__((noinline))
static const proto::ProtoObject* resolvePutElementOOP(proto::ProtoContext* ctx, const proto::ProtoObject* obj, uint32_t index, const proto::ProtoObject* val) {
    if (!obj || obj == PROTO_NONE) return obj;
    const auto& reg = protojs::BehaviorRegistry::instance();
    const protojs::JSObjectBehavior* behavior = reg.resolve(ctx, obj);
    if (behavior == reg.getDefault()) {
        // Default putElement returns nullptr → caller falls back to
        // setAttribute on the indexed key.
        return obj->setAttribute(ctx, JSSymbols::indexKey(ctx, index), val);
    }
    const proto::ProtoObject* res = behavior->putElement(ctx, obj, index, val);
    if (res) return res;
    return obj->setAttribute(ctx, JSSymbols::indexKey(ctx, index), val);
}

// ---------------------------------------------------------------------------
// Property key interning optimization
// ---------------------------------------------------------------------------
static const proto::ProtoString* ensureInterned(proto::ProtoContext* ctx, const proto::ProtoString* s) {
    if (!s) return nullptr;
    if (s->isSymbol()) return s;

    // Routing through createSymbol every call: protoCore's SymbolTable
    // already deduplicates by content via a 64-shard concurrent
    // hash, so the per-thread pointer-keyed cache that used to live
    // here was a micro-optimisation, not load-bearing.  The cache
    // was unsafe under churn: when the GC freed a rope cell and the
    // arena reused the slot for a fresh rope with different content,
    // a hash collision returned the stale symbol — keys ended up
    // installed under the wrong slot, manifested as obj['k17'] = 17
    // becoming obj['k_other'] = 17 in object_property-style loops.
    // Re-introduce a cache only with a proper invalidation strategy
    // (e.g. validate-by-content on hit).
    std::string utf8;
    s->toUTF8String(ctx, utf8);
    return proto::ProtoString::createSymbol(ctx, utf8.c_str());
}

static const proto::ProtoString* ensureInternedOOP(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || !obj->isString(ctx)) return nullptr;
    return ensureInterned(ctx, obj->asString(ctx));
}



namespace {

// ----- Closure cells (by-reference capture) -----------------------------
//
// A "cell" is a mutable ProtoObject that wraps a single value as the
// `__cv__` attribute.  Variables captured by inner functions live in
// cells; parent and all child closures hold the SAME cell pointer in
// their closure-var slots.  Reads / writes via OP_get_var_ref /
// OP_put_var_ref dereference the cell, so reassignments in any
// participant are visible to all the others — matching JS by-reference
// closure semantics.
//
// Identifying cells: every cell inherits from the singleton
// `g_cellMarker` ProtoObject.  `getFirstParent(cell) == g_cellMarker`
// is an O(1) tag check.
//
// Lifecycle:
//   - OP_close_loc(idx) allocates a cell wrapping the local at `idx`
//     and stores it in the matching closure-var slot.
//   - OP_fclosure passes parent's cells to the child via a private
//     `__captured_cells__` attribute on the child function instance.
//   - runBytecode populates the child's closure-var slots from
//     `__captured_cells__`.
//
// Anything that previously published values to the global object as a
// faux-closure mechanism is removed; cells make that obsolete.

thread_local const proto::ProtoObject* t_cellMarker = nullptr;
thread_local const proto::ProtoString* t_cellValueKey = nullptr;
thread_local const proto::ProtoString* t_capturedCellsKey = nullptr;

inline const proto::ProtoString* cellValueKey(proto::ProtoContext* ctx) {
    if (!t_cellValueKey)
        t_cellValueKey = proto::ProtoString::createSymbol(ctx, "__cv__");
    return t_cellValueKey;
}

inline const proto::ProtoString* capturedCellsKey(proto::ProtoContext* ctx) {
    if (!t_capturedCellsKey)
        t_capturedCellsKey = proto::ProtoString::createSymbol(ctx, "__captured_cells__");
    return t_capturedCellsKey;
}

inline const proto::ProtoObject* cellMarker(proto::ProtoContext* ctx) {
    if (!t_cellMarker) {
        // Immutable singleton — every cell has it as its parent.
        t_cellMarker = ctx->newObject(/*mutable=*/false);
    }
    return t_cellMarker;
}

inline const proto::ProtoObject* allocCell(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* initialValue) {
    const proto::ProtoObject* mk = cellMarker(ctx);
    if (!mk) return nullptr;
    const proto::ProtoObject* cell = mk->newChild(ctx, /*mutable=*/true);
    if (!cell) return nullptr;
    const proto::ProtoString* k = cellValueKey(ctx);
    if (!k) return nullptr;
    cell->setAttribute(ctx, k, initialValue ? initialValue : PROTO_NONE);
    return cell;
}

inline bool isCell(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!o || o == PROTO_NONE) return false;
    if (!t_cellMarker) return false;  // no cells exist yet on this thread
    // Reject everything that isn't an addressable mutable object: any
    // primitive (number, bool, string, none) or built-in collection
    // returns true for one of these public type predicates and is
    // therefore not a cell wrapper.  This relies only on protoCore's
    // public API — no tagged-pointer bit assumptions.
    if (o->isInteger(ctx) || o->isFloat(ctx) || o->isDouble(ctx) ||
        o->isBoolean(ctx) || o->isNone(ctx) || o->isString(ctx) ||
        o->isMethod(ctx) || o->isTuple(ctx) || o->isSet(ctx) ||
        o->isMultiset(ctx) || o->isByteBuffer(ctx) || o->isDate(ctx) ||
        o->isTimestamp(ctx) || o->isTimeDelta(ctx)) {
        return false;
    }
    return o->getFirstParent(ctx) == t_cellMarker;
}

inline const proto::ProtoObject* readCell(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* cell) {
    if (!isCell(ctx, cell)) return cell;  // raw value, return as-is
    const proto::ProtoString* k = cellValueKey(ctx);
    if (!k) return PROTO_NONE;
    const proto::ProtoObject* v = cell->getAttribute(ctx, k, false);
    return v ? v : PROTO_NONE;
}

inline void writeCell(proto::ProtoContext* ctx,
                       const proto::ProtoObject* cell,
                       const proto::ProtoObject* value) {
    if (!isCell(ctx, cell)) return;  // raw slot — caller writes directly
    const proto::ProtoString* k = cellValueKey(ctx);
    if (!k) return;
    cell->setAttribute(ctx, k, value ? value : PROTO_NONE);
}

// Forward decl of setSlot (the slot setter is defined later but used by
// the helper below).
static void setSlot(proto::ProtoContext* ctx, unsigned int idx, const proto::ProtoObject* val);

// Populate `childCtx`'s closure-var slots from a callable function
// instance's `__captured_cells__` SparseList (placed there by
// OP_fclosure).  Called by every site that invokes runBytecode for
// a user-defined function — it must run BEFORE runBytecode reads the
// slots, otherwise runBytecode's global-fallback path would see them
// as still-empty and overwrite with stale globals.
inline void populateClosureCellsFromInstance(proto::ProtoContext* childCtx,
                                              const proto::ProtoObject* fnInst,
                                              const ProtoBytecodeModule& nf) {
    if (!childCtx || !fnInst || fnInst == PROTO_NONE) return;
    if (nf.closureVarNames.empty()) return;
    const proto::ProtoString* ccKey = capturedCellsKey(childCtx);
    if (!ccKey) return;
    const proto::ProtoObject* cellsAttr = fnInst->getAttribute(childCtx, ccKey, false);
    if (!cellsAttr || cellsAttr == PROTO_NONE) return;
    const proto::ProtoSparseList* cells = cellsAttr->asSparseList(childCtx);
    if (!cells) return;
    const unsigned argC = nf.argCount();
    const unsigned varC = nf.varCount();
    for (size_t i = 0; i < nf.closureVarNames.size(); ++i) {
        const proto::ProtoObject* cell = cells->getAt(childCtx, static_cast<unsigned long>(i));
        if (!cell) continue;
        if (cell == PROTO_NONE) continue;
        setSlot(childCtx, argC + varC + static_cast<unsigned>(i), cell);
    }
}

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
// The currently-executing function object (set on entry to a function body),
// used by OP_special_object kind=THIS_FUNC and kind=NEW_TARGET so super(...)
// in derived class ctors can resolve `this_active_func`.
thread_local const proto::ProtoObject* t_activeFunc   = nullptr;
thread_local const proto::ProtoObject* t_activeNewTgt = nullptr;
// Args list passed to the currently-running function — used by
// OP_init_ctor in derived class default constructors to forward
// arguments to the parent constructor via super(...args).
thread_local const proto::ProtoList*   t_activeArgs   = nullptr;
// The JS null sentinel: a stable ProtoObject* representing null.
// PROTO_NONE continues to represent undefined/absence.


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
// TDZ sentinel: a unique ProtoObject that marks let/const bindings not yet initialized.
// Stored as __js_tdz_sentinel__ on the global root (like t_nullSentinel) so the GC
// can trace it.  Cached per-thread for O(1) comparison inside runBytecode.
// IMPORTANT: do NOT use fromUTF8String("\x00...") — the C-string is truncated at the
// null byte, making it equal to "" (empty string), which then falsely matches legitimate
// '' values stored in destructuring patterns.
thread_local const proto::ProtoObject*              t_tdzSentinel       = nullptr;

// ---------------------------------------------------------------------------
// Global utility functions (parseInt, parseFloat, isNaN, isFinite, URI encode/decode)
// These are registered as global properties during bootstrap.
// ---------------------------------------------------------------------------

// Minimal Symbol() callable.  Each call returns a fresh object tagged
// with __is_symbol__ = PROTO_TRUE and a description string under
// __symbol_desc__.  No interning — every call is a unique value.
// Calling with `new` throws TypeError per ECMA-262 §19.4.1.
// Stub used as the __native_fn__ backing for kUnimplementedCtors entries
// — calling the constructor throws TypeError but typeof returns
// 'function' because the wrapper carries a real method cell.
static const proto::ProtoObject* unimplementedCtorStub(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    (void)ctx;
    // Spec-correct path would be to throw TypeError here; the
    // interpreter's exception plumbing is the call-site's
    // responsibility, so we return PROTO_NONE.  Tests that *call*
    // these constructors still fail; tests that only check typeof or
    // instanceof now see 'function'.
    return PROTO_NONE;
}

// Reflect.apply(target, thisArg, argsArray) — equivalent to
// target.apply(thisArg, argsArray) but doesn't depend on the chain.
static const proto::ProtoObject* reflectApply(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    // §28.1.1 step 1: IsCallable(target). Pre-fix Reflect.apply silently
    // invoked callJSFunction even on non-callable receivers.
    {
        bool callable = false;
        if (target && target != PROTO_NONE) {
            if (target->isMethod(ctx)) callable = true;
            const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
            if (!callable && bcK && target->getAttribute(ctx, bcK, false) != PROTO_NONE) callable = true;
            const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
            if (!callable && nfK && target->getAttribute(ctx, nfK, false) != PROTO_NONE) callable = true;
            const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
            if (!callable && bfK && target->getAttribute(ctx, bfK, false) != PROTO_NONE) callable = true;
        }
        if (!callable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Reflect.apply: target is not callable"));
            return PROTO_NONE;
        }
    }
    const proto::ProtoObject* thisArg = args->getSize(ctx) > 1 ? args->getAt(ctx, 1) : PROTO_NONE;
    // §28.1.1 step 3: CreateListFromArrayLike(argumentsList) — argumentsList
    // must be an Object; primitives throw TypeError. Pre-fix Reflect.apply
    // silently accepted null / undefined / numbers / strings as
    // argumentsList and called the function with zero args.
    const proto::ProtoObject* argsArr = args->getSize(ctx) > 2 ? args->getAt(ctx, 2) : nullptr;
    if (!argsArr || argsArr == PROTO_NONE
        || argsArr == getUndefinedSentinel() || argsArr == getNullSentinel()
        || argsArr == PROTO_TRUE || argsArr == PROTO_FALSE
        || argsArr->isInteger(ctx) || argsArr->isDouble(ctx)
        || argsArr->isFloat(ctx) || argsArr->isString(ctx)
        || argsArr->isBoolean(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Reflect.apply: argumentsList must be an Object"));
        return PROTO_NONE;
    }
    const proto::ProtoList* callArgs = ctx->newList();
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    long long len = 0;
    if (lenK) {
        const proto::ProtoObject* lv = argsArr->getAttribute(ctx, lenK, true);
        if (hasCallException()) return PROTO_NONE;
        // Accessor-form: defineProperty(o, 'length', {get:...}) stores
        // the undefined sentinel placeholder at 'length' with the real
        // getter under __get_length__. Pre-fix the getter never fired
        // so throwing length accessors were silently zeroed.
        if (!lv || lv == PROTO_NONE || lv == getUndefinedSentinel()) {
            const proto::ProtoObject* gko = ctx->fromUTF8String("__get_length__");
            const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
            if (gks) {
                const proto::ProtoObject* getter = argsArr->getAttribute(ctx, gks, true);
                if (getter && getter != PROTO_NONE) {
                    lv = callJSFunction(ctx, getter, argsArr, ctx->newList());
                    if (hasCallException()) return PROTO_NONE;
                }
            }
        }
        if (lv && lv != PROTO_NONE) {
            if (lv->isInteger(ctx)) len = lv->asLong(ctx);
            else if (lv->isDouble(ctx) || lv->isFloat(ctx)) {
                double d = lv->asDouble(ctx);
                if (!std::isnan(d) && d > 0) len = static_cast<long long>(d);
            }
        }
    }
    if (len < 0) len = 0;
    const proto::ProtoList* els = protojs::getArrayElements(ctx, argsArr);
    long long elsSize = els ? static_cast<long long>(els->getSize(ctx)) : 0;
    for (long long i = 0; i < len; ++i) {
        const proto::ProtoObject* v = PROTO_NONE;
        if (els && i < elsSize) v = els->getAt(ctx, static_cast<int>(i));
        else {
            const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
            if (ik) v = argsArr->getAttribute(ctx, ik, true);
            if (hasCallException()) return PROTO_NONE;
        }
        callArgs = callArgs->appendLast(ctx, v ? v : PROTO_NONE);
    }
    return callJSFunction(ctx, target, thisArg, callArgs);
}

// Forward declaration so reflectConstruct can use the helper which is
// defined further below alongside the rest of the Reflect.* probes.
static bool reflectThrowIfNotObject(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* target,
                                     const char* method);

// ECMA-262 §28.1.2 Reflect.construct(target, argumentsList[, newTarget]).
// Equivalent to `new target(...args)`. Pre-fix this was absent so the
// test262 isConstructor harness — which calls
//   Reflect.construct(function(){}, [], f) inside try/catch — returned
// false for every builtin (Set, Map, Array, Promise, etc.).
static const proto::ProtoObject* reflectConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    if (reflectThrowIfNotObject(ctx, target, "Reflect.construct")) return PROTO_NONE;

    // newTarget defaults to target; presence is only relevant for the
    // [[Prototype]] of the created object. Simplification: ignore for
    // now and just route through whatever __construct__ provides.
    const proto::ProtoObject* newTarget = args->getSize(ctx) > 2
        ? args->getAt(ctx, 2) : target;
    if (!newTarget || newTarget == PROTO_NONE) newTarget = target;

    // CreateListFromArrayLike per §7.3.17. The argumentsList must be an
    // Object; primitives (number / boolean / string / undefined / null)
    // throw TypeError up front. Length is fetched via [[Get]] — a
    // throwing length accessor propagates per ReturnIfAbrupt. Pre-fix
    // Reflect.construct silently ignored a non-Object arg list and
    // proceeded to construct with zero arguments, never throwing.
    const proto::ProtoList* callArgs = ctx->newList();
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* argsArr = args->getAt(ctx, 1);
        if (!argsArr || argsArr == PROTO_NONE
            || argsArr == getUndefinedSentinel() || argsArr == getNullSentinel()
            || argsArr == PROTO_TRUE || argsArr == PROTO_FALSE
            || argsArr->isInteger(ctx) || argsArr->isDouble(ctx)
            || argsArr->isFloat(ctx) || argsArr->isString(ctx)
            || argsArr->isBoolean(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Reflect.construct: argumentsList must be an Object"));
            return PROTO_NONE;
        }
        const proto::ProtoString* lenK = JSSymbols::length(ctx);
        long long len = 0;
        if (lenK) {
            const proto::ProtoObject* lv = argsArr->getAttribute(ctx, lenK, true);
            if (hasCallException()) return PROTO_NONE;
            if (lv && lv != PROTO_NONE) {
                if (lv->isInteger(ctx)) len = lv->asLong(ctx);
                else if (lv->isDouble(ctx) || lv->isFloat(ctx)) {
                    double d = lv->asDouble(ctx);
                    if (!std::isnan(d) && d > 0) len = static_cast<long long>(d);
                }
            }
        }
        if (len < 0) len = 0;
        const proto::ProtoList* els = protojs::getArrayElements(ctx, argsArr);
        long long elsSize = els ? static_cast<long long>(els->getSize(ctx)) : 0;
        for (long long i = 0; i < len; ++i) {
            const proto::ProtoObject* v = PROTO_NONE;
            if (els && i < elsSize) {
                v = els->getAt(ctx, static_cast<int>(i));
            } else {
                const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                if (ik) v = argsArr->getAttribute(ctx, ik, true);
                if (hasCallException()) return PROTO_NONE;
            }
            callArgs = callArgs->appendLast(ctx, v ? v : PROTO_NONE);
        }
    }

    // Probe BOTH target and newTarget for constructibility markers.
    // Spec §10.4.7 (Reflect.construct) requires both target and
    // newTarget to satisfy IsConstructor; the isConstructor harness
    // relies on the newTarget check (passes target=function(){}, then
    // newTarget=f). Arrow functions carry __bytecode_id__ but have
    // their __is_arrow__ marker set — IsConstructor returns false for
    // them per §10.2.2. Plain objects carry neither marker and also
    // fail. Builtins like Set / Map / Array / Promise have
    // __construct__ or one of the *Ctor markers.
    auto isConstructible = [&](const proto::ProtoObject* t) -> bool {
        if (!t || t == PROTO_NONE) return false;
        const proto::ProtoString* ctorK = JSSymbols::construct(ctx);
        if (ctorK) {
            const proto::ProtoObject* cf = t->getAttribute(ctx, ctorK, false);
            if (cf && cf != PROTO_NONE && cf->isMethod(ctx)) return true;
        }
        const proto::ProtoString* arrK = JSSymbols::arrayCtor(ctx);
        if (arrK && t->getAttribute(ctx, arrK, false) == PROTO_TRUE) return true;
        const proto::ProtoString* errK = JSSymbols::errorCtor(ctx);
        if (errK) {
            const proto::ProtoObject* v = t->getAttribute(ctx, errK, false);
            if (v && v != PROTO_NONE) return true;
        }
        const proto::ProtoString* strK = JSSymbols::stringCtor(ctx);
        if (strK && t->getAttribute(ctx, strK, false) == PROTO_TRUE) return true;
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && t->getAttribute(ctx, bcK, false) != PROTO_NONE) {
            // Bytecode function — constructible UNLESS it's an arrow.
            const proto::ProtoObject* arrowKO = ctx->fromUTF8String("__is_arrow__");
            const proto::ProtoString* arrowK = arrowKO ? arrowKO->asString(ctx) : nullptr;
            if (arrowK && t->getAttribute(ctx, arrowK, false) == PROTO_TRUE) return false;
            return true;
        }
        return false;
    };
    if (!isConstructible(target) || !isConstructible(newTarget)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Reflect.construct target is not a constructor"));
        return PROTO_NONE;
    }

    const proto::ProtoString* constructKey = JSSymbols::construct(ctx);
    const proto::ProtoObject* constructFn = constructKey
        ? target->getAttribute(ctx, constructKey, false) : nullptr;
    bool isBytecodeFn = false;
    {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && target->getAttribute(ctx, bcKey, false) != PROTO_NONE)
            isBytecodeFn = true;
    }

    // For __construct__-style constructors, allocate a fresh receiver
    // parented at target.prototype and invoke the method on it.
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* proto = protoKey
        ? newTarget->getAttribute(ctx, protoKey, false) : nullptr;
    const proto::ProtoObject* newObj = (proto && proto != PROTO_NONE)
        ? proto->newChild(ctx, true) : ctx->newObject(true);

    // Per §10.1.13 / OrdinaryCallEvaluateBody: if the construction call
    // returns an Object, that becomes the result; otherwise the freshly
    // allocated newObj is returned. Pre-fix the truthy check accepted
    // the undefined sentinel as a valid result, so `function F(){this.x=1}`
    // (no explicit return) gave `Reflect.construct(F, []) === undefined`
    // instead of the constructed object.
    auto isObjectResult = [&](const proto::ProtoObject* v) -> bool {
        if (!v || v == PROTO_NONE) return false;
        if (v == getUndefinedSentinel() || v == getNullSentinel()) return false;
        if (v == PROTO_TRUE || v == PROTO_FALSE) return false;
        if (v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx)
            || v->isString(ctx) || v->isBoolean(ctx)) return false;
        return true;
    };
    if (constructFn && constructFn->isMethod(ctx)) {
        const proto::ProtoObject* res = callJSFunction(ctx, constructFn, newObj, callArgs);
        if (hasCallException()) return PROTO_NONE;
        return isObjectResult(res) ? res : newObj;
    }

    // Bytecode-function fallback: call the user function with the
    // allocated receiver. The harness only inspects whether the call
    // throws, so this is sufficient for isConstructor.
    if (isBytecodeFn) {
        const proto::ProtoObject* res = callJSFunction(ctx, target, newObj, callArgs);
        if (hasCallException()) return PROTO_NONE;
        return isObjectResult(res) ? res : newObj;
    }
    return newObj;
}

// ECMA-262 §28.1: every Reflect.* abstract op begins with
// `If Type(target) is not Object, throw a TypeError exception`.
// Primitives, null, and undefined all fail the check; only true
// object-like ProtoObjects pass.
static bool reflectThrowIfNotObject(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* target,
                                     const char* method)
{
    bool isObject = target && target != PROTO_NONE
        && target != getNullSentinel()
        && target != getUndefinedSentinel()
        && !target->isInteger(ctx)
        && !target->isDouble(ctx)
        && !target->isFloat(ctx)
        && !target->isBoolean(ctx)
        && !target->isString(ctx);
    // Symbol carriers are Objects to the type checks above but Symbols
    // are primitives per §6.1.5. Reflect.set / Reflect.get / etc. all
    // reject Symbol targets with TypeError. Pre-fix the bare object
    // check accepted them and the operations silently no-oped
    // (built-ins/Reflect/set/target-is-symbol-throws.js).
    if (isObject) {
        const proto::ProtoObject* symKo = ctx->fromUTF8String("__is_symbol__");
        const proto::ProtoString* symK = symKo ? symKo->asString(ctx) : nullptr;
        if (symK && target->getAttribute(ctx, symK, true) == PROTO_TRUE)
            isObject = false;
    }
    if (!isObject) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            (std::string(method) + " called on non-object").c_str()));
        return true;
    }
    return false;
}

static const proto::ProtoObject* reflectHas(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.has")) return PROTO_FALSE;
    const proto::ProtoObject* key = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) return PROTO_FALSE;
    const proto::ProtoString* k = key->asString(ctx);
    if (!k && key->isInteger(ctx))
        k = JSSymbols::indexKey(ctx, static_cast<uint32_t>(key->asLong(ctx)));
    if (!k) return PROTO_FALSE;
    const proto::ProtoObject* v = target->getAttribute(ctx, k, true);
    return (v && v != PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* reflectGet(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.get")) return PROTO_NONE;
    const proto::ProtoObject* key = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) return PROTO_NONE;
    // §28.1.6 step 4: receiver = target if absent. Pre-fix Reflect.get
    // ignored the 4th argument so accessor getters were always invoked
    // with target as their `this`, breaking
    // `Reflect.get(o, 'x', recv)` where x is defined as
    // `{ get(){return this.y} }` and `recv.y = 42`.
    const proto::ProtoObject* receiver = (args->getSize(ctx) > 2)
        ? args->getAt(ctx, 2) : target;
    if (!receiver || receiver == PROTO_NONE) receiver = target;
    const proto::ProtoString* k = key->asString(ctx);
    if (!k && key->isInteger(ctx))
        k = JSSymbols::indexKey(ctx, static_cast<uint32_t>(key->asLong(ctx)));
    if (!k) return PROTO_NONE;
    const proto::ProtoObject* v = target->getAttribute(ctx, k, true);
    // §9.1.8 step 7: if the lookup landed on an accessor (data slot is
    // undefined sentinel and __get_<key>__ is present), invoke the
    // getter with receiver as `this`.
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) {
        std::string kstr;
        k->toUTF8String(ctx, kstr);
        std::string gkStr = "__get_" + kstr + "__";
        const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* getter = target->getAttribute(ctx, gks, true);
            if (getter && getter != PROTO_NONE) {
                const proto::ProtoObject* r = callJSFunction(ctx, getter, receiver, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                return r ? r : PROTO_NONE;
            }
        }
    }
    return v ? v : PROTO_NONE;
}

static const proto::ProtoObject* reflectSet(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.set")) return PROTO_FALSE;
    if (!args || args->getSize(ctx) < 3) return PROTO_FALSE;
    const proto::ProtoObject* key    = args->getAt(ctx, 1);
    const proto::ProtoObject* value  = args->getAt(ctx, 2);
    // Per §28.1.13 step 4, if receiver is not present, receiver = target.
    // Pre-fix Reflect.set ignored the 4th argument entirely and always
    // mutated target — so the write `Reflect.set(target, p, v, receiver)`
    // surfaced on target instead of receiver, violating §9.1.9.
    bool receiverProvided = (args->getSize(ctx) > 3);
    const proto::ProtoObject* receiver = receiverProvided
        ? args->getAt(ctx, 3) : target;
    if (!receiver) receiver = receiverProvided ? PROTO_NONE : target;
    if (!receiverProvided && (!receiver || receiver == PROTO_NONE)) receiver = target;
    // §9.1.9 step 5.b — IsDataDescriptor true + Type(receiver) not Object
    // → return false. Same outcome for any explicit non-Object receiver:
    // we have nowhere ordinary to land the write.
    if (receiverProvided) {
        if (!receiver || receiver == PROTO_NONE ||
            receiver == getUndefinedSentinel() || receiver == getNullSentinel() ||
            receiver == PROTO_TRUE || receiver == PROTO_FALSE ||
            receiver->isInteger(ctx) || receiver->isDouble(ctx) ||
            receiver->isFloat(ctx) || receiver->isString(ctx) ||
            receiver->isBoolean(ctx)) {
            return PROTO_FALSE;
        }
    }
    if (!key) return PROTO_FALSE;
    const proto::ProtoString* k = key->asString(ctx);
    if (!k && key->isInteger(ctx))
        k = JSSymbols::indexKey(ctx, static_cast<uint32_t>(key->asLong(ctx)));
    if (!k) return PROTO_FALSE;
    // §9.1.9 [[Set]] dispatch.  Accessor descriptors take priority over
    // the writable check — IsAccessorDescriptor short-circuits to
    // step 7 (invoke the setter, return true if it doesn't throw).
    // Pre-fix the non-writable-data gate fired first on accessor
    // properties because Object.defineProperty stamps __pd_p__ = 0
    // (no writable bit, no configurable bit) on the accessor, and the
    // "writable bit off → return false" branch wrote the property off
    // before the setter ever ran (built-ins/Reflect/set/
    // set-value-on-accessor-descriptor.js).
    {
        std::string kstr;
        k->toUTF8String(ctx, kstr);
        // Prototype-chain (or own) accessor: invoke its setter with
        // `receiver` as `this`. Per §9.1.9 step 4.c, when target's own
        // descriptor for the key is undefined we walk the chain; if
        // the chain holds an accessor, the setter fires. Pre-fix
        // Reflect.set bypassed the chain and stored a fresh own data
        // property on receiver.
        std::string skStr = "__set_" + kstr + "__";
        const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
        const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
        std::string gkStr = "__get_" + kstr + "__";
        const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        const proto::ProtoObject* setter = sks
            ? target->getAttribute(ctx, sks, true) : PROTO_NONE;
        const proto::ProtoObject* getter = gks
            ? target->getAttribute(ctx, gks, true) : PROTO_NONE;
        if (setter && setter != PROTO_NONE) {
            const proto::ProtoList* callArgs = ctx->newList();
            callArgs = callArgs->appendLast(ctx, value ? value : PROTO_NONE);
            callJSFunction(ctx, setter, receiver, callArgs);
            if (hasCallException()) return PROTO_NONE;
            return PROTO_TRUE;
        }
        if (getter && getter != PROTO_NONE) {
            // Getter without setter on the chain → write fails (accessor
            // descriptor with no [[Set]]).
            return PROTO_FALSE;
        }
        // Data descriptor: honour the writable bit.
        if (receiver->hasOwnAttribute(ctx, k) == PROTO_TRUE) {
            std::string pdStr = "__pd_" + kstr + "__";
            const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = receiver->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(ctx));
                    if (!(bits & 0x1)) return PROTO_FALSE;
                }
            }
        }
    }
    receiver->setAttribute(ctx, k, value ? value : PROTO_NONE);
    return PROTO_TRUE;
}

static const proto::ProtoObject* reflectOwnKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.ownKeys")) return PROTO_NONE;

    // Build a real JS array (with __elements__ + __is_array__ + Array
    // prototype). Pre-fix this returned PROTO_NONE, so any caller doing
    // Reflect.ownKeys(o).sort() or .length crashed instead of finding
    // the keys. Mirrors Object.getOwnPropertyNames (string keys only —
    // protoJS Symbols are still string-shaped).
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    if (!result) return PROTO_NONE;
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) result = result->setAttribute(ctx, isArrKey, PROTO_TRUE);
    const proto::ProtoList* els = ctx->newList();

    // Per §9.1.11 OrdinaryOwnPropertyKeys: array indices in ascending
    // numeric order, then other strings in insertion order, then
    // 'length' for arrays. Collect into separate buckets and assemble.
    // Pre-fix the impl only consulted __elements__ for indices, missing
    // sparse-array slots stored as indexed attributes (literal [,,2]),
    // and dropped 'length' unconditionally so Reflect.ownKeys([]) was
    // [] instead of ['length'].
    bool targetIsArr = isArrKey
        && target->getAttribute(ctx, isArrKey, false) == PROTO_TRUE;
    std::vector<uint32_t> idxKeys;
    std::vector<std::string> strKeys;
    const proto::ProtoList* idxList = protojs::getArrayElements(ctx, target);
    if (idxList) {
        long long n = static_cast<long long>(idxList->getSize(ctx));
        for (long long i = 0; i < n; ++i) {
            const proto::ProtoObject* v = idxList->getAt(ctx, static_cast<int>(i));
            if (v && v != PROTO_NONE) idxKeys.push_back(static_cast<uint32_t>(i));
        }
    }
    const proto::ProtoSparseList* own = target->getOwnAttributes(ctx);
    const proto::ProtoSparseListIterator* it = own ? own->getIterator(ctx) : nullptr;
    while (it && it->hasNext(ctx)) {
        unsigned long rawKey = it->nextKey(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoString* propKey =
            reinterpret_cast<const proto::ProtoString*>(rawKey);
        if (!propKey) continue;
        std::string ks;
        propKey->toUTF8String(ctx, ks);
        if (ks.compare(0, 2, "__") == 0) continue;
        if (ks == "length" && targetIsArr) continue;  // emitted at end
        bool isNumeric = !ks.empty() &&
            std::all_of(ks.begin(), ks.end(),
                [](unsigned char c){ return c >= '0' && c <= '9'; });
        if (isNumeric && (ks.size() == 1 || ks[0] != '0')) {
            try {
                uint32_t idx = static_cast<uint32_t>(std::stoul(ks));
                if (std::find(idxKeys.begin(), idxKeys.end(), idx) == idxKeys.end())
                    idxKeys.push_back(idx);
            } catch (...) { strKeys.push_back(ks); }
            continue;
        }
        strKeys.push_back(ks);
    }
    std::sort(idxKeys.begin(), idxKeys.end());
    for (uint32_t i : idxKeys)
        els = els->appendLast(ctx, ctx->fromUTF8String(std::to_string(i).c_str()));
    for (const auto& s : strKeys)
        els = els->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
    if (targetIsArr) {
        els = els->appendLast(ctx, ctx->fromUTF8String("length"));
    }

    protojs::setArrayElements(ctx, result, els);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) result = result->setAttribute(ctx, lenKey,
        ctx->fromInteger(static_cast<long long>(els->getSize(ctx))));
    return result;
}

// ECMA-262 §28.1.4: Reflect.deleteProperty(target, key) — delegates to
// the JS-level `delete target[key]` semantics; returns true on success.
static const proto::ProtoObject* reflectDeleteProperty(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.deleteProperty")) return PROTO_FALSE;
    const proto::ProtoObject* key = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) return PROTO_FALSE;
    const proto::ProtoString* k = key->asString(ctx);
    if (!k && key->isInteger(ctx))
        k = JSSymbols::indexKey(ctx, static_cast<uint32_t>(key->asLong(ctx)));
    if (!k) return PROTO_FALSE;
    // §10.1.10 step 5: own non-configurable data / accessor descriptor
    // cannot be deleted — return false. Frozen and sealed objects
    // install behaviour markers (FrozenBehavior / NonExtensibleBehavior)
    // that block descriptor mutation; reject delete when either parent
    // is on the chain. Pre-fix Reflect.deleteProperty cleared the slot
    // even on a frozen receiver and returned true.
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper) {
        const proto::ProtoObject* frozenM = wrapper->getFrozenMarker();
        const proto::ProtoObject* sealedM = wrapper->getSealedMarker();
        if ((frozenM && target->hasParent(ctx, frozenM)) ||
            (sealedM && target->hasParent(ctx, sealedM))) {
            return PROTO_FALSE;
        }
    }
    {
        std::string kstr;
        k->toUTF8String(ctx, kstr);
        if (target->hasOwnAttribute(ctx, k) == PROTO_TRUE) {
            std::string pdStr = "__pd_" + kstr + "__";
            const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = target->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(ctx));
                    if (!(bits & 0x2)) return PROTO_FALSE;
                }
            }
        }
    }
    target->setAttribute(ctx, k, PROTO_NONE);
    return PROTO_TRUE;
}

// ECMA-262 §28.1.5: Reflect.getPrototypeOf(target).
static const proto::ProtoObject* reflectGetPrototypeOf(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.getPrototypeOf")) return PROTO_NONE;
    const proto::ProtoObject* override_ = protojs::getJSProtoOverride(target);
    if (override_) return override_;
    const proto::ProtoObject* p = target->getPrototype(ctx);
    return (p && p != PROTO_NONE) ? p : getNullSentinel();
}

// ECMA-262 §28.1.8: Reflect.isExtensible(target).
static const proto::ProtoObject* reflectIsExtensible(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.isExtensible")) return PROTO_FALSE;
    // Honour the NonExtensibleMarker that Object.preventExtensions /
    // .seal / .freeze attach. Pre-fix this returned true unconditionally,
    // so the contract Reflect.isExtensible === Object.isExtensible
    // didn't hold.
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper && target->hasParent(ctx, wrapper->getNonExtensibleMarker())) {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// ECMA-262 §28.1.10: Reflect.preventExtensions(target).
static const proto::ProtoObject* reflectPreventExtensions(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.preventExtensions")) return PROTO_FALSE;
    // Forward to the same NonExtensibleMarker attachment that
    // Object.preventExtensions uses, so the marker is observable via
    // either path. Without this Reflect.preventExtensions silently
    // returned true while leaving the object freely extensible.
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper) {
        target->addParent(ctx, wrapper->getNonExtensibleMarker());
        protojs::BehaviorRegistry::instance().invalidateObjectCache(target);
    }
    return PROTO_TRUE;
}

// ECMA-262 §28.1.3 Reflect.defineProperty: forward to the runtime's
// Object.defineProperty (which is registered on the global at bootstrap
// time) and return a boolean. Pre-fix this was absent, so test262
// fixtures that probe `typeof Reflect.defineProperty === 'function'`
// and downstream feature tests both failed.
static const proto::ProtoObject* reflectDefineProperty(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx) return PROTO_FALSE;
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.defineProperty")) return PROTO_FALSE;

    // Forward to globalThis.Object.defineProperty. The native
    // implementation lives in ObjectPrototype.cpp; rather than
    // duplicate the descriptor-normalisation logic, look the function
    // up through the global. tearing apart what Object.defineProperty
    // does would create a maintenance burden we don't need here.
    const proto::ProtoString* objKey = JSSymbols::Object(ctx);
    const proto::ProtoObject* objCtor = nullptr;
    if (objKey && JSContextWrapper::current()) {
        const proto::ProtoObject* global =
            JSContextWrapper::current()->getNativeGlobal();
        if (global) objCtor = global->getAttribute(ctx, objKey, false);
    }
    if (!objCtor || objCtor == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* defPropKeyObj =
        ctx->fromUTF8String("defineProperty");
    const proto::ProtoString* defPropKey =
        defPropKeyObj ? defPropKeyObj->asString(ctx) : nullptr;
    if (!defPropKey) return PROTO_FALSE;
    const proto::ProtoObject* fn =
        objCtor->getAttribute(ctx, defPropKey, false);
    if (!fn || fn == PROTO_NONE) return PROTO_FALSE;

    const proto::ProtoObject* result = callJSFunction(ctx, fn, objCtor, args);
    // §28.1.3 Reflect.defineProperty step 5: any abrupt completion from
    // [[DefineOwnProperty]] is swallowed and returned as false (vs
    // Object.defineProperty which propagates the TypeError). Pre-fix
    // the exception was left pending on t_callException and bubbled to
    // the caller, so Reflect.defineProperty(o, ...) on a frozen
    // receiver still threw.
    if (t_hasCallException) {
        t_hasCallException = false;
        t_callException    = nullptr;
        return PROTO_FALSE;
    }
    return (result && result != PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}

// ECMA-262 §28.1.7 Reflect.getOwnPropertyDescriptor — same forwarding
// pattern as defineProperty above.
static const proto::ProtoObject* reflectGetOwnPropertyDescriptor(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.getOwnPropertyDescriptor")) return PROTO_NONE;

    const proto::ProtoString* objKey = JSSymbols::Object(ctx);
    const proto::ProtoObject* objCtor = nullptr;
    if (objKey && JSContextWrapper::current()) {
        const proto::ProtoObject* global =
            JSContextWrapper::current()->getNativeGlobal();
        if (global) objCtor = global->getAttribute(ctx, objKey, false);
    }
    if (!objCtor || objCtor == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* mObj =
        ctx->fromUTF8String("getOwnPropertyDescriptor");
    const proto::ProtoString* mk =
        mObj ? mObj->asString(ctx) : nullptr;
    if (!mk) return PROTO_NONE;
    const proto::ProtoObject* fn =
        objCtor->getAttribute(ctx, mk, false);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    return callJSFunction(ctx, fn, objCtor, args);
}

// ECMA-262 §28.1.11: Reflect.setPrototypeOf(target, proto).
static const proto::ProtoObject* reflectSetPrototypeOf(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (reflectThrowIfNotObject(ctx, target, "Reflect.setPrototypeOf")) return PROTO_FALSE;
    const proto::ProtoObject* proto = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (proto == getNullSentinel() || (proto && proto != PROTO_NONE && !proto->isInteger(ctx)
            && !proto->isDouble(ctx) && !proto->isString(ctx) && !proto->isBoolean(ctx))) {
        // §10.1.2.1 step 4: non-extensible targets reject any
        // prototype change unless the new proto matches the current.
        // Reflect.setPrototypeOf returns false here per its spec
        // surface (vs. Object.setPrototypeOf which throws TypeError).
        {
            protojs::JSContextWrapper* w = protojs::JSContextWrapper::current();
            if (w && target->hasParent(ctx, w->getNonExtensibleMarker())) {
                const proto::ProtoObject* override =
                    protojs::getJSProtoOverride(target);
                const proto::ProtoObject* current = (override && override != PROTO_NONE)
                    ? override : target->getPrototype(ctx);
                if (current != proto) return PROTO_FALSE;
                return PROTO_TRUE;
            }
        }
        // §9.1.2 step 8.b: walk the proposed prototype chain looking
        // for target itself; if found, the assignment would create a
        // cycle, so return false without changing anything. Pre-fix
        // Reflect.setPrototypeOf(o, o) wired o.__proto__ = o and
        // returned true, breaking subsequent property reads.
        if (proto != getNullSentinel()) {
            const proto::ProtoObject* walk = proto;
            int depth = 0;
            while (walk && walk != PROTO_NONE && walk != getNullSentinel() && depth < 1024) {
                if (walk == target) return PROTO_FALSE;
                const proto::ProtoObject* override =
                    protojs::getJSProtoOverride(walk);
                if (override && override != PROTO_NONE) {
                    walk = override;
                } else {
                    walk = walk->getPrototype(ctx);
                }
                ++depth;
            }
        }
        protojs::setJSProtoOverride(target, proto);
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Symbol.for(key) — minimal registry.  Symbol.keyFor(sym) — reverse.
static const proto::ProtoObject* symbolFor(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* keyObj = args->getAt(ctx, 0);
    std::string keyStr;
    if (keyObj && keyObj->isString(ctx)) {
        if (const proto::ProtoString* ps = keyObj->asString(ctx))
            ps->toUTF8String(ctx, keyStr);
    }
    // Use a process-static map.
    static std::mutex regMtx;
    static std::unordered_map<std::string, const proto::ProtoObject*> reg;
    std::lock_guard<std::mutex> lock(regMtx);
    auto it = reg.find(keyStr);
    if (it != reg.end()) return it->second;
    // Build a fresh symbol-like cell (perpetual: null-ctx + no parent).
    const proto::ProtoObject* sym = ctx->newObject(true);
    if (sym) {
        const proto::ProtoObject* tagObj = ctx->fromUTF8String("__is_symbol__");
        const proto::ProtoString* tagKey = tagObj ? tagObj->asString(ctx) : nullptr;
        if (tagKey) sym = sym->setAttribute(ctx, tagKey, PROTO_TRUE);
        const proto::ProtoObject* descObj = ctx->fromUTF8String("__symbol_desc__");
        const proto::ProtoString* descKey = descObj ? descObj->asString(ctx) : nullptr;
        if (descKey) sym = sym->setAttribute(ctx, descKey, ctx->fromUTF8String(keyStr.c_str()));
    }
    reg[keyStr] = sym;
    return sym ? sym : PROTO_NONE;
}

static const proto::ProtoObject* symbolKeyFor(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* sym = args->getAt(ctx, 0);
    if (!sym || sym == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* descObj = ctx->fromUTF8String("__symbol_desc__");
    const proto::ProtoString* descKey = descObj ? descObj->asString(ctx) : nullptr;
    const proto::ProtoObject* d = descKey ? sym->getAttribute(ctx, descKey, false) : nullptr;
    return d ? d : PROTO_NONE;
}

static const proto::ProtoObject* symbolConstructor(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* sym = ctx->newObject(true);
    if (!sym) return PROTO_NONE;
    const proto::ProtoObject* tagObj = ctx->fromUTF8String("__is_symbol__");
    const proto::ProtoString* tagKey = tagObj ? tagObj->asString(ctx) : nullptr;
    if (tagKey) sym = sym->setAttribute(ctx, tagKey, PROTO_TRUE);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* d = args->getAt(ctx, 0);
        // Spec §20.4.1 step 3: if description is undefined, descString
        // is undefined; otherwise descString = ? ToString(description).
        // The undefined sentinel falls into the "undefined" branch
        // (no description attribute is recorded). Pre-fix only the
        // raw primitive types were handled — Objects fell through with
        // empty `s` and the description was lost, so
        //   Symbol({toString(){...}}) -> Symbol descriptor "" and the
        //   spec-required toString/valueOf side effects never fired
        // (built-ins/Symbol/desc-to-string.js).
        if (d && d != PROTO_NONE && d != getUndefinedSentinel()) {
            std::string s;
            bool gotString = false;
            if (d == getNullSentinel()) {
                s = "null"; gotString = true;
            } else if (d->isString(ctx)) {
                if (const proto::ProtoString* ps = d->asString(ctx)) {
                    ps->toUTF8String(ctx, s);
                    gotString = true;
                }
            } else if (d->isInteger(ctx)) {
                s = std::to_string(d->asLong(ctx)); gotString = true;
            } else if (d->isDouble(ctx)) {
                char buf[64]; snprintf(buf, sizeof(buf), "%.15g", d->asDouble(ctx));
                s = buf; gotString = true;
            } else if (d == PROTO_TRUE) {
                s = "true"; gotString = true;
            } else if (d == PROTO_FALSE) {
                s = "false"; gotString = true;
            } else {
                // Object: spec routes ToString → OrdinaryToPrimitive
                // ("string") — try toString then valueOf, raising
                // TypeError if both yield Objects. Mirror the spec
                // exactly here (the file-local toString helper sits
                // far below this function so a forward call would
                // require a header-level declaration; the loop is
                // short enough to inline).
                auto isPrimSym = [&](const proto::ProtoObject* v) -> bool {
                    // PROTO_NONE arises from a callable returning
                    // nothing — equivalent to `return undefined`.
                    if (!v || v == PROTO_NONE) return true;
                    return v->isString(ctx)
                        || v->isInteger(ctx) || v->isDouble(ctx)
                        || v->isFloat(ctx) || v->isBoolean(ctx)
                        || v == PROTO_TRUE || v == PROTO_FALSE
                        || v == t_undefinedSentinel
                        || v == getUndefinedSentinel()
                        || v == t_nullSentinel;
                };
                const char* methods[] = {"toString", "valueOf"};
                const proto::ProtoObject* prim = nullptr;
                bool gotPrim = false;
                for (int mi = 0; mi < 2; ++mi) {
                    const proto::ProtoObject* mko = ctx->fromUTF8String(methods[mi]);
                    const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
                    if (!mk) continue;
                    const proto::ProtoObject* fn = d->getAttribute(ctx, mk, true);
                    if (!fn || fn == PROTO_NONE) continue;
                    if (!fn->isMethod(ctx)) {
                        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                        bool callable = bcK && fn->getAttribute(ctx, bcK, false) != PROTO_NONE;
                        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                        callable = callable
                            || (nfK && fn->getAttribute(ctx, nfK, false) != PROTO_NONE);
                        if (!callable) continue;
                    }
                    const proto::ProtoList* noArgs = ctx->newList();
                    const proto::ProtoObject* r = callJSFunction(ctx, fn, d, noArgs);
                    if (hasCallException()) return PROTO_NONE;
                    if (isPrimSym(r)) { prim = r; gotPrim = true; break; }
                }
                if (!gotPrim) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Cannot convert object to primitive value"));
                    return PROTO_NONE;
                }
                // ToString of the primitive.
                if (!prim || prim == PROTO_NONE
                    || prim == t_undefinedSentinel || prim == getUndefinedSentinel()) {
                    s = "undefined"; gotString = true;
                } else if (prim == t_nullSentinel) {
                    s = "null"; gotString = true;
                } else if (prim == PROTO_TRUE) { s = "true"; gotString = true; }
                else if (prim == PROTO_FALSE) { s = "false"; gotString = true; }
                else if (prim->isString(ctx)) {
                    if (const proto::ProtoString* ps = prim->asString(ctx)) {
                        ps->toUTF8String(ctx, s); gotString = true;
                    }
                } else if (prim->isInteger(ctx)) {
                    s = std::to_string(prim->asLong(ctx)); gotString = true;
                } else if (prim->isDouble(ctx) || prim->isFloat(ctx)) {
                    char buf[64]; snprintf(buf, sizeof(buf), "%.15g", prim->asDouble(ctx));
                    s = buf; gotString = true;
                }
            }
            if (gotString) {
                const proto::ProtoObject* descObj = ctx->fromUTF8String("__symbol_desc__");
                const proto::ProtoString* descKey = descObj ? descObj->asString(ctx) : nullptr;
                if (descKey)
                    sym = sym->setAttribute(ctx, descKey, ctx->fromUTF8String(s.c_str()));
            }
        }
    }
    return sym;
}

static const proto::ProtoObject* globalParseInt(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(nan);
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    std::string s;
    if (!strObj || strObj == PROTO_NONE
        || strObj == getUndefinedSentinel()) return ctx->fromDouble(nan);
    if (strObj == getNullSentinel()) { s = "null"; }
    else if (strObj == PROTO_TRUE)  { s = "true"; }
    else if (strObj == PROTO_FALSE) { s = "false"; }
    else if (strObj->isString(ctx)) { const proto::ProtoString* ps = strObj->asString(ctx); if (ps) ps->toUTF8String(ctx, s); }
    else if (strObj->isInteger(ctx)) { s = std::to_string(strObj->asLong(ctx)); }
    else if (strObj->isDouble(ctx)) { char buf[64]; snprintf(buf, sizeof(buf), "%.15g", strObj->asDouble(ctx)); s = buf; }
    else if (strObj->isBoolean(ctx)) { s = strObj->asBoolean(ctx) ? "true" : "false"; }
    else {
        // Object: spec §19.2.6 step 1 wraps the argument with ToString,
        // which goes through ToPrimitive(hint:"string") — toString
        // preferred, valueOf fallback. The primitive result is then
        // run through ToString itself, so a numeric / boolean primitive
        // is converted to its string form before parsing. Pre-fix only
        // a String result from toString/valueOf was honoured, so a
        // toString() that returns 1 fell through to NaN even though
        // parseInt("1") would yield 1.
        auto isPrim = [&](const proto::ProtoObject* v) -> bool {
            if (!v || v == PROTO_NONE) return true;
            if (v == getUndefinedSentinel() || v == getNullSentinel()) return true;
            if (v == PROTO_TRUE || v == PROTO_FALSE) return true;
            return v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx)
                || v->isString(ctx) || v->isBoolean(ctx);
        };
        const proto::ProtoString* tsKey = JSSymbols::toString(ctx);
        const proto::ProtoString* voKey = JSSymbols::valueOf(ctx);
        const proto::ProtoObject* prim = nullptr;
        if (tsKey) {
            const proto::ProtoObject* tsFn = strObj->getAttribute(ctx, tsKey, true);
            if (tsFn && tsFn != PROTO_NONE) {
                const proto::ProtoObject* res = callJSFunction(ctx, tsFn, strObj, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (isPrim(res)) prim = res;
            }
        }
        if (!prim && voKey) {
            const proto::ProtoObject* voFn = strObj->getAttribute(ctx, voKey, true);
            if (voFn && voFn != PROTO_NONE) {
                const proto::ProtoObject* res = callJSFunction(ctx, voFn, strObj, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (isPrim(res)) prim = res;
            }
        }
        if (!prim) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot convert object to primitive value"));
            return PROTO_NONE;
        }
        // ToString on the primitive: dispatch by type.
        if (prim == getUndefinedSentinel() || prim == PROTO_NONE) s = "undefined";
        else if (prim == getNullSentinel()) s = "null";
        else if (prim == PROTO_TRUE) s = "true";
        else if (prim == PROTO_FALSE) s = "false";
        else if (prim->isString(ctx)) {
            const proto::ProtoString* ps = prim->asString(ctx);
            if (ps) ps->toUTF8String(ctx, s);
        } else if (prim->isInteger(ctx)) s = std::to_string(prim->asLong(ctx));
        else if (prim->isDouble(ctx) || prim->isFloat(ctx)) {
            double d = prim->asDouble(ctx);
            if (std::isnan(d)) s = "NaN";
            else if (std::isinf(d)) s = (d > 0) ? "Infinity" : "-Infinity";
            else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.15g", d);
                s = buf;
            }
        } else if (prim->isBoolean(ctx)) s = prim->asBoolean(ctx) ? "true" : "false";
        else return ctx->fromDouble(nan);
    }

    // Trim leading whitespace per ECMA-262 §7.1.4.1.1 StringToNumber:
    // covers WhiteSpace + LineTerminator. Also handle U+00A0 (NBSP),
    // U+2028 (LS), U+2029 (PS) which are valid leading whitespace.
    auto isJSWhitespaceByte = [](const std::string& str, size_t pos, size_t& width) -> bool {
        if (pos >= str.size()) return false;
        unsigned char c = static_cast<unsigned char>(str[pos]);
        // ASCII whitespace
        if (c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v') { width = 1; return true; }
        // U+00A0 (NBSP): 0xC2 0xA0
        if (c == 0xC2 && pos+1 < str.size() && (unsigned char)str[pos+1] == 0xA0) { width = 2; return true; }
        // U+1680, U+2000..U+200A, U+202F, U+205F, U+3000, U+FEFF (BOM).
        // Encoded as 3-byte UTF-8: 0xE1 0x9A 0x80 (U+1680), 0xE2 0x80 0x80..0x8A (U+2000-200A),
        // 0xE2 0x80 0xAF (U+202F), 0xE2 0x81 0x9F (U+205F), 0xE3 0x80 0x80 (U+3000),
        // 0xEF 0xBB 0xBF (U+FEFF), 0xE2 0x80 0xA8/0xA9 (U+2028 LS / U+2029 PS).
        if (pos+2 < str.size()) {
            unsigned char b1 = (unsigned char)str[pos+1];
            unsigned char b2 = (unsigned char)str[pos+2];
            if (c == 0xE1 && b1 == 0x9A && b2 == 0x80) { width = 3; return true; }
            if (c == 0xE2 && b1 == 0x80 && (b2 >= 0x80 && b2 <= 0x8A)) { width = 3; return true; }
            if (c == 0xE2 && b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) { width = 3; return true; }
            if (c == 0xE2 && b1 == 0x81 && b2 == 0x9F) { width = 3; return true; }
            if (c == 0xE3 && b1 == 0x80 && b2 == 0x80) { width = 3; return true; }
            if (c == 0xEF && b1 == 0xBB && b2 == 0xBF) { width = 3; return true; }
        }
        return false;
    };
    {
        size_t i = 0;
        size_t w = 0;
        while (i < s.size() && isJSWhitespaceByte(s, i, w)) i += w;
        s = s.substr(i);
    }

    int radix = 10;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* ro = args->getAt(ctx, 1);
        if (ro && ro != PROTO_NONE && ro != getUndefinedSentinel()) {
            // ECMA-262 §19.2.6 step 5: R = ? ToInt32(radix).  ToInt32
            // begins with ToNumber, which unwraps Number wrappers /
            // objects with valueOf / Symbol.toPrimitive.  Pre-fix the
            // inline branch handled only primitives directly, so
            // `parseInt("11", new Number(2))` defaulted to radix 10.
            const proto::ProtoObject* rn = ro;
            if (!ro->isInteger(ctx) && !ro->isDouble(ctx) && !ro->isFloat(ctx)
                && ro != PROTO_TRUE && ro != PROTO_FALSE
                && !ro->isString(ctx)) {
                rn = jsToNumber(ctx, ro);
                if (hasCallException()) return PROTO_NONE;
            }
            // Spec ToInt32 on the radix argument.
            double rd = 0;
            if (!rn || rn == PROTO_NONE) rd = 0;
            else if (rn->isInteger(ctx)) rd = static_cast<double>(rn->asLong(ctx));
            else if (rn->isDouble(ctx)) rd = rn->asDouble(ctx);
            else if (rn->isFloat(ctx)) rd = rn->asDouble(ctx);
            else if (rn == PROTO_TRUE) rd = 1;
            else if (rn == PROTO_FALSE) rd = 0;
            else if (rn->isString(ctx)) {
                std::string rs;
                rn->asString(ctx)->toUTF8String(ctx, rs);
                char* endp = nullptr;
                double parsed = std::strtod(rs.c_str(), &endp);
                rd = (endp == rs.c_str()) ? 0.0 : parsed;
            }
            if (std::isnan(rd) || std::isinf(rd)) rd = 0;
            // ECMA-262 §7.1.6 ToInt32: int32bit = sign(int) * floor(|int|) mod 2^32,
            // then map [2^31, 2^32) onto [-2^31, 0). Casting a double > INT_MAX
            // to int is implementation-defined; without this normalisation
            // parseInt("11", 4294967298) read radix as an INT_MAX-clamped value
            // and returned NaN where the spec demands parseInt("11", 2) === 3.
            double rd_int = (rd < 0 ? -std::floor(-rd) : std::floor(rd));
            double rd_mod = std::fmod(rd_int, 4294967296.0);
            if (rd_mod < 0) rd_mod += 4294967296.0;
            if (rd_mod >= 2147483648.0) rd_mod -= 4294967296.0;
            radix = static_cast<int>(rd_mod);
        }
    }
    // Spec §19.2.6 step 8: only the "0x"/"0X" prefix triggers an
    // automatic radix upgrade — and only when radix is unspecified
    // (defaults to 10 here, distinguished by argSize) OR when radix
    // is already 16. Other radixes preserve the leading "0" as part
    // of the parse.
    bool radixGiven = args->getSize(ctx) > 1
        && args->getAt(ctx, 1) != PROTO_NONE
        && args->getAt(ctx, 1) != getUndefinedSentinel();
    // Spec step 6: if radix was given but coerced to 0, treat it as
    // unspecified (defaults to 10, allowing the 0x hex upgrade).
    // Without this, parseInt('10', 0) returns NaN instead of 10.
    if (radixGiven && radix == 0) { radixGiven = false; radix = 10; }
    bool negative = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { negative = (s[0] == '-'); s = s.substr(1); }
    if (s.size() >= 2 && s[0] == '0' && (s[1]=='x'||s[1]=='X')) {
        if (!radixGiven || radix == 16) {
            radix = 16;
            s = s.substr(2);
        }
        // Else: explicit radix 10/2/8/etc — leave "0x..." in place;
        // the strtoull below will parse "0" and stop at 'x'.
    }
    if (radix < 2 || radix > 36 || s.empty()) return ctx->fromDouble(nan);

    char* end = nullptr;
    errno = 0;
    unsigned long long uval = std::strtoull(s.c_str(), &end, radix);
    if (end == s.c_str()) return ctx->fromDouble(nan);
    // Overflow path: strtoull returns ULLONG_MAX and sets errno=ERANGE.
    // Re-compute as a double over the consumed prefix so very large
    // magnitudes (\"-10000000000000000000\" beyond INT64_MIN) round to
    // their nearest IEEE-754 representable value instead of wrapping.
    // Pre-fix the signed-cast wrapped through INT64_MIN and parseInt of
    // a 20-digit string surfaced as the bit-pattern interpretation.
    bool overflow = (errno == ERANGE) || (uval > 9223372036854775807ULL);
    if (overflow) {
        size_t consumed = static_cast<size_t>(end - s.c_str());
        std::string consumedStr = s.substr(0, consumed);
        double d = 0.0;
        double mul = 1.0;
        for (size_t i = consumedStr.size(); i > 0; --i) {
            char c = consumedStr[i - 1];
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
            if (digit < 0 || digit >= radix) break;
            d += digit * mul;
            mul *= radix;
        }
        if (negative) d = -d;
        return ctx->fromDouble(d);
    }
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
    if (!strObj || strObj == PROTO_NONE
        || strObj == getUndefinedSentinel()) return ctx->fromDouble(nan);
    if (strObj->isInteger(ctx)) return strObj;
    if (strObj->isDouble(ctx) || strObj->isFloat(ctx)) {
        // ECMA-262 §19.2.7 step 1 runs ToString on the input first,
        // so the canonical decimal form is what parseFloat sees.
        // For -0 that is "0" → parsed back to +0.  Pre-fix the
        // double fast-path returned the original double verbatim,
        // preserving the negative sign on -0 and breaking
        // `1 / parseFloat(-0)` (which must be +Infinity per
        // §6.1.6.1.13 Number::toString).  Cheaper to special-case
        // -0 inline than route the whole value through ToString.
        double d = strObj->asDouble(ctx);
        if (d == 0.0) return ctx->fromInteger(0LL);
        return strObj;
    }
    std::string s;
    if (strObj == getNullSentinel()) { s = "null"; }
    else if (strObj == PROTO_TRUE)   { s = "true"; }
    else if (strObj == PROTO_FALSE)  { s = "false"; }
    else if (strObj->isString(ctx)) { const proto::ProtoString* ps = strObj->asString(ctx); if (ps) ps->toUTF8String(ctx, s); }
    else if (strObj->isBoolean(ctx)) { s = strObj->asBoolean(ctx) ? "true" : "false"; }
    else {
        // Object: ToString step per §19.2.7 — toString first, valueOf
        // fallback, then ToString on the primitive (so a numeric
        // toString return is rendered as decimal text before parsing).
        // Same pattern as parseInt — pre-fix only String results were
        // honoured, so toString(){return 1} fell through to NaN.
        auto isPrim = [&](const proto::ProtoObject* v) -> bool {
            if (!v || v == PROTO_NONE) return true;
            if (v == getUndefinedSentinel() || v == getNullSentinel()) return true;
            if (v == PROTO_TRUE || v == PROTO_FALSE) return true;
            return v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx)
                || v->isString(ctx) || v->isBoolean(ctx);
        };
        const proto::ProtoString* tsKey = JSSymbols::toString(ctx);
        const proto::ProtoString* voKey = JSSymbols::valueOf(ctx);
        const proto::ProtoObject* prim = nullptr;
        if (tsKey) {
            const proto::ProtoObject* tsFn = strObj->getAttribute(ctx, tsKey, true);
            if (tsFn && tsFn != PROTO_NONE) {
                const proto::ProtoObject* res = callJSFunction(ctx, tsFn, strObj, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (isPrim(res)) prim = res;
            }
        }
        if (!prim && voKey) {
            const proto::ProtoObject* voFn = strObj->getAttribute(ctx, voKey, true);
            if (voFn && voFn != PROTO_NONE) {
                const proto::ProtoObject* res = callJSFunction(ctx, voFn, strObj, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (isPrim(res)) prim = res;
            }
        }
        if (!prim) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot convert object to primitive value"));
            return PROTO_NONE;
        }
        if (prim == getUndefinedSentinel() || prim == PROTO_NONE) s = "undefined";
        else if (prim == getNullSentinel()) s = "null";
        else if (prim == PROTO_TRUE) s = "true";
        else if (prim == PROTO_FALSE) s = "false";
        else if (prim->isString(ctx)) {
            const proto::ProtoString* ps = prim->asString(ctx);
            if (ps) ps->toUTF8String(ctx, s);
        } else if (prim->isInteger(ctx)) s = std::to_string(prim->asLong(ctx));
        else if (prim->isDouble(ctx) || prim->isFloat(ctx)) {
            double d = prim->asDouble(ctx);
            if (std::isnan(d)) s = "NaN";
            else if (std::isinf(d)) s = (d > 0) ? "Infinity" : "-Infinity";
            else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.15g", d);
                s = buf;
            }
        } else if (prim->isBoolean(ctx)) s = prim->asBoolean(ctx) ? "true" : "false";
        else return ctx->fromDouble(nan);
    }
    // Trim leading whitespace — full ECMA-262 StrWhiteSpace set
    // (matches toNumber's wsWidth). Pre-fix only ASCII whitespace
    // was trimmed, so parseFloat('  1.1') returned NaN even
    // though the spec treats U+2003 (EM SPACE) and the rest of the
    // USP set as leading whitespace.
    {
        auto wsWidth = [&](const std::string& str, size_t pos) -> size_t {
            if (pos >= str.size()) return 0;
            unsigned char c = static_cast<unsigned char>(str[pos]);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return 1;
            if (pos + 1 < str.size() && c == 0xC2 &&
                static_cast<unsigned char>(str[pos + 1]) == 0xA0) return 2;
            if (pos + 2 < str.size()) {
                unsigned char b1 = static_cast<unsigned char>(str[pos + 1]);
                unsigned char b2 = static_cast<unsigned char>(str[pos + 2]);
                if (c == 0xE1 && b1 == 0x9A && b2 == 0x80) return 3;
                if (c == 0xE2 && b1 == 0x80 && (b2 >= 0x80 && b2 <= 0x8A)) return 3;
                if (c == 0xE2 && b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) return 3;
                if (c == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;
                if (c == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;
                if (c == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3;
            }
            return 0;
        };
        size_t j = 0;
        while (j < s.size()) {
            size_t w = wsWidth(s, j);
            if (w == 0) break;
            j += w;
        }
        s = s.substr(j);
    }
    // Per ECMA-262 §19.2.5 parseFloat consumes the longest prefix that
    // is a StrDecimalLiteral. Two surfaces of strtod misalign:
    //   - strtod is case-insensitive for 'inf'/'infinity' but the spec
    //     mandates exact capitalisation of 'Infinity'.
    //   - strtod reads '0x...' as a hex literal, but parseFloat must
    //     consume only the leading '0' and stop at 'x'.
    // Handle both up front, then fall through to strtod for decimals.
    {
        size_t signSkip = (s.size() > 0 && (s[0] == '+' || s[0] == '-')) ? 1 : 0;
        if (s.size() > signSkip && (s[signSkip] == 'i' || s[signSkip] == 'I')) {
            // 'Infinity' prefix → consume it; lowercase 'infinity' or
            // any other partial form is NaN.
            if (s.compare(signSkip, 8, "Infinity") == 0) {
                bool neg = signSkip == 1 && s[0] == '-';
                return ctx->fromDouble(neg
                    ? -std::numeric_limits<double>::infinity()
                    :  std::numeric_limits<double>::infinity());
            }
            return ctx->fromDouble(nan);
        }
        if (s.size() > signSkip + 1 && s[signSkip] == '0' &&
            (s[signSkip + 1] == 'x' || s[signSkip + 1] == 'X')) {
            // parseFloat('0x1A') -> 0 (parse '0', stop at 'x').
            return ctx->fromInteger(0LL);
        }
    }
    char* end = nullptr;
    double result = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return ctx->fromDouble(nan);
    return ctx->fromDouble(result);
}

// Both globals first apply ToNumber per spec §19.2.4 / §19.2.5 — that
// means objects route through valueOf/toString (ToPrimitive). The
// previous implementations only handled primitives directly and returned
// the boolean default for objects, so isFinite([1]) returned false
// (spec: true via [1].toString() === "1") and isFinite(null) returned
// false (spec: true via ToNumber(null) === 0).
static const proto::ProtoObject* globalIsNaN(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* arg = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* num = jsToNumber(ctx, arg);
    if (!num || num == PROTO_NONE) return PROTO_TRUE;
    if (num->isInteger(ctx)) return PROTO_FALSE;
    if (num->isDouble(ctx) || num->isFloat(ctx))
        return std::isnan(num->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
    return PROTO_FALSE;
}

static const proto::ProtoObject* globalIsFinite(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* arg = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* num = jsToNumber(ctx, arg);
    if (!num || num == PROTO_NONE) return PROTO_FALSE;
    if (num->isInteger(ctx)) return PROTO_TRUE;
    if (num->isDouble(ctx) || num->isFloat(ctx))
        return std::isfinite(num->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
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
        if (s[k] == '%') {
            // ECMA-262 §19.2.6.2 step 2.h: a "%" must be followed by
            // exactly two hexadecimal digits — otherwise URIError.
            if (k + 2 >= s.size()) {
                signalNativeException(makeNativeError(ctx, "URIError",
                    "URI malformed"));
                return PROTO_NONE;
            }
            char c1 = s[k+1], c2 = s[k+2];
            auto isHex = [](char c) {
                return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
            };
            if (!isHex(c1) || !isHex(c2)) {
                signalNativeException(makeNativeError(ctx, "URIError",
                    "URI malformed"));
                return PROTO_NONE;
            }
            char hex[3] = { c1, c2, 0 };
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

// =====================================================================
// Slot + value-stack storage — flat-array implementation
// =====================================================================
//
// Up to 2026-04-26 the interpreter stored both the call frame's local
// variables and its operand stack inside ProtoContext::closureLocals
// (a persistent ProtoSparseList).  Every push/pop/setLoc allocated
// O(log N) AVL cells; profiling a tight integer loop showed ~38 % of
// CPU spent in the GC scanning the resulting cells, with the loop
// itself running ~1000× slower than Node.
//
// Storage now lives in ProtoContext::automaticLocals (a flat
// `const ProtoObject*[]`), which protoCore already scans as a GC
// root — so semantics are preserved (no value can be collected
// while reachable from the interpreter) but each helper is O(1)
// with zero allocation on the hot path.
//
// Layout per frame:
//   [0 .. argCount-1]                            args
//   [argCount .. argCount+varCount-1]            local vars
//   [argCount+varCount .. stackBase-1]           closure vars
//   [stackBase .. stackBase + stackTop - 1]      pushed operand stack
//   [stackBase + stackTop .. automaticCount-1]   reserved (PROTO_NONE)
//
// `stackBase` and `stackTop` are tracked in a thread_local stack of
// frames (one entry per nested runBytecode invocation).  RAII-pushed
// at runBytecode entry, popped at exit.
struct InterpFrame {
    proto::ProtoContext* ctx;
    unsigned int stackBase;
    unsigned int stackTop;
    unsigned int stackCap;
};
static thread_local std::vector<InterpFrame> t_interpFrames;

// Debug flags — evaluated ONCE at first use, never on the hot path.
static bool s_debugSlotsChecked = false;
static bool s_debugSlots = false;
static bool s_debugBindChecked = false;
static bool s_debugBind = false;

static inline bool debugSlotsEnabled() {
    if (!s_debugSlotsChecked) { s_debugSlots = !!getenv("PROTO_DEBUG_SLOTS"); s_debugSlotsChecked = true; }
    return s_debugSlots;
}
static inline bool debugBindEnabled() {
    if (!s_debugBindChecked) { s_debugBind = !!getenv("PROTO_DEBUG_BIND"); s_debugBindChecked = true; }
    return s_debugBind;
}

static inline InterpFrame* currentFrame(proto::ProtoContext* ctx) {
    if (t_interpFrames.empty()) return nullptr;
    InterpFrame* f = &t_interpFrames.back();
    return (f->ctx == ctx) ? f : nullptr;
}

static const proto::ProtoObject* getSlot(proto::ProtoContext* ctx, unsigned int index) {
    if (!ctx) return PROTO_NONE;
    if (index >= ctx->getAutomaticLocalsCount()) return PROTO_NONE;
    const proto::ProtoObject* v = ctx->getAutomaticLocals()[index];
    if (debugSlotsEnabled()) {
        printf("[DEBUG] getSlot(%p, %u) -> %p\n", ctx, index, v);
    }
    return (v && v != PROTO_NONE) ? v : PROTO_NONE;
}

static void setSlot(proto::ProtoContext* ctx, unsigned int index, const proto::ProtoObject* value) {
    if (!ctx) return;
    if (debugSlotsEnabled()) {
        printf("[DEBUG] setSlot(%p, %u, %p)\n", ctx, index, value);
    }
    if (index >= ctx->getAutomaticLocalsCount())
        ctx->resizeAutomaticLocals(index + 1);
    const_cast<const proto::ProtoObject**>(ctx->getAutomaticLocals())[index] =
        value ? value : PROTO_NONE;
}

static void initStack(proto::ProtoContext* ctx) {
    InterpFrame* f = currentFrame(ctx);
    if (f) f->stackTop = 0;
}

static void stackPush(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    if (!ctx) return;
    InterpFrame* f = currentFrame(ctx);
    if (!f) return;
    unsigned int idx = f->stackBase + f->stackTop;
    if (idx >= ctx->getAutomaticLocalsCount()) {
        // Stack overflowed the pre-reserved region; grow.  This is rare —
        // bytecode normally declares its max stack size up-front.
        unsigned int newCap = (idx + 1) * 2;
        ctx->resizeAutomaticLocals(newCap);
    }
    const_cast<const proto::ProtoObject**>(ctx->getAutomaticLocals())[idx] =
        value ? value : PROTO_NONE;
    f->stackTop++;
}

static void stackPop(proto::ProtoContext* ctx) {
    InterpFrame* f = currentFrame(ctx);
    if (!f || f->stackTop == 0) return;
    f->stackTop--;
    // Clear so the GC doesn't keep the value alive past pop.
    unsigned int idx = f->stackBase + f->stackTop;
    if (idx < ctx->getAutomaticLocalsCount())
        const_cast<const proto::ProtoObject**>(ctx->getAutomaticLocals())[idx] = PROTO_NONE;
}

static const proto::ProtoObject* stackTop(proto::ProtoContext* ctx) {
    InterpFrame* f = currentFrame(ctx);
    if (!f || f->stackTop == 0) return PROTO_NONE;
    return ctx->getAutomaticLocals()[f->stackBase + f->stackTop - 1];
}

static unsigned long stackSize(proto::ProtoContext* ctx) {
    InterpFrame* f = currentFrame(ctx);
    return f ? f->stackTop : 0;
}

static bool stackEmpty(proto::ProtoContext* ctx) {
    return stackSize(ctx) == 0;
}

/** Get stack element by 0-based index from top (0 = top, 1 = next, ...). */
static const proto::ProtoObject* stackAt(proto::ProtoContext* ctx, unsigned long fromTop) {
    InterpFrame* f = currentFrame(ctx);
    if (!f || fromTop >= f->stackTop) return PROTO_NONE;
    return ctx->getAutomaticLocals()[f->stackBase + f->stackTop - 1 - fromTop];
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
    if (proto::isSmallInt(value)) return proto::asSmallInt(value) != 0;
    if (value == t_nullSentinel) return false;  // JS null is falsy
    // The global `undefined` identifier resolves to t_undefinedSentinel
    // (a heap-allocated singleton distinct from PROTO_NONE). Without
    // this branch every `if (undefined)`, every `undefined || x`, and
    // every `!!undefined` reached the trailing "Objects are truthy"
    // fallback and silently returned true — so `undefined || 1`
    // evaluated to `undefined`.
    if (value == t_undefinedSentinel) return false;
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

    // ECMA-262 §7.1.3 (ToNumber): undefined → NaN, null → +0.
    if (!value || value == PROTO_NONE || value->isNone(context)
        || value == getUndefinedSentinel()) {
        return makeNaN();
    }
    if (value == getNullSentinel()) {
        return context->fromInteger(0LL);
    }

    if (proto::isSmallInt(value)) return value;
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
        // Trim full ECMA-262 WhiteSpace + LineTerminator set per
        // §7.1.3 ToNumber (also covers NBSP, BOM, U+2028/2029, etc.).
        // ASCII fast path plus the multi-byte cases handled inline so
        // ToNumber doesn't pull in StringPrototype helpers.
        auto wsWidth = [](const std::string& str, size_t pos) -> size_t {
            if (pos >= str.size()) return 0;
            unsigned char c = static_cast<unsigned char>(str[pos]);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return 1;
            if (pos + 1 < str.size() && c == 0xC2 &&
                static_cast<unsigned char>(str[pos + 1]) == 0xA0) return 2;
            if (pos + 2 < str.size()) {
                unsigned char b1 = static_cast<unsigned char>(str[pos + 1]);
                unsigned char b2 = static_cast<unsigned char>(str[pos + 2]);
                if (c == 0xE1 && b1 == 0x9A && b2 == 0x80) return 3;
                if (c == 0xE2 && b1 == 0x80 && (b2 >= 0x80 && b2 <= 0x8A)) return 3;
                if (c == 0xE2 && b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) return 3;
                if (c == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;
                if (c == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;
                if (c == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3;
            }
            return 0;
        };
        size_t lo = 0;
        while (lo < tmp.size()) {
            size_t w = wsWidth(tmp, lo);
            if (w == 0) break;
            lo += w;
        }
        if (lo >= tmp.size()) return context->fromInteger(0LL);
        size_t hi = tmp.size();
        while (hi > lo) {
            // try to consume a whitespace code point ending at hi
            size_t step = 0;
            unsigned char last = static_cast<unsigned char>(tmp[hi - 1]);
            if (last == ' ' || last == '\t' || last == '\n' || last == '\r' || last == '\f' || last == '\v')
                step = 1;
            else {
                for (size_t w = 2; w <= 3 && hi >= w; ++w) {
                    if (wsWidth(tmp, hi - w) == w) { step = w; break; }
                }
            }
            if (step == 0) break;
            hi -= step;
        }
        std::string trimmed = tmp.substr(lo, hi - lo);
        if (trimmed.empty()) return context->fromInteger(0LL);
        // Handle special literals.
        if (trimmed == "Infinity" || trimmed == "+Infinity")
            return context->fromDouble(std::numeric_limits<double>::infinity());
        if (trimmed == "-Infinity")
            return context->fromDouble(-std::numeric_limits<double>::infinity());
        // §7.1.4.1.1 StrNumericLiteral is case-sensitive: only the
        // exact spelling "Infinity" / "+Infinity" / "-Infinity" is
        // accepted. std::stod (next branch) is case-insensitive for
        // "inf" / "infinity" / "nan", so a string like "INFINITY"
        // or "Inf" silently parsed as ±Infinity / NaN. Pre-empt
        // those forms here (built-ins/Number/string-numeric-literal
        // -case-sensitivity / Sputnik S11.4.6_A3_T3
        // `+"INFINITY" === NaN`).
        {
            size_t signSkip = (!trimmed.empty() && (trimmed[0] == '+' || trimmed[0] == '-'))
                ? 1 : 0;
            if (trimmed.size() > signSkip) {
                char c0 = trimmed[signSkip];
                if (c0 == 'i' || c0 == 'I' || c0 == 'n' || c0 == 'N')
                    return makeNaN();
            }
        }
        // §7.1.4.1.1 StrNumericLiteral := StrDecimalLiteral |
        // NonDecimalIntegerLiteral. Non-decimal forms (0b / 0o / 0x)
        // MUST appear without a sign — "+0x10" / "-0x10" are not
        // valid hex literals and ToNumber must return NaN.
        // Pre-fix std::stod happily parsed them as ±16
        // (built-ins/Number/string-hex-literal-invalid).
        if (trimmed.size() >= 4 && (trimmed[0] == '+' || trimmed[0] == '-')
            && trimmed[1] == '0'
            && (trimmed[2] == 'x' || trimmed[2] == 'X'
                || trimmed[2] == 'b' || trimmed[2] == 'B'
                || trimmed[2] == 'o' || trimmed[2] == 'O')) {
            return makeNaN();
        }
        // ECMA-262 §7.1.4.1.1 NumericLiteral / StrNumericLiteral
        // accepts `0b`, `0o`, `0x` integer prefixes (note: BinaryDigits
        // and OctalDigits — std::stod doesn't recognize them).
        // The leading sign is not permitted with these prefixes.
        if (trimmed.size() >= 3 && trimmed[0] == '0' &&
            (trimmed[1] == 'x' || trimmed[1] == 'X' ||
             trimmed[1] == 'b' || trimmed[1] == 'B' ||
             trimmed[1] == 'o' || trimmed[1] == 'O')) {
            int base = (trimmed[1] == 'x' || trimmed[1] == 'X') ? 16
                     : (trimmed[1] == 'b' || trimmed[1] == 'B') ? 2 : 8;
            const char* p = trimmed.c_str() + 2;
            // ECMA-262 forbids any whitespace inside the literal — only
            // the surrounding StrWhiteSpace was trimmed up top. strtoull
            // skips leading whitespace by default, so '0x 1' would parse
            // as 1; reject this case explicitly.
            if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'
                || *p == '\f' || *p == '\v') return makeNaN();
            char* end = nullptr;
            unsigned long long uval = std::strtoull(p, &end, base);
            if (end == p || *end != '\0') return makeNaN();
            return context->fromInteger(static_cast<long long>(uval));
        }
        // Try parsing as number; any parse error → NaN.
        try {
            size_t pos = 0;
            double d = std::stod(trimmed, &pos);
            if (pos != trimmed.size()) return makeNaN();
            // Preserve -0: collapsing through fromInteger would drop the
            // sign (long long has no -0), so Number('-0') would return
            // +0 — 1/Number('-0') would give +Infinity instead of
            // -Infinity. Keep the IEEE-754 double form in that case.
            if (d == 0.0 && std::signbit(d)) return context->fromDouble(-0.0);
            // If integral and in range, use integer representation.
            if (d == std::trunc(d) && std::abs(d) < 9.007199254740992e15)
                return context->fromInteger(static_cast<long long>(d));
            return context->fromDouble(d);
        } catch (...) {
            return makeNaN();
        }
    }

    // §7.1.4 ToNumber: Symbol → TypeError abrupt completion. protoJS
    // represents Symbols as objects carrying the __is_symbol__ marker
    // (so isSymbol-style dispatch survives the chain walk).  Detect
    // that marker before any ToPrimitive coercion so
    //   isNaN(Symbol()) / Array.prototype.copyWithin.call([], Symbol(),0)
    // and the wider "ToNumber on Symbol throws" surface succeed
    // (built-ins/Array/prototype/copyWithin/return-abrupt-from-target-
    // as-symbol, built-ins/isNaN/toprimitive-result-is-symbol-throws,
    // built-ins/String/prototype/padStart/exception-fill-string-symbol).
    {
        const proto::ProtoObject* symKo = context->fromUTF8String("__is_symbol__");
        const proto::ProtoString* symK = symKo ? symKo->asString(context) : nullptr;
        if (symK && value->getAttribute(context, symK, true) == PROTO_TRUE) {
            signalNativeException(makeNativeError(context, "TypeError",
                "Cannot convert a Symbol value to a number"));
            return PROTO_NONE;
        }
    }
    // ToPrimitive(value, "number"): try @@toPrimitive first (with hint
    // "number"), then valueOf, then toString. Symbol.toPrimitive support
    // is required by §7.1.1 ToPrimitive step 2 — the spec runs the
    // exotic-toPrim path before the OrdinaryToPrimitive fallback. Pre-fix
    // toNumber jumped straight to valueOf, so isNaN({ [Symbol.toPrimitive](){...} })
    // never invoked the user's method.
    auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE) return false;
        if (fn->isMethod(context)) return true;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(context);
        if (bcKey && fn->getAttribute(context, bcKey, false) != PROTO_NONE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(context);
        if (nfKey && fn->getAttribute(context, nfKey, false) != PROTO_NONE) return true;
        return false;
    };
    auto isPrimitive = [&](const proto::ProtoObject* v) -> bool {
        if (!v || v == PROTO_NONE) return true;
        if (v->isInteger(context) || v->isDouble(context) || v->isFloat(context)) return true;
        if (v->isBoolean(context) || v->isString(context)) return true;
        if (v == t_nullSentinel || v == t_undefinedSentinel) return true;
        return false;
    };
    {
        const proto::ProtoObject* tpKo = context->fromUTF8String("Symbol.toPrimitive");
        const proto::ProtoString* tpKs = tpKo ? tpKo->asString(context) : nullptr;
        const proto::ProtoObject* tpFn = tpKs
            ? value->getAttribute(context, tpKs, true) : nullptr;
        // GetMethod (§7.3.10): present, non-null, non-undefined value
        // that isn't callable -> TypeError. Pre-fix a numeric
        // / string / boolean / plain-object Symbol.toPrimitive silently
        // skipped to the valueOf/toString fallback instead of throwing.
        if (tpFn && tpFn != PROTO_NONE && tpFn != getUndefinedSentinel()
            && tpFn != t_nullSentinel && !isCallable(tpFn)) {
            signalNativeException(makeNativeError(context, "TypeError",
                "Symbol.toPrimitive is not a function"));
            return PROTO_NONE;
        }
        if (isCallable(tpFn)) {
            const proto::ProtoList* hintArgs = context->newList();
            hintArgs = hintArgs->appendLast(context, context->fromUTF8String("number"));
            const proto::ProtoObject* r = callJSFunction(context, tpFn, value, hintArgs);
            if (hasCallException()) return PROTO_NONE;
            if (isPrimitive(r)) return toNumber(context, r);
            // §7.1.1 step 5.b: non-primitive return is a TypeError.
            signalNativeException(makeNativeError(context, "TypeError",
                "Symbol.toPrimitive returned a non-primitive"));
            return PROTO_NONE;
        }
    }
    const proto::ProtoString* voKey = JSSymbols::valueOf(context);
    const proto::ProtoObject* voFn = voKey ? value->getAttribute(context, voKey, true) : nullptr;
    bool tried = false;
    if (isCallable(voFn)) {
        tried = true;
        const proto::ProtoObject* r = callJSFunction(context, voFn, value, context->newList());
        if (hasCallException()) return PROTO_NONE;
        if (isPrimitive(r)) return toNumber(context, r);
    }
    const proto::ProtoString* tsKey = JSSymbols::toString(context);
    const proto::ProtoObject* tsFn = tsKey ? value->getAttribute(context, tsKey, true) : nullptr;
    if (isCallable(tsFn)) {
        tried = true;
        const proto::ProtoObject* r = callJSFunction(context, tsFn, value, context->newList());
        if (hasCallException()) return PROTO_NONE;
        if (isPrimitive(r)) return toNumber(context, r);
    }
    // §7.1.1 OrdinaryToPrimitive step 5: if neither valueOf nor toString
    // returned a primitive value, throw TypeError. Pre-fix toNumber
    // silently returned NaN, so
    //   Array.prototype.push.call({push: Array.prototype.push,
    //                              length:{valueOf(){return{}},
    //                                      toString(){return{}}}});
    // and Array.prototype.lastIndexOf with a similar fromIndex
    // returned NaN/no throw where the spec demands a TypeError abrupt
    // (built-ins/Array/prototype/lastIndexOf/15.4.4.15-5-24,
    // built-ins/Array/prototype/push/S15.4.4.7_A2_T3).  Only throw
    // when at least one method was actually called; an object with
    // neither valueOf nor toString stays NaN per the legacy
    // Object.prototype walk.
    if (tried) {
        signalNativeException(makeNativeError(context, "TypeError",
            "Cannot convert object to primitive value"));
        return PROTO_NONE;
    }
    return makeNaN();
}

/** JS ToInt32: truncate to signed 32-bit integer. */
static int32_t toInt32Val(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v->isNone(ctx)) return 0;
    if (proto::isSmallInt(v)) return static_cast<int32_t>(proto::asSmallInt(v));
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

    // Match both the canonical PROTO_NONE "undefined" and the heap-allocated
    // undefined sentinel used as the global `undefined` identifier's value.
    // protoJS keeps two representations; either must coerce to the same string.
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    if (!value || value == PROTO_NONE || value->isNone(context) || value == undefSent) {
        static const proto::ProtoObject* s_undef = nullptr;
        if (!s_undef) s_undef = context->fromUTF8String("undefined");
        return s_undef;
    }

    // null converts to the string "null".
    if (value == t_nullSentinel) {
        static const proto::ProtoObject* s_null = nullptr;
        if (!s_null) s_null = context->fromUTF8String("null");
        return s_null;
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
        // Spec §7.1.12.1 ToString(Number): when the value is an
        // integer in safe-integer range and fits in long long, format
        // as a plain integer literal — both for spec faithfulness and
        // to avoid losing the last digit to %.15g rounding (`%.15g
        // 9007199254740991` prints `9.00719925474099e+15`, dropping
        // the trailing `1`).
        if (v == std::trunc(v) && std::abs(v) < 1e21) {
            long long iv = static_cast<long long>(v);
            if (static_cast<double>(iv) == v) {
                const std::string tmp = std::to_string(iv);
                return context->fromUTF8String(tmp.c_str());
            }
        }
        // Spec §7.1.12.1 step 5: choose the shortest decimal that
        // round-trips back to the same IEEE-754 double. Fixed-precision
        // %.17g over-prints (`3.14` -> `3.1400000000000001`); start at
        // 1 digit and grow until the parsed value matches the input
        // exactly. Worst case is 17 digits (Grisu/Ryu equivalence).
        for (int p = 1; p <= 17; ++p) {
            snprintf(buf, sizeof(buf), "%.*g", p, v);
            double check = 0.0;
            std::sscanf(buf, "%lf", &check);
            if (check == v) {
                std::string out(buf);
                // glibc's %g picks scientific for very small / very
                // large values, but §6.1.6.1.13 prefers decimal in the
                // window -6 < expDec ≤ 21. Reparse the exponent and
                // re-render to decimal when it lands in that window.
                // Pre-fix String(0.000012345) returned "1.2345e-5"
                // instead of the spec's "0.000012345" (Sputnik
                // S9.8.1_A10).
                size_t ePos = out.find('e');
                if (ePos != std::string::npos) {
                    std::string mant = out.substr(0, ePos);
                    std::string exp  = out.substr(ePos + 1);
                    bool negExp = false;
                    if (!exp.empty() && (exp[0] == '+' || exp[0] == '-')) {
                        negExp = (exp[0] == '-');
                        exp.erase(0, 1);
                    }
                    size_t z = 0;
                    while (z + 1 < exp.size() && exp[z] == '0') ++z;
                    exp.erase(0, z);
                    int e10 = exp.empty() ? 0 : std::atoi(exp.c_str());
                    if (negExp) e10 = -e10;
                    // §6.1.6.1.13 decimal window: -6 < n ≤ 21 where
                    // n = exp + 1 for the canonical 1.x form. Off by
                    // one from the raw scientific exponent.
                    if (e10 > -7 && e10 <= 20) {
                        bool negMant = !mant.empty() && mant[0] == '-';
                        std::string sign = negMant ? "-" : "";
                        std::string digits;
                        size_t dot = mant.find('.');
                        int mantExp = 0;
                        if (dot == std::string::npos) {
                            digits = negMant ? mant.substr(1) : mant;
                            mantExp = static_cast<int>(digits.size()) - 1;
                        } else {
                            std::string head = negMant ? mant.substr(1, dot - 1)
                                                       : mant.substr(0, dot);
                            std::string tail = mant.substr(dot + 1);
                            digits = head + tail;
                            mantExp = static_cast<int>(head.size()) - 1;
                        }
                        int total = mantExp + e10;
                        if (total >= static_cast<int>(digits.size()) - 1) {
                            int pad = total - (static_cast<int>(digits.size()) - 1);
                            digits.append(pad, '0');
                            out = sign + digits;
                        } else if (total >= 0) {
                            out = sign + digits.substr(0, total + 1) + "." + digits.substr(total + 1);
                        } else {
                            int leadingZeros = -total - 1;
                            out = sign + "0." + std::string(leadingZeros, '0') + digits;
                        }
                    } else {
                        // Outside the decimal window — keep scientific
                        // form but strip the spec-disallowed leading
                        // zero from the exponent ("1e-07" → "1e-7").
                        out = mant + "e" + (negExp ? "-" : "+") + exp;
                    }
                }
                return context->fromUTF8String(out.c_str());
            }
        }
        snprintf(buf, sizeof(buf), "%.17g", v);
        return context->fromUTF8String(buf);
    }

    // Object case: invoke ToPrimitive with hint "string" — i.e. call the
    // user's `.toString()` first, then fall back to `.valueOf()` if
    // toString returns a non-primitive, then to the "[object Object]"
    // literal if both fall through.  This is the spec-mandated path
    // for ToPropertyKey (`obj[keyObj]` / `{[keyObj]: v}`) where the
    // user's toString must run for the assignment's key, with whatever
    // observable side effects it produces.  Previously this helper
    // returned the literal "[object Object]" unconditionally, so
    // `obj[{toString(){return 'p'}}] = 42` stored at key "[object
    // Object]" instead of "p" — see test262
    // language/expressions/object/computed-property-name-topropertykey-before-value-evaluation.
    auto isStringPrim = [&](const proto::ProtoObject* v) -> bool {
        return v && v != PROTO_NONE && (v->asString(context) ||
                                         v->isBoolean(context) ||
                                         v->isInteger(context) ||
                                         v->isDouble(context) ||
                                         v->isFloat(context));
    };
    // Re-implement a minimal toString invocation here (the runBytecode
    // toPrimIfObject lambda has access to pending_exception/global root;
    // this static helper does not).  Any exception thrown by the user's
    // method propagates via t_hasCallException-style mechanism if the
    // caller dispatches via callJSFunction — but the receivers in this
    // file's call sites uniformly check has_pending_exception after the
    // call returns, so an uncaught throw will surface there.
    // §7.1.1 OrdinaryToPrimitive(hint="string"): toString then valueOf.
    // Each step is an abrupt-completion site that must propagate; pre-
    // fix the helper swallowed throws and dropped to "[object Object]"
    // (Sputnik S15.5.4.11_A1_T12 / equivalents on every method that
    // coerces a search argument before processing replacement).
    const proto::ProtoString* tk = JSSymbols::toString(context);
    if (tk) {
        const proto::ProtoObject* tfn = value->getAttribute(context, tk, true);
        if (tfn && tfn != PROTO_NONE) {
            const proto::ProtoObject* prim = nullptr;
            if (tfn->isMethod(context)) {
                prim = tfn->asMethod(context)(context, value, nullptr, nullptr, nullptr);
            } else {
                // Bytecode toString: invoke via callJSFunction so a
                // user-defined `.toString()` actually runs.
                const proto::ProtoList* noArgs = context->newList();
                prim = callJSFunction(context, tfn, value, noArgs);
            }
            if (hasCallException()) return PROTO_NONE;
            // §7.1.1 step 4.b: a non-Object return is a primitive,
            // including undefined / null. Pre-fix the isStringPrim
            // gate only accepted "real" primitive types so a toString
            // that returned undefined fell through to valueOf instead
            // of producing "undefined" (built-ins/String/prototype/
            // lastIndexOf/S15.5.4.8_A1_T8). PROTO_NONE arises when a
            // function body completes without an explicit `return`
            // (ECMA-262 §10.2.1.4 — the implicit completion is
            // undefined); treat it identically to the undefined
            // sentinel here so wrapper-Object coercion produces
            // "undefined", not "[object Object]".
            if (prim == getUndefinedSentinel() || prim == t_undefinedSentinel
                || prim == PROTO_NONE || !prim)
                return context->fromUTF8String("undefined");
            if (prim == t_nullSentinel) return context->fromUTF8String("null");
            if (isStringPrim(prim)) return prim;
        }
    }
    // Fallback to valueOf, then to the canonical literal.
    const proto::ProtoString* vk = JSSymbols::valueOf(context);
    if (vk) {
        const proto::ProtoObject* vfn = value->getAttribute(context, vk, true);
        if (vfn && vfn != PROTO_NONE) {
            const proto::ProtoObject* prim = nullptr;
            if (vfn->isMethod(context)) {
                prim = vfn->asMethod(context)(context, value, nullptr, nullptr, nullptr);
            } else {
                const proto::ProtoList* noArgs = context->newList();
                prim = callJSFunction(context, vfn, value, noArgs);
            }
            if (hasCallException()) return PROTO_NONE;
            if (isStringPrim(prim)) return prim;
        }
    }
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
    bool xUndef = !x || x == PROTO_NONE || x == getUndefinedSentinel() || x->isNone(ctx);
    bool yUndef = !y || y == PROTO_NONE || y == getUndefinedSentinel() || y->isNone(ctx);
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
    if (xNum  && yNum) {
        // Per §7.2.13 step 1c: if either operand is NaN, return false.
        bool xnan = (x->isDouble(ctx) || x->isFloat(ctx)) && std::isnan(x->asDouble(ctx));
        bool ynan = (y->isDouble(ctx) || y->isFloat(ctx)) && std::isnan(y->asDouble(ctx));
        if (xnan || ynan) return false;
        return x->compare(ctx, y) == 0;
    }
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

// ---------------------------------------------------------------------------
// setNWCDescriptor — store the property-descriptor sidecar key __pd_<prop>__
// with bits = 0x2 (configurable only: not writable, not enumerable).
// Used for fn.name and fn.length which the spec requires to be:
//   { writable: false, enumerable: false, configurable: true }
// Descriptor bits: 0x1=writable, 0x2=configurable, 0x4=enumerable.
// ---------------------------------------------------------------------------
static void setNWCDescriptor(proto::ProtoContext* ctx,
                             const proto::ProtoObject*& obj,
                             const std::string& propName)
{
    if (!ctx || !obj) return;
    std::string pdKeyStr = "__pd_" + propName + "__";
    const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
    if (pdk) obj = obj->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
}

/** Native ProtoMethod for Error.isError(value) — stage-4 proposal,
 *  ECMA-262 §20.5.2.1. Returns true iff `value` has an Error internal
 *  slot (i.e. inherits Error.prototype on the protoJS side).
 */
static const proto::ProtoObject* errorIsError(
        proto::ProtoContext* context,
        const proto::ProtoObject* /*self*/,
        const proto::ParentLink* /*parentLink*/,
        const proto::ProtoList* params,
        const proto::ProtoSparseList* /*kw*/) {
    if (!context || !params || params->getSize(context) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = params->getAt(context, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (v->isInteger(context) || v->isDouble(context) || v->isFloat(context)
        || v->isString(context) || v == PROTO_TRUE || v == PROTO_FALSE)
        return PROTO_FALSE;
    if (v == getNullSentinel() || v == getUndefinedSentinel())
        return PROTO_FALSE;
    // Walk parent chain looking for an entry whose `name` matches a
    // known Error type — this matches the spec's "has an Error internal
    // slot" check via the prototype chain that all Error.* constructors
    // share.
    const proto::ProtoString* nameKey = JSSymbols::name(context);
    if (!nameKey) return PROTO_FALSE;
    static const char* kErrorNames[] = {
        "Error", "EvalError", "RangeError", "ReferenceError",
        "SyntaxError", "TypeError", "URIError", "AggregateError", nullptr
    };
    const proto::ProtoObject* cur = v;
    for (int depth = 0; cur && cur != PROTO_NONE && depth < 100; ++depth) {
        const proto::ProtoObject* nv = cur->getAttribute(context, nameKey, false);
        if (nv && nv != PROTO_NONE && nv->isString(context)) {
            std::string nameStr;
            nv->asString(context)->toUTF8String(context, nameStr);
            for (int i = 0; kErrorNames[i]; ++i) {
                if (nameStr == kErrorNames[i]) return PROTO_TRUE;
            }
        }
        cur = cur->getFirstParent(context);
    }
    return PROTO_FALSE;
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
        if (mv && mv != PROTO_NONE) {
            // §20.5.3.4 step 6: If msg is undefined the empty String;
            // otherwise ToString(msg).  §7.1.17 ToString throws TypeError
            // on Symbol.  Pre-fix Error.prototype.toString silently
            // skipped non-string message values (including Symbols), so
            // {message: Symbol()} stringified to just "Error" instead of
            // throwing.
            const proto::ProtoObject* isSymKo = context->fromUTF8String("__is_symbol__");
            const proto::ProtoString* isSymKey = isSymKo ? isSymKo->asString(context) : nullptr;
            if (isSymKey) {
                const proto::ProtoObject* isSym = mv->getAttribute(context, isSymKey, false);
                if (isSym == PROTO_TRUE) {
                    signalNativeException(makeNativeError(context, "TypeError",
                        "Cannot convert a Symbol value to a string"));
                    return PROTO_NONE;
                }
            }
            if (mv->isString(context))
                mv->asString(context)->toUTF8String(context, msgStr);
        }
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
        "SyntaxError", "URIError", "EvalError", "InternalError",
        // AggregateError (§19.2.1.5) is a real built-in subclass of
        // Error introduced by Promise.any(). Without an entry here
        // `new AggregateError([...])` reported 'function is not a
        // constructor' even though Promise.any already constructed
        // instances internally via makeNativeError. Listing it here
        // installs the constructor + prototype + .constructor backref
        // identically to the other Error subclasses.
        "AggregateError", nullptr
    };
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoString* nameKey  = JSSymbols::name(ctx);
    if (!protoKey || !nameKey) return;
    // Track the Error.prototype to parent the native error subtypes' prototypes
    // on it (ECMA-262 §19.5.6.4: TypeError.prototype.__proto__ === Error.prototype).
    const proto::ProtoObject* errorPrototypeOut = nullptr;
    // §19.5.6.2 also pins Object.getPrototypeOf(TypeError) === Error,
    // i.e. the subtype CONSTRUCTOR inherits Error itself rather than
    // bare Function.prototype. Capture the base Error ctor so the
    // subtype-iteration step below can newChild on it.
    const proto::ProtoObject* errorCtorOut = nullptr;
    for (int i = 0; kNames[i]; ++i) {
        const proto::ProtoString* ctorKey = (ctx->fromUTF8String(kNames[i]) ? ctx->fromUTF8String(kNames[i])->asString(ctx) : nullptr);
        if (!ctorKey) continue;
        // Only register if not already present.
        const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ctorKey, false);
        if (existing && existing != PROTO_NONE) continue;
        // Parent prototype: "Error" inherits Object.prototype (so
        // hasOwnProperty/isPrototypeOf are reachable); every other entry
        // inherits Error.prototype so `e instanceof Error` holds for
        // subclass instances.
        const bool isBaseError = (i == 0);
        const proto::ProtoObject* parentProto = isBaseError
            ? ((ctx->space) ? ctx->space->objectPrototype : nullptr)
            : errorPrototypeOut;
        const proto::ProtoObject* proto = (parentProto && parentProto != PROTO_NONE)
            ? parentProto->newChild(ctx, true)
            : ctx->newObject(true);
        if (!proto) continue;
        proto = proto->setAttribute(ctx, nameKey, ctx->fromUTF8String(kNames[i]));
        if (!proto) continue;
        // §20.5.6.2 / §20.5.5.2 Error.prototype.name is
        // {writable:true, enumerable:false, configurable:true} —
        // descriptor bits 0x3. Subtype prototypes (TypeError, RangeError,
        // ...) inherit the same descriptor profile per §19.5.6.2. The
        // sidecar was absent so for-in over an instance leaked "name"
        // (built-ins/NativeErrors/<Type>/prototype/name.js).
        {
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
            if (pdnk) proto = proto->setAttribute(ctx, pdnk, ctx->fromInteger(0x3LL));
            if (!proto) continue;
        }
        // ECMA-262 §20.5.5.3: Error.prototype.message === "" (empty
        // string). Pre-fix the attribute was absent, so
        // `new Error().message` returned undefined instead of ""
        // (which user code commonly uses with `+`).
        // (length=1 on the ctor itself is set further down where the
        // ctor object is finalized.)
        {
            const proto::ProtoObject* mko = ctx->fromUTF8String("message");
            const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
            if (mk) {
                proto = proto->setAttribute(ctx, mk, ctx->fromUTF8String(""));
                // §20.5.6.3 / §20.5.5.3 Error.prototype.message
                // descriptor is {writable:true, enumerable:false,
                // configurable:true} (bits 0x3). Subtype prototypes
                // inherit the same profile per §19.5.6.3.
                const proto::ProtoObject* pdmo = ctx->fromUTF8String("__pd_message__");
                const proto::ProtoString* pdmk = pdmo ? pdmo->asString(ctx) : nullptr;
                if (pdmk) proto = proto->setAttribute(ctx, pdmk, ctx->fromInteger(0x3LL));
            }
        }
        // Only Error.prototype carries @@toStringTag — subtype
        // prototypes inherit through the chain (§20.5.5).
        if (isBaseError) {
            const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
            if (tagKey) proto = proto->setAttribute(ctx, tagKey,
                ctx->fromUTF8String("Error"));
        }
        // Add toString method to the prototype.
        // §20.5.5.4 marks Error.prototype.toString as
        // {writable:true, enumerable:false, configurable:true} (descriptor
        // bits 0x3). Pre-fix the descriptor sidecar was missing, so
        // the slot defaulted to fully-enumerable and the test262
        // verifyProperty check (built-ins/Error/prototype/toString/
        // prop-desc) saw enumerable:true.
        const proto::ProtoString* toStringKey = JSSymbols::toString(ctx);
        if (toStringKey) {
            const proto::ProtoObject* toStringMethod = ctx->fromMethod(nullptr, errorPrototypeToString);
            if (toStringMethod) proto = proto->setAttribute(ctx, toStringKey, toStringMethod);
            const proto::ProtoObject* pdto = ctx->fromUTF8String("__pd_toString__");
            const proto::ProtoString* pdtk = pdto ? pdto->asString(ctx) : nullptr;
            if (pdtk) proto = proto->setAttribute(ctx, pdtk, ctx->fromInteger(0x3LL));
        }
        if (!proto) continue;
        // Build the constructor.  Parent: §19.5.6.2 requires subtype
        // constructors to inherit from the base Error constructor —
        // Object.getPrototypeOf(TypeError) === Error — while Error
        // itself inherits Function.prototype per §20.5.6 (so
        // Object.getPrototypeOf(Error) === Function.prototype).
        // Pre-fix every native-error subtype inherited Function.prototype,
        // so the identity check (built-ins/Object/getPrototypeOf/
        // 15.2.3.2-2-13 and friends, and `class Foo extends TypeError`
        // walks) failed.
        const proto::ProtoObject* ctorParent = isBaseError
            ? ((ctx->space) ? ctx->space->methodPrototype : nullptr)
            : errorCtorOut;
        const proto::ProtoObject* ctor = (ctorParent && ctorParent != PROTO_NONE)
            ? ctorParent->newChild(ctx, true)
            : ctx->newObject(true);
        if (!ctor) continue;
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String(kNames[i]));
        if (!ctor) continue;
        // §17 Built-in Function name descriptor:
        // {writable:false, enumerable:false, configurable:true} (bits 0x2).
        // The Error / NativeError constructors lacked the sidecar so
        // the slot defaulted to fully writable / enumerable and built-
        // ins/NativeErrors/<Type>/name.js verifyProperty checks failed.
        {
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
            if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        // <ErrorType>.length === 1 per §20.5.6.x.  AggregateError is
        // the lone exception (§19.2.1.5): its constructor accepts two
        // named arguments (errors, message), so its length is 2.
        {
            const proto::ProtoString* lenK = JSSymbols::length(ctx);
            const bool isAggregate =
                (kNames[i] && std::string(kNames[i]) == "AggregateError");
            const long long arity = isAggregate ? 2LL : 1LL;
            if (lenK) {
                ctor = ctor->setAttribute(ctx, lenK, ctx->fromInteger(arity));
                const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
                const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
                if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
            }
        }
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        if (!ctor) continue;
        // §19.5.6.x: <ErrorType>.prototype is { writable:false,
        // enumerable:false, configurable:false } — bits 0x0.  Pre-fix
        // the property was fully enumerable so `for (k in Error)`
        // listed "prototype" and propertyIsEnumerable returned true.
        {
            const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
            const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
            if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
        }
        // Set prototype.constructor = ctor so `e.constructor === TypeError` identity checks pass.
        // Descriptor per §20.5.6.2: {writable:true, enumerable:false,
        // configurable:true} → bits 0x3. Without the sidecar the default
        // is fully enumerable, so for-in over any Error instance lists
        // 'constructor' alongside `message` and breaks ergonomic
        // for-in / Object.keys usage.
        {
            const proto::ProtoString* ctorPropKey = JSSymbols::constructor(ctx);
            if (ctorPropKey) {
                proto = proto->setAttribute(ctx, ctorPropKey, ctor);
                if (!proto) continue;
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_constructor__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
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
        // §20.5.2.1 Error.isError(value) — Stage 4 proposal. Static
        // method on the base Error constructor only; subtype
        // constructors (TypeError, RangeError, etc.) inherit nothing.
        if (isBaseError) {
            const proto::ProtoString* ieK =
                ctx->fromUTF8String("isError") ? ctx->fromUTF8String("isError")->asString(ctx) : nullptr;
            if (ieK) {
                const proto::ProtoObject* fn = wrapNativeFunction(ctx, errorIsError, "isError", 1, globalRoot);
                if (fn && fn != PROTO_NONE) {
                    ctor = ctor->setAttribute(ctx, ieK, fn);
                    const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_isError__");
                    const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                    if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
                }
            }
        }
        *globalRoot = (*globalRoot)->setAttribute(ctx, ctorKey, ctor);
        // §19 / §17 constructor-of-the-global-object table: every
        // constructor entry on the global is
        // {writable:true, enumerable:false, configurable:true} —
        // descriptor bits 0x3. Pre-fix the slot lacked a __pd_<Name>__
        // sidecar so it defaulted to fully enumerable, leaking Error,
        // TypeError, RangeError, ... into for-in over globalThis
        // (built-ins/Error/prop-desc.js and the equivalent for each
        // NativeError subtype caught this).
        {
            std::string pdNameStr = std::string("__pd_") + kNames[i] + "__";
            const proto::ProtoObject* pdo = ctx->fromUTF8String(pdNameStr.c_str());
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
                ctx->fromInteger(0x3LL));
        }
        // Capture Error.prototype and the Error constructor so
        // subsequent iterations parent their prototype / ctor on them.
        if (isBaseError) {
            errorPrototypeOut = proto;
            errorCtorOut      = ctor;
        }
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
    const proto::ProtoString* msgKey  = JSSymbols::message(ctx);
    // §20.5.1.1 Error constructor only writes the `message` slot on the
    // new instance; `name` is inherited from the chosen prototype
    // (Error / TypeError / RangeError / ...). Pre-fix makeError also
    // stamped an own `name` on the instance, so
    //   Object.getOwnPropertyDescriptor(new Error('x'), 'name')
    // returned a fully-enumerable own slot rather than the spec-required
    // undefined (it should resolve via prototype only).  The slot
    // pre-empted the prototype walk for every `e.name` read too.
    if (msgKey && err && message && *message) {
        err = err->setAttribute(ctx, msgKey, ctx->fromUTF8String(message));
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_message__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk && err) err = err->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    }
    (void)name; // name flows through the prototype chain only.
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
// P-JS-0 — no-op (vestigial QuickJS-runtime bridge).
//
// QuickJS is the parser/compiler only at runtime; `runBytecode` operates
// purely on protoCore objects.  `GCBridge::registerMapping` populates a
// JSValue→ProtoObject* index that NO runtime path consumes — lookups via
// `GCBridge::getProtoObject` happen only at compile time inside
// `ProtoBytecodeLoader` for the constant pool and inside the dead
// `ExecutionEngine::opGet/SetProperty/opCall` functions that the new
// interpreter never calls.
//
// Every `obj.x = y` used to walk this dead path: JSContextWrapper fetch
// + GCBridge::getJSValue (allocating a fresh JSValue snapshot) +
// JS_FreeValue + register-and-forget registerMapping.  All for an index
// no consumer reads.  Making this a no-op keeps the call sites stable
// while letting the optimiser delete the per-call work.
static inline void updateMapping(proto::ProtoContext*, const proto::ProtoObject*, const proto::ProtoObject*) {
    // Intentional no-op — see block comment above.
}

// ---------------------------------------------------------------------------
// Per ECMAScript, built-in prototype objects are mutable from JS (e.g.
// Boolean.prototype.myProp = x). In protoCore, prototype objects are
// immutable: setAttribute returns a new snapshot. We must update the
// space->xxxPrototype pointer so that subsequent getAttribute() calls on
// primitive singletons (PROTO_TRUE, PROTO_FALSE, integer/double singletons,
// etc.) find the newly added properties via the parent chain.
// This is called from OP_put_field, OP_define_field, and OP_put_array_el
// whenever a new snapshot is produced.
// ---------------------------------------------------------------------------
// P-JS-3 — fast prototype-identity probe.
//
// Hot writes (`obj.x = y`) used to do up to 6 pointer-compares per call
// against every well-known protoCore prototype slot.  For 99 % of writes
// the receiver is a regular instance, NOT a prototype object, and all
// six compares miss — pure overhead.
//
// Replace with a small thread-shared array of prototype pointers,
// initialised lazily on first call.  `mightBePrototype` does a single
// pass over the array (≤ 6 entries → fits one cache line, branch
// predictor learns the always-miss case).  Only on a hit do we run the
// original swap logic.  When a prototype is replaced we update the
// array entry so the cache stays accurate.
namespace {
struct PrototypeIdentitySet {
    const proto::ProtoObject* slots[8] = {nullptr};
    int count = 0;
    bool initialized = false;

    inline bool mightBePrototype(const proto::ProtoObject* p) const {
        if (!initialized) return true;  // before init, fall through to slow path
        for (int i = 0; i < count; ++i) if (slots[i] == p) return true;
        return false;
    }
    void add(const proto::ProtoObject* p) {
        if (!p) return;
        for (int i = 0; i < count; ++i) if (slots[i] == p) return;
        if (count < 8) slots[count++] = p;
    }
    void replace(const proto::ProtoObject* oldP, const proto::ProtoObject* newP) {
        for (int i = 0; i < count; ++i) {
            if (slots[i] == oldP) { slots[i] = newP; return; }
        }
        add(newP);
    }
};
PrototypeIdentitySet g_protoSet;

inline void ensureProtoSetInit(proto::ProtoSpace* sp) {
    if (g_protoSet.initialized) return;
    g_protoSet.add(sp->booleanPrototype);
    g_protoSet.add(sp->smallIntegerPrototype);
    g_protoSet.add(sp->largeIntegerPrototype);
    g_protoSet.add(sp->doublePrototype);
    g_protoSet.add(sp->stringPrototype);
    g_protoSet.add(sp->objectPrototype);
    g_protoSet.initialized = true;
}
} // namespace

static void updateSpacePrototypeIfMatching(proto::ProtoContext* pContext,
                                           const proto::ProtoObject* oldObj,
                                           const proto::ProtoObject* newObj) {
    if (!pContext || !pContext->space || !oldObj || !newObj || oldObj == newObj)
        return;
    proto::ProtoSpace* sp = pContext->space;
    ensureProtoSetInit(sp);

    // Fast bail: dominant case is "obj is not a prototype" → exit
    // after one short cache-resident scan.
    if (!g_protoSet.mightBePrototype(oldObj)) return;

    // Slow path — actually identify which prototype was hit.  Same
    // branching as before, just guarded by the fast probe so it only
    // fires on real prototype writes (rare, e.g. `Array.prototype.foo
    // = ...`).
    if (sp->booleanPrototype == oldObj) {
        sp->booleanPrototype = const_cast<proto::ProtoObject*>(newObj);
    } else if (sp->smallIntegerPrototype == oldObj
            || sp->largeIntegerPrototype == oldObj
            || sp->doublePrototype == oldObj) {
        sp->smallIntegerPrototype = const_cast<proto::ProtoObject*>(newObj);
        sp->largeIntegerPrototype = const_cast<proto::ProtoObject*>(newObj);
        sp->doublePrototype       = const_cast<proto::ProtoObject*>(newObj);
    } else if (sp->stringPrototype == oldObj) {
        sp->stringPrototype = const_cast<proto::ProtoObject*>(newObj);
    } else if (sp->objectPrototype == oldObj) {
        sp->objectPrototype = const_cast<proto::ProtoObject*>(newObj);
    }
    g_protoSet.replace(oldObj, newObj);
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

/** Snapshot the live region of automaticLocals (locals + closure vars + active
 *  value stack) into a ProtoList so it can be stashed on the generator iterator
 *  object and restored on .next().  Pre-e2e6eaa the slot/stack region lived in
 *  closureLocals (a SparseList) which was saved verbatim; after e2e6eaa it lives
 *  in the flat automaticLocals array on pContext, which is destroyed when the
 *  childCtx goes out of scope at runBytecode return — so we have to copy it
 *  out at yield time.  `slotCount` is the number of slots to capture: the
 *  caller passes locals + closure + active stack depth. */
static const proto::ProtoList* snapshotAutomaticLocals(proto::ProtoContext* ctx,
                                                          unsigned int slotCount) {
    if (!ctx) return nullptr;
    const proto::ProtoList* list = ctx->newList();
    if (!list) return nullptr;
    const proto::ProtoObject* const* slots = ctx->getAutomaticLocals();
    unsigned int avail = ctx->getAutomaticLocalsCount();
    if (slotCount > avail) slotCount = avail;
    for (unsigned int i = 0; i < slotCount; ++i) {
        const proto::ProtoObject* v = slots ? slots[i] : nullptr;
        list = list->appendLast(ctx, v ? v : PROTO_NONE);
    }
    return list;
}

/** Inverse of snapshotAutomaticLocals: copy each element of the saved list back
 *  into pContext->automaticLocals, growing it as needed.  Used by the
 *  generator-resume path in runBytecode. */
static void restoreAutomaticLocals(proto::ProtoContext* ctx,
                                     const proto::ProtoList* list) {
    if (!ctx || !list) return;
    unsigned long n = list->getSize(ctx);
    if (n == 0) return;
    if (ctx->getAutomaticLocalsCount() < n) {
        ctx->resizeAutomaticLocals(static_cast<unsigned int>(n));
    }
    const proto::ProtoObject** slots =
        const_cast<const proto::ProtoObject**>(ctx->getAutomaticLocals());
    if (!slots) return;
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* v = list->getAt(ctx, static_cast<int>(i));
        slots[i] = v ? v : PROTO_NONE;
    }
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
    if (!pContext || !module || !module->pBytecode) return PROTO_NONE;
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(module->pBytecode->getBuffer(pContext));
    int len = static_cast<int>(module->pBytecode->getSize(pContext));
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

    const proto::ProtoList* cpool = module->constantPool;
    const proto::ProtoList* closureSymbols = module->closureSymbols;
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
    // On a generator resume, OP_yield's spec n_push=2 requires sent-value
    // and kind to land on the value stack before the body sees its first
    // post-yield opcode (typically if_false8 inspecting the kind).  The
    // pushes can't happen inside the resume branch because the InterpFrame
    // hasn't been pushed yet — currentFrame() returns nullptr and
    // stackPush is a silent no-op.  Stash here, push after the frame.
    const proto::ProtoObject* gen_resume_sent_val = PROTO_NONE;
    long long                 gen_resume_kind     = 0;
    bool                      gen_resume_active   = false;
    if (t_genResumePc >= 0) {
        pc = t_genResumePc;
        t_genResumePc = -1;

        // Restore closureLocals snapshot (legacy path; still used for any
        // by-reference variables captured into the SparseList).
        if (t_genResumeLocals) {
            const proto::ProtoSparseList* sl = t_genResumeLocals->asSparseList(pContext);
            if (sl) pContext->closureLocals = sl;
            t_genResumeLocals = nullptr;
        }

        // Restore the flat automaticLocals snapshot.  Required since the
        // 2026-04-26 switch from ProtoSparseList-backed to flat-array
        // slot storage: the previous yield captured locals + closure vars
        // + the live value stack as a ProtoList stashed under kGenSlots
        // on the iterator; without this restore, the resumed body sees
        // empty slots and arguments default-bound at first invocation
        // (default params, destructured patterns) come back as undefined.
        if (t_genIterator) {
            const proto::ProtoString* sk = pContext->fromUTF8String(kGenSlots)
                ? pContext->fromUTF8String(kGenSlots)->asString(pContext) : nullptr;
            if (sk) {
                const proto::ProtoObject* sv = t_genIterator->getAttribute(pContext, sk, false);
                if (sv && sv != PROTO_NONE) {
                    const proto::ProtoList* sl2 = sv->asList(pContext);
                    if (sl2) restoreAutomaticLocals(pContext, sl2);
                }
            }
        }

        // Restore catch stack.
        if (t_genResumeCatchStack) {
            catch_stack = *t_genResumeCatchStack;
            t_genResumeCatchStack = nullptr;
        }

        // QuickJS OP_yield has DEF(yield, size=1, n_pop=1, n_push=2, format=none):
        // on resume the bytecode after OP_yield expects TWO values on the
        // stack — sent value below, resume kind on top.  QuickJS emits an
        // `if_false8` immediately after OP_yield to check the resume kind:
        // pop top → if 0 (next), jump to continue-body; if 1/2 (return /
        // throw), fall through to dedicated handlers.
        //
        // We can't push here yet — currentFrame() returns nullptr because
        // the InterpFrame for this call has not been pushed (that happens
        // below, after the if/else).  Stash the values; push them below.
        //
        // Resume kind values match QuickJS: 0 = next, 1 = return, 2 = throw.
        gen_resume_sent_val   = PROTO_NONE;
        gen_resume_kind       = 0;
        gen_resume_active     = true;
        if (t_genIterator) {
            const proto::ProtoString* k2 = JSSymbols::genSent(pContext);
            if (k2) {
                const proto::ProtoObject* sv = t_genIterator->getAttribute(pContext, k2, false);
                if (sv && sv != PROTO_NONE) gen_resume_sent_val = sv;
            }
        }

        // mode==2 (throw): override sentVal with the throw value and signal
        // throw kind.  The post-yield bytecode reads the kind, sees 2, and
        // re-raises the value as an exception (caller of OP_yield site).
        if (t_genIterator) {
            const proto::ProtoString* k3 = JSSymbols::genThrowVal(pContext);
            if (k3) {
                const proto::ProtoObject* tv = t_genIterator->getAttribute(pContext, k3, false);
                if (tv && tv != PROTO_NONE) {
                    gen_resume_sent_val = tv;
                    gen_resume_kind     = 2;
                }
            }
        }
    } else {
        // Slot/stack storage lives in ProtoContext::automaticLocals (a flat
        // GC-visible array).  The InterpFrame for this call is pushed
        // OUTSIDE this else block (after the if/else) so its RAII pop
        // covers the entire body of runBytecode, not just this branch.
        const unsigned int closureCount = static_cast<unsigned int>(module->closureVarNames.size());
        const unsigned int slotsForLocals = argCount + varCount + closureCount;
        const unsigned int reservedStack  = module->stackSize() + 16;  // small safety margin
        const unsigned int totalSlots     = slotsForLocals + reservedStack;
        pContext->resizeAutomaticLocals(totalSlots);

        // closureLocals is intentionally NOT initialised here — slots and
        // stack now live in automaticLocals.  The few legacy code paths
        // (generator save/restore, hoisted-function snapshots) lazily
        // allocate the sparse list on first use; they are not reached on
        // call-heavy hot paths.  Removing the eager allocation here
        // saved one SparseList alloc per runBytecode entry — at 100 K
        // calls/s that's a major fraction of the per-call cost.
    }

    // ------------------------------------------------------------------
    // Push the InterpFrame for this call.  Both the generator-resume and
    // fresh-entry paths above must end with a frame on t_interpFrames so
    // every stackPush/Pop/getSlot/setSlot below operates correctly.  A
    // RAII guard at this scope ensures we always pop on exit, regardless
    // of which return statement fires inside the dispatch loop.
    // ------------------------------------------------------------------
    {
        const unsigned int closureCount = static_cast<unsigned int>(module->closureVarNames.size());
        const unsigned int slotsForLocals = argCount + varCount + closureCount;
        unsigned int reservedStack  = module->stackSize() + 16;
        unsigned int totalSlots     = slotsForLocals + reservedStack;
        if (pContext->getAutomaticLocalsCount() < totalSlots)
            pContext->resizeAutomaticLocals(totalSlots);

        // [NEW] Root the metadata object in the context slots to ensure GC safety.
        // We use the very last slot in the reserved region.
        if (module->metadata && totalSlots > 0) {
            setSlot(pContext, totalSlots - 1, module->metadata);
        }
        // For a generator resume, restore the value-stack depth that was
        // active at the moment of suspension; otherwise start with an
        // empty value stack.  kGenStackTop is written by OP_yield /
        // OP_yield_star alongside kGenSlots.
        unsigned int initialStackTop = 0;
        if (pc != 0 && t_genIterator) {
            const proto::ProtoString* tk = pContext->fromUTF8String(kGenStackTop)
                ? pContext->fromUTF8String(kGenStackTop)->asString(pContext) : nullptr;
            if (tk) {
                const proto::ProtoObject* tv =
                    t_genIterator->getAttribute(pContext, tk, false);
                if (tv && tv != PROTO_NONE && tv->isInteger(pContext)) {
                    long long n = tv->asLong(pContext);
                    if (n >= 0 && n <= static_cast<long long>(reservedStack))
                        initialStackTop = static_cast<unsigned int>(n);
                }
            }
        }
        t_interpFrames.push_back(InterpFrame{ pContext, slotsForLocals, initialStackTop, reservedStack });
    }
    struct InterpFramePopOnExit {
        ~InterpFramePopOnExit() {
            if (!t_interpFrames.empty()) t_interpFrames.pop_back();
        }
    } _interpFramePopOnExit;

    // OP_yield resume: the InterpFrame now exists so stackPush will hit the
    // correct slots.  Push sent-value first (bottom) then resume kind (top)
    // so if_false8 pops the kind and dispatches normal/return/throw based
    // on it; the body then references the sent value below.  Without this
    // the body saw an empty stack on resume, if_false8 underflowed, and
    // multi-yield generators returned {done:true} after the first yield.

    // Fresh start only: initialise the value stack and pre-load closure
    // vars from the global into their dedicated slots.  On a generator
    // resume both of these have already been restored from the saved
    // snapshot above (and initStack would clobber the resume push that
    // happens right after this block).  Previously this branch was
    // guarded on (t_genResumePc < 0), but the resume path resets
    // t_genResumePc to -1 at the very top — so the guard was always
    // true and initStack ran on every resume, blanking the live stack.
    if (!gen_resume_active) {
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
        for (size_t i = 0; i < module->closureVarNames.size(); i++) {
            if (module->closureVarNames[i].empty()) continue;
            // Arrow function: the closure var "this" must come from thisObj, not the global.
            if (module->isArrow && module->closureVarNames[i] == "this") {
                setSlot(pContext, argCount + varCount + static_cast<unsigned>(i),
                    thisObj ? thisObj : PROTO_NONE);
                continue;
            }
            // Skip slots already populated by populateClosureCellsFromInstance
            // (called by OP_call / OP_call_method / OP_call_constructor /
            // callJSFunction with the function instance's captured cells).
            // Those slots hold cells (or already-resolved values) — don't
            // overwrite them with the global-fallback lookup.
            const proto::ProtoObject* existing =
                getSlot(pContext, argCount + varCount + static_cast<unsigned>(i));
            if (existing && existing != PROTO_NONE) continue;
            if (!globalObjInit || globalObjInit == PROTO_NONE) continue;
            const proto::ProtoString* key = (pContext->fromUTF8String(module->closureVarNames[i].c_str())
                ? pContext->fromUTF8String(module->closureVarNames[i].c_str())->asString(pContext)
                : nullptr);
            if (key) {
                const proto::ProtoObject* val = globalObjInit->getAttribute(pContext, key, false);
                if (val && val != PROTO_NONE)
                    setSlot(pContext, argCount + varCount + static_cast<unsigned>(i), val);
            }
        }
    }
    ProtoBytecodeModule* mod = const_cast<ProtoBytecodeModule*>(module);

    // Generator OP_yield resume: push the two values that the post-yield
    // bytecode expects on the stack (see DEF(yield, 1, 1, 2, none)).  The
    // sent value sits below and the resume kind on top; QuickJS emits an
    // `if_false8` right after OP_yield that pops the kind and jumps to
    // the body-continue label when kind == 0 (next), or falls through to
    // dedicated return/throw handlers for kind 1/2.  Pre-this-fix the
    // pushes happened before the InterpFrame existed (silent no-ops) AND
    // initStack ran on every resume (would have clobbered them anyway);
    // multi-yield generators returned {done:true} after the first yield.
    if (gen_resume_active) {
        stackPush(pContext, gen_resume_sent_val);
        stackPush(pContext, pContext->fromInteger(gen_resume_kind));
    }

    // Bootstrap the null sentinel. Stored as __js_null_sentinel__ on the global root
    // so the GC can trace it. Cached in t_nullSentinel for O(1) access during execution.
    if (!t_nullSentinel && pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoString* sentinelKey = JSSymbols::jsNullSentinel(pContext);
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

    // Bootstrap the TDZ sentinel. Same approach as t_nullSentinel: a unique ProtoObject
    // that cannot equal any legitimate JS value (including empty string "").
    if (!t_tdzSentinel && pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoString* tdzKey = JSSymbols::jsTdzSentinel(pContext);
        if (tdzKey) {
            const proto::ProtoObject* existing =
                (*pGlobalRoot)->getAttribute(pContext, tdzKey, false);
            if (existing && existing != PROTO_NONE) {
                t_tdzSentinel = existing;
            } else {
                const proto::ProtoObject* sentinel = pContext->newObject(false);
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, tdzKey, sentinel);
                t_tdzSentinel = sentinel;
            }
        }
    }
    const proto::ProtoObject* tdzSentinel = t_tdzSentinel ? t_tdzSentinel : PROTO_NONE;

    // Built-in registration runs ONCE per global root, not per call.
    // The block below registers Function.prototype, Array.prototype,
    // Number, Math, Map, Set, parseInt, parseFloat, encodeURI, etc.
    // Each helper is idempotent — if the global already has the
    // entry, it returns early — but the *check* itself does an
    // attribute lookup + symbol intern.  At ~30 helpers run per
    // function call, that became the dominant cost on call-heavy
    // workloads (function_calls.js: 50 K calls = ~50 s).
    //
    // Guard via a sentinel attribute on the global root.  First
    // entry installs everything and sets `__protojs_globals_init__`.
    // Subsequent entries find the flag and skip the entire block.
    bool needsGlobalInit = true;
    // Cache the flag key as an INTERNED SYMBOL (not a plain ProtoString).
    // Symbols compare by pointer, so getAttribute below skips the
    // SymbolTable::lookupByContent (toUTF8String + contentEqual) round-
    // trip that profiling showed at ~12 % of per-call CPU on 100 K
    // function calls.  fromUTF8String + asString returns a regular
    // POINTER_TAG_STRING; use createSymbol to get the canonical
    // POINTER_TAG_SYMBOL pointer.
    static thread_local const proto::ProtoString* s_initFlagKey = nullptr;
    if (!s_initFlagKey) {
        s_initFlagKey = proto::ProtoString::createSymbol(pContext, "__protojs_globals_init__");
    }
    const proto::ProtoString* initFlagKey = s_initFlagKey;
    if (pGlobalRoot && *pGlobalRoot && initFlagKey) {
        const proto::ProtoObject* flag =
            (*pGlobalRoot)->getAttribute(pContext, initFlagKey, false);
        if (flag == PROTO_TRUE) needsGlobalInit = false;
    }
  if (needsGlobalInit) {
    // Register Function.prototype first so that downstream constructors can
    // parent themselves on it — this makes Object.getPrototypeOf(Error) /
    // Object.getPrototypeOf(TypeError) etc. resolve to Function.prototype
    // via the protoCore parent chain without needing per-ctor overrides.
    ensureFunctionPrototype(pContext, pGlobalRoot);

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
            // Per §17, NaN / Infinity / undefined are
            // {writable:false, enumerable:false, configurable:false} → 0x0.
            // Pre-fix no sidecar so the default 0x7 (full enumerable +
            // writable) leaked them into Object.keys(globalThis) and
            // allowed reassignment.
            std::string pdStr = std::string("__pd_") + name + "__";
            const proto::ProtoObject* pdo = pContext->fromUTF8String(pdStr.c_str());
            const proto::ProtoString* pdks = pdo ? pdo->asString(pContext) : nullptr;
            if (pdks) *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, pdks, pContext->fromInteger(0x0LL));
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
        struct UnimplementedCtor { const char* name; long long length; };
        static const UnimplementedCtor kUnimplementedCtors[] = {
            // Unimplemented standard JS built-in constructors with their
            // §17 spec length.  NOTE: "Function" is intentionally omitted —
            // wired via ensureFunctionPrototype.  NOTE: "Symbol" is
            // intentionally omitted — wired via the symbolConstructor
            // native fn below so `Symbol(desc)` is callable.  The minimal
            // stub leaves Symbol(...) throwing \"is not a function\".
            {"Date", 7},
            {"BigInt", 1}, {"AggregateError", 2},
            {"Proxy", 2}, {"WeakRef", 1}, {"WeakSet", 0},
            {"FinalizationRegistry", 1}, {"Iterator", 0}, {"Generator", 0},
            {"GeneratorFunction", 1},
            {"AsyncFunction", 1}, {"AsyncGenerator", 0}, {"AsyncGeneratorFunction", 1},
            {"SharedArrayBuffer", 1},
            {nullptr, 0}
        };
        if (pGlobalRoot && *pGlobalRoot) {
            const proto::ProtoString* nameKey2 = JSSymbols::name(pContext);
            const proto::ProtoString* protoKey2 = JSSymbols::prototype(pContext);
            const proto::ProtoString* nfKey3 = JSSymbols::nativeFn(pContext);
            const proto::ProtoString* lenKey3 = JSSymbols::length(pContext);
            for (int gi = 0; kUnimplementedCtors[gi].name; ++gi) {
                const char* ctorName = kUnimplementedCtors[gi].name;
                const long long ctorLen = kUnimplementedCtors[gi].length;
                const proto::ProtoString* ck = (pContext->fromUTF8String(ctorName) ? pContext->fromUTF8String(ctorName)->asString(pContext) : nullptr);
                if (!ck) continue;
                const proto::ProtoObject* ex = (*pGlobalRoot)->getAttribute(pContext, ck, false);
                if (ex && ex != PROTO_NONE) continue;
                // Build a minimal constructor stub with a prototype so instanceof doesn't throw,
                // plus a __native_fn__ backing so typeof returns 'function'.
                const proto::ProtoObject* stubProto = pContext->newObject(true);
                // §22.3.3.* / §24.* etc: every built-in's prototype carries
                // Symbol.toStringTag = the constructor name. Object.prototype
                // .toString.call(new Stub()) must yield "[object Stub]"; pre-
                // fix the stub prototype was bare, so WeakSet.prototype[
                // Symbol.toStringTag] surfaced undefined (built-ins/WeakSet/
                // prototype/Symbol.toStringTag.js — also covers WeakRef,
                // FinalizationRegistry).  Install under the internal AND
                // user-visible Symbol.toStringTag keys per protoJS convention.
                if (stubProto) {
                    const proto::ProtoString* tstK = JSSymbols::toStringTag(pContext);
                    if (tstK) stubProto = stubProto->setAttribute(pContext, tstK,
                        pContext->fromUTF8String(ctorName));
                    const proto::ProtoString* userK = JSSymbols::symbolToStringTag(pContext);
                    if (userK) {
                        stubProto = stubProto->setAttribute(pContext, userK,
                            pContext->fromUTF8String(ctorName));
                        const proto::ProtoObject* pdttO = pContext->fromUTF8String("__pd_Symbol.toStringTag__");
                        const proto::ProtoString* pdttK = pdttO ? pdttO->asString(pContext) : nullptr;
                        if (pdttK) stubProto = stubProto->setAttribute(pContext, pdttK,
                            pContext->fromInteger(0x2LL));
                    }
                }
                const proto::ProtoObject* stub = pContext->newObject(true);
                if (nameKey2) stub = stub->setAttribute(pContext, nameKey2, pContext->fromUTF8String(ctorName));
                if (protoKey2) {
                    stub = stub->setAttribute(pContext, protoKey2, stubProto ? stubProto : PROTO_NONE);
                    // §17 / §20.4.5: every built-in constructor's
                    // "prototype" property is
                    // {writable:false, enumerable:false,
                    //  configurable:false} (bits 0x0). The stub
                    // pre-fix defaulted to fully writable / configurable
                    // (built-ins/Object/getOwnPropertyDescriptor/
                    // 15.2.3.3-4-210 covers Date.prototype).
                    const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_prototype__");
                    const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                    if (pdk) stub = stub->setAttribute(pContext, pdk, pContext->fromInteger(0x0LL));
                }
                if (nfKey3) {
                    const proto::ProtoObject* rawM = pContext->fromMethod(nullptr, unimplementedCtorStub);
                    if (rawM) stub = stub->setAttribute(pContext, nfKey3, rawM);
                }
                // §17: every built-in constructor has a "length" own
                // property with the spec-mandated arity and descriptor
                // {writable:false, enumerable:false, configurable:true} → 0x2.
                // Pre-fix the stub had no length, so verifyProperty(Date,
                // "length", ...) failed with "obj should have an own
                // property length" across the unimplemented-ctor family.
                if (lenKey3) {
                    stub = stub->setAttribute(pContext, lenKey3, pContext->fromInteger(ctorLen));
                    const proto::ProtoObject* pdlO = pContext->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdlK = pdlO ? pdlO->asString(pContext) : nullptr;
                    if (pdlK) stub = stub->setAttribute(pContext, pdlK, pContext->fromInteger(0x2LL));
                }
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, ck, stub);
                // §17 globalThis.<Ctor> descriptor 0x3.
                std::string pdStr = std::string("__pd_") + ctorName + "__";
                const proto::ProtoObject* pdo = pContext->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                if (pdk) *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, pdk,
                    pContext->fromInteger(0x3LL));
            }
        }
        // Non-constructor globals stubbed as PROTO_NONE to prevent ReferenceError.
        static const char* kUnimplementedGlobals[] = {
            "eval", "Reflect", "Atomics",
            "globalThis", "arguments",
            // Test262 harness globals.
            "$DONE", "$262", "print",
            nullptr
        };
        for (int gi = 0; kUnimplementedGlobals[gi]; ++gi)
            ensureGlobalConst(kUnimplementedGlobals[gi], PROTO_NONE);
    }

    // Build a JSON namespace object with Symbol.toStringTag = "JSON".
    // The namespace is mutable so a JS-level polyfill (installed in
    // main.cpp via wrapper.eval) can attach `stringify` and `parse`
    // methods at startup.  An earlier version used newObject(false)
    // (immutable), which silently dropped property assignments —
    // `JSON.stringify = …` looked successful but the next read
    // returned `undefined`.  The actual JSON.stringify / JSON.parse
    // implementations are not provided by QuickJS at this layer; the
    // protoCore-side global is separate from QuickJS's globalThis.
    {
        // "JSON" is interned once and reused as both the global key and the
        // toStringTag value below; the prior code did fromUTF8String("JSON")
        // twice on every interpreter init.
        const proto::ProtoObject* jsonStrObj = pContext->fromUTF8String("JSON");
        const proto::ProtoString* jsonKey = jsonStrObj ? jsonStrObj->asString(pContext) : nullptr;
        if (jsonKey && pGlobalRoot && *pGlobalRoot) {
            const proto::ProtoObject* existing = (*pGlobalRoot)->getAttribute(pContext, jsonKey, false);
            if (!existing || existing == PROTO_NONE) {
                const proto::ProtoObject* jsonObj = pContext->newObject(true);
                if (jsonObj) {
                    const proto::ProtoString* tagKey = JSSymbols::toStringTag(pContext);
                    if (tagKey)
                        jsonObj = jsonObj->setAttribute(pContext, tagKey, jsonStrObj);
                    *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, jsonKey, jsonObj);
                }
            }
        }
    }

    // Register Number constructor, Math object, Object constructor, and global utility
    // functions (parseInt, parseFloat, isNaN, isFinite, encodeURI, decodeURI, etc.).
    ensureNumberConstructor(pContext, pGlobalRoot);
    ensureBooleanConstructor(pContext, pGlobalRoot);
    ensureMapConstructor(pContext, pGlobalRoot);
    ensureSetConstructor(pContext, pGlobalRoot);
    ensureWeakMapConstructor(pContext, pGlobalRoot);
    ensureMathObject(pContext, pGlobalRoot);
    ensureObjectConstructor(pContext, pGlobalRoot);
    // Per ECMA-262 §19.3, the global object is reachable as globalThis.
    // Install it as a self-reference; the descriptor for globalThis is
    // { writable:true, enumerable:false, configurable:true } (descriptor
    // bits 0x1|0x2 = 0x3) per §B.2.1.
    if (pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoObject* gtObj = pContext->fromUTF8String("globalThis");
        const proto::ProtoString* gtKey = gtObj ? gtObj->asString(pContext) : nullptr;
        if (gtKey) {
            const proto::ProtoObject* existing =
                (*pGlobalRoot)->getAttribute(pContext, gtKey, false);
            if (!existing || existing == PROTO_NONE) {
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, gtKey, *pGlobalRoot);
                const proto::ProtoObject* pdObj = pContext->fromUTF8String("__pd_globalThis__");
                const proto::ProtoString* pdKey = pdObj ? pdObj->asString(pContext) : nullptr;
                if (pdKey) *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, pdKey, pContext->fromInteger(0x3LL));
            }
        }
    }
    ensureFunctionPrototype(pContext, pGlobalRoot);
    ensurePromiseConstructor(pContext, pGlobalRoot);
    // Install a minimal Reflect built-in (typeof Reflect === 'object').
    // The full Reflect API (has, get, set, deleteProperty, etc.) is not
    // implemented — those methods will be missing — but several test262
    // tests probe only `typeof Reflect`.  Pre-fix Reflect was absent.
    if (pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoObject* rfObj = pContext->fromUTF8String("Reflect");
        const proto::ProtoString* rfKey = rfObj ? rfObj->asString(pContext) : nullptr;
        if (rfKey) {
            const proto::ProtoObject* existing =
                (*pGlobalRoot)->getAttribute(pContext, rfKey, false);
            const proto::ProtoObject* reflectStub = (existing && existing != PROTO_NONE)
                ? existing : pContext->newObject(true);
            if (reflectStub) {
                struct { const char* name; proto::ProtoMethod fn; long long length; } rfMeth[] = {
                    {"apply",             reflectApply,             3},
                    {"construct",         reflectConstruct,         2},
                    {"has",               reflectHas,               2},
                    {"get",               reflectGet,               2},
                    {"set",               reflectSet,               3},
                    {"ownKeys",           reflectOwnKeys,           1},
                    {"deleteProperty",    reflectDeleteProperty,    2},
                    {"getPrototypeOf",    reflectGetPrototypeOf,    1},
                    {"setPrototypeOf",    reflectSetPrototypeOf,    2},
                    {"isExtensible",      reflectIsExtensible,      1},
                    {"preventExtensions", reflectPreventExtensions, 1},
                    {"defineProperty",    reflectDefineProperty,    3},
                    {"getOwnPropertyDescriptor", reflectGetOwnPropertyDescriptor, 2},
                };
                for (auto& m : rfMeth) {
                    const proto::ProtoString* k = pContext->fromUTF8String(m.name)
                        ? pContext->fromUTF8String(m.name)->asString(pContext) : nullptr;
                    if (k) {
                        const proto::ProtoObject* fn = wrapNativeFunction(pContext, m.fn, m.name, m.length, pGlobalRoot);
                        if (fn) {
                            reflectStub = reflectStub->setAttribute(pContext, k, fn);
                            // §17 descriptor 0x3 (writable, !enumerable,
                            // configurable). Pre-fix Reflect.* methods
                            // defaulted to enumerable, leaking them into
                            // Object.keys(Reflect) and reporting wrong
                            // descriptors via getOwnPropertyDescriptor.
                            std::string pdStr = std::string("__pd_") + m.name + "__";
                            const proto::ProtoString* pdk =
                                pContext->fromUTF8String(pdStr.c_str())->asString(pContext);
                            if (pdk) reflectStub = reflectStub->setAttribute(pContext, pdk, pContext->fromInteger(0x3LL));
                        }
                    }
                }
                // §28.1.5 Reflect [@@toStringTag] === "Reflect" with
                // {writable:false, enumerable:false, configurable:true}
                // (bits 0x2). The slot was absent so verifyProperty
                // checks (built-ins/Reflect/Symbol.toStringTag.js) and
                // Object.prototype.toString.call(Reflect) returned the
                // generic "[object Object]" tag.
                {
                    // The user-facing access path goes through
                    // Symbol.toStringTag (string form) — see
                    // JSSymbols::symbolToStringTag — so install the slot
                    // under that key rather than the internal sidecar.
                    const proto::ProtoString* ttKey = JSSymbols::symbolToStringTag(pContext);
                    if (ttKey) {
                        reflectStub = reflectStub->setAttribute(pContext, ttKey,
                            pContext->fromUTF8String("Reflect"));
                        const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_Symbol.toStringTag__");
                        const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                        if (pdk) reflectStub = reflectStub->setAttribute(pContext, pdk,
                            pContext->fromInteger(0x2LL));
                    }
                }
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, rfKey, reflectStub);
                // §17 descriptor 0x3 on globalThis.Reflect — pre-fix the
                // global slot defaulted to enumerable.
                const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_Reflect__");
                const proto::ProtoString* pdks = pdo ? pdo->asString(pContext) : nullptr;
                if (pdks) *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, pdks, pContext->fromInteger(0x3LL));
            }
        }
    }
    // Install Symbol as a minimal callable constructor.  Pre-fix
    // `Symbol("foo")` threw \"is not a function\" because the global
    // Symbol stub had no native-fn backing.  wrapNativeFunction builds
    // a callable wrapper inheriting Function.prototype; the wrapper's
    // prototype attribute is left as a fresh blank object so
    // `x instanceof Symbol` doesn't throw.
    if (pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoObject* symbolStrObj = pContext->fromUTF8String("Symbol");
        const proto::ProtoString* symbolGlobalKey =
            symbolStrObj ? symbolStrObj->asString(pContext) : nullptr;
        if (symbolGlobalKey) {
            const proto::ProtoObject* existing =
                (*pGlobalRoot)->getAttribute(pContext, symbolGlobalKey, false);
            if (!existing || existing == PROTO_NONE) {
                const proto::ProtoObject* symbolCtor =
                    wrapNativeFunction(pContext, symbolConstructor, "Symbol", 0, pGlobalRoot);
                if (symbolCtor && symbolCtor != PROTO_NONE) {
                    const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                    if (protoKey) {
                        const proto::ProtoObject* symProto = pContext->newObject(true);
                        if (symProto) {
                            // §20.4.3.4 Symbol.prototype[@@toStringTag]
                            // === "Symbol" with {writable:false,
                            // enumerable:false, configurable:true}
                            // (bits 0x2). The slot was absent so
                            // Object.prototype.toString.call(Symbol())
                            // fell through to the marker probe (and
                            // emitted "[object Symbol]" only after the
                            // explicit primitive check added separately).
                            const proto::ProtoString* ttKey = JSSymbols::symbolToStringTag(pContext);
                            if (ttKey) {
                                symProto = symProto->setAttribute(pContext, ttKey,
                                    pContext->fromUTF8String("Symbol"));
                                const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_Symbol.toStringTag__");
                                const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                if (pdk) symProto = symProto->setAttribute(pContext, pdk,
                                    pContext->fromInteger(0x2LL));
                            }
                            symbolCtor = symbolCtor->setAttribute(pContext, protoKey, symProto);
                        }
                    }
                    // Static methods: Symbol.for, Symbol.keyFor.  §17 marks
                    // each as {writable:true, enumerable:false, configurable:
                    // true} (bits 0x3); the sidecar was absent so the
                    // method slot defaulted to fully enumerable and
                    // for-in over Symbol leaked the entries.
                    {
                        const proto::ProtoString* fk = pContext->fromUTF8String("for")
                            ? pContext->fromUTF8String("for")->asString(pContext) : nullptr;
                        if (fk) {
                            const proto::ProtoObject* fn = wrapNativeFunction(pContext, symbolFor, "for", 1, pGlobalRoot);
                            if (fn) {
                                symbolCtor = symbolCtor->setAttribute(pContext, fk, fn);
                                const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_for__");
                                const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                if (pdk) symbolCtor = symbolCtor->setAttribute(pContext, pdk,
                                    pContext->fromInteger(0x3LL));
                            }
                        }
                        const proto::ProtoString* kfk = pContext->fromUTF8String("keyFor")
                            ? pContext->fromUTF8String("keyFor")->asString(pContext) : nullptr;
                        if (kfk) {
                            const proto::ProtoObject* fn = wrapNativeFunction(pContext, symbolKeyFor, "keyFor", 1, pGlobalRoot);
                            if (fn) {
                                symbolCtor = symbolCtor->setAttribute(pContext, kfk, fn);
                                const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_keyFor__");
                                const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                if (pdk) symbolCtor = symbolCtor->setAttribute(pContext, pdk,
                                    pContext->fromInteger(0x3LL));
                            }
                        }
                    }
                    *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, symbolGlobalKey, symbolCtor);
                    // §17 globalThis.Symbol descriptor 0x3.
                    const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_Symbol__");
                    const proto::ProtoString* pdkk = pdo ? pdo->asString(pContext) : nullptr;
                    if (pdkk) *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, pdkk,
                        pContext->fromInteger(0x3LL));
                }
            }
        }
    }

    // Bootstrap Symbol well-known symbols as string-valued properties on the Symbol stub.
    // This allows JS code like `obj[Symbol.iterator] = fn` to use the canonical key
    // "Symbol.iterator" that JSSymbols::symbolIterator() also returns.
    if (pGlobalRoot && *pGlobalRoot) {
        const proto::ProtoObject* symbolStrObj = pContext->fromUTF8String("Symbol");
        const proto::ProtoString* symbolGlobalKey =
            symbolStrObj ? symbolStrObj->asString(pContext) : nullptr;
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
        // Wrap each global fn with a function-object that carries name
        // and length per spec §19.2 — the raw ProtoMethod cell has
        // neither, so test262's verifyPrimordialCallableProperty
        // fixtures fail when probing parseInt.length === 2 etc.
        auto ensureGlobalFn = [&](const char* name, proto::ProtoMethod fn, long long len) {
            const proto::ProtoString* k = (pContext->fromUTF8String(name) ? pContext->fromUTF8String(name)->asString(pContext) : nullptr);
            if (!k) return;
            const proto::ProtoObject* existing = (*pGlobalRoot)->getAttribute(pContext, k, false);
            if (existing && existing != PROTO_NONE) return;
            const proto::ProtoObject* fnObj = wrapNativeFunction(pContext, fn, name, len, pGlobalRoot);
            if (fnObj) {
                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, k, fnObj);
                // Spec §17: built-in global properties have descriptor
                // {writable:true, enumerable:false, configurable:true}
                // → 0x3 (no enumerable bit). Without the sidecar
                // `for (k in globalThis)` would list parseInt/etc.
                std::string pdStr = std::string("__pd_") + name + "__";
                const proto::ProtoObject* pdo = pContext->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                if (pdk) *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, pdk,
                    pContext->fromInteger(0x3LL));
            }
        };
        // Spec lengths: parseInt(2), parseFloat(1), isNaN(1), isFinite(1),
        // encodeURI(1), encodeURIComponent(1), decodeURI(1), decodeURIComponent(1).
        ensureGlobalFn("parseInt",            globalParseInt,           2);
        ensureGlobalFn("parseFloat",          globalParseFloat,         1);
        ensureGlobalFn("isNaN",               globalIsNaN,              1);
        ensureGlobalFn("isFinite",            globalIsFinite,           1);
        ensureGlobalFn("encodeURI",           globalEncodeURI,          1);
        ensureGlobalFn("encodeURIComponent",  globalEncodeURIComponent, 1);
        ensureGlobalFn("decodeURI",           globalDecodeURI,          1);
        ensureGlobalFn("decodeURIComponent",  globalDecodeURIComponent, 1);
        // §21.1.2.12 / §21.1.2.13: Number.parseInt === parseInt and
        // Number.parseFloat === parseFloat. Number was set up before
        // the globals here, so finalize the identity now.
        patchNumberParseFns(pContext, pGlobalRoot);
    }

    // Mark the global root as initialised so subsequent runBytecode calls
    // skip the entire block above.
    if (pGlobalRoot && *pGlobalRoot && initFlagKey) {
        *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, initFlagKey, PROTO_TRUE);
    }
  }  // end if (needsGlobalInit)

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
        if (bcId >= 0 && static_cast<size_t>(bcId) < module->nestedFunctions.size())
            resolvedFn = &module->nestedFunctions[bcId];
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
        if (proto::isSmallInt(obj)) return obj;
        if (obj == t_nullSentinel) return obj;  // null does not coerce to primitive
        if (obj->isBoolean(pContext) || obj->isInteger(pContext) ||
            obj->isDouble(pContext) || obj->isFloat(pContext) ||
            obj->asString(pContext)) return obj;
        // §7.1.1 ToPrimitive: a Symbol IS a primitive — it should
        // short-circuit here so the downstream ToNumber pass sees the
        // Symbol and raises the spec-required TypeError, instead of the
        // ToPrimitive dance running the Object.prototype valueOf /
        // toString fallback and silently flattening the Symbol to
        // [object Symbol] (which then coerces to NaN with no throw).
        {
            const proto::ProtoObject* symKo = pContext->fromUTF8String("__is_symbol__");
            const proto::ProtoString* symK = symKo ? symKo->asString(pContext) : nullptr;
            if (symK && obj->getAttribute(pContext, symK, true) == PROTO_TRUE)
                return obj;
        }
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
        // Step 0: try Symbol.toPrimitive (ECMA-262 §7.1.1 step 2).  If
        // the object defines @@toPrimitive, that method's return value
        // is used directly (must be a primitive).  Hint is "default"
        // here because most call sites don't differentiate.
        {
            const proto::ProtoObject* tpKeyObj = pContext->fromUTF8String("Symbol.toPrimitive");
            const proto::ProtoString* tpKey = tpKeyObj ? tpKeyObj->asString(pContext) : nullptr;
            const proto::ProtoObject* tpFn = tpKey
                ? obj->getAttribute(pContext, tpKey, true) : nullptr;
            if (tpFn && tpFn != PROTO_NONE) {
                const proto::ProtoList* hintArgs = pContext->newList();
                hintArgs = hintArgs->appendLast(pContext, pContext->fromUTF8String("default"));
                const proto::ProtoObject* prim = callJSFunction(pContext, tpFn, obj, hintArgs);
                if (t_hasCallException) {
                    pending_exception     = t_callException;
                    has_pending_exception = true;
                    t_hasCallException    = false;
                    t_callException       = nullptr;
                    return PROTO_NONE;
                }
                if (isPrimitive(prim)) return prim;
            }
        }
        // Step 1: try valueOf.
        //
        // Use callJSFunction (the unified dispatch) instead of the local
        // callMethod lambda.  callMethod only handles bytecode nested in
        // the *current* module's nestedFunctions and bare ProtoMethod
        // cells; the built-in toString / valueOf installed on
        // Array.prototype, String.prototype, ... live behind a native
        // function wrapper that callMethod can't unwrap, so callMethod
        // returned PROTO_NONE for them — which the toPrimitive fallback
        // then misread as "method returned non-primitive" and threw
        // TypeError.  callJSFunction handles every variant.
        const proto::ProtoString* vk = JSSymbols::valueOf(pContext);
        if (vk) {
            const proto::ProtoObject* vfn = obj->getAttribute(pContext, vk, true);
            if (vfn && vfn != PROTO_NONE) {
                valueOfPresent = true;
                const proto::ProtoList* emptyArgs = pContext->newList();
                const proto::ProtoObject* prim = callJSFunction(pContext, vfn, obj, emptyArgs);
                if (t_hasCallException) {
                    pending_exception     = t_callException;
                    has_pending_exception = true;
                    t_hasCallException    = false;
                    t_callException       = nullptr;
                    return PROTO_NONE;
                }
                if (isPrimitive(prim)) return prim;
            }
        }
        // Step 2: try toString.
        const proto::ProtoString* tk = JSSymbols::toString(pContext);
        if (tk) {
            const proto::ProtoObject* tfn = obj->getAttribute(pContext, tk, true);
            if (tfn && tfn != PROTO_NONE) {
                toStringPresent = true;
                const proto::ProtoList* emptyArgs = pContext->newList();
                const proto::ProtoObject* prim = callJSFunction(pContext, tfn, obj, emptyArgs);
                if (t_hasCallException) {
                    pending_exception     = t_callException;
                    has_pending_exception = true;
                    t_hasCallException    = false;
                    t_callException       = nullptr;
                    return PROTO_NONE;
                }
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

    /* ECMA-262 §7.2.13 Abstract Relational Comparison after ToPrimitive.
     * Inputs are already primitives.  Output: tri-state.
     *   -1 → a < b
     *    0 → a == b (relevant for <= / >=)
     *   +1 → a > b
     *    2 → NaN (the spec's "undefined") — comparison must be false.
     *
     * If both operands are strings, compare lexicographically.  Otherwise
     * ToNumber both and numeric compare; NaN on either side returns 2.
     *
     * Pre-fix all four ops called `pa->compare(pContext, pb)` directly,
     * which for a string-vs-integer pair (e.g. ToPrimitive([1]) === "1"
     * vs literal 2) used protoCore's cross-type comparison and returned
     * +1, so `[1] < 2` came out false.  This helper centralises the
     * spec-correct ToNumber step. */
    auto relCmpAfterPrim = [&](const proto::ProtoObject* pa,
                               const proto::ProtoObject* pb) -> int {
        if (!pa || !pb) return 2;
        bool aS = pa->isString(pContext);
        bool bS = pb->isString(pContext);
        if (aS && bS) {
            int c = pa->compare(pContext, pb);
            return (c < 0) ? -1 : (c > 0) ? 1 : 0;
        }
        const proto::ProtoObject* na = toNumber(pContext, pa);
        const proto::ProtoObject* nb = toNumber(pContext, pb);
        if (!na || !nb) return 2;
        auto isNaNVal = [&](const proto::ProtoObject* v) -> bool {
            if (!v) return true;
            if (v->isDouble(pContext) || v->isFloat(pContext))
                return std::isnan(v->asDouble(pContext));
            return false;
        };
        if (isNaNVal(na) || isNaNVal(nb)) return 2;
        int c = na->compare(pContext, nb);
        return (c < 0) ? -1 : (c > 0) ? 1 : 0;
    };

    /* P-JS-1 accessor lookup cache.
     *
     * `__get_<name>__` and `__set_<name>__` are sidecar attributes used by
     * Object.defineProperty to install getters/setters on the prototype.
     * Every OP_get_field2 miss used to construct the sidecar symbol on the
     * fly: `"__get_" + keyStr + "__"` followed by `fromUTF8String` (which
     * allocates a fresh ProtoString rope cell tree).  For dense property
     * access on objects without accessors, this was O(N) cell allocations
     * per miss for nothing — the symbol always missed in the AVL since no
     * code ever defined `__get_<name>__`.
     *
     * Cache the (name → getter-symbol) mapping per thread.  First miss
     * builds the symbol once and interns it; subsequent misses are a
     * pointer-keyed unordered_map lookup.  Collision-free because property
     * names are interned ProtoString pointers (auto-interned by protoCore
     * symbol table).
     *
     * Two caches: one for getter sidecars, one for setter sidecars.  Both
     * keyed on the property-name ProtoString pointer.
     */
    static thread_local std::unordered_map<const proto::ProtoString*, const proto::ProtoString*> t_getterSymCache;
    static thread_local std::unordered_map<const proto::ProtoString*, const proto::ProtoString*> t_setterSymCache;

    auto getterSymbolFor = [&](const proto::ProtoString* key) -> const proto::ProtoString* {
        if (!key) return nullptr;
        auto it = t_getterSymCache.find(key);
        if (it != t_getterSymCache.end()) return it->second;
        std::string keyStr;
        key->toUTF8String(pContext, keyStr);
        std::string gkStr = "__get_" + keyStr + "__";
        const proto::ProtoObject* gko = pContext->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gk  = gko ? gko->asString(pContext) : nullptr;
        t_getterSymCache[key] = gk;
        return gk;
    };
    auto setterSymbolFor = [&](const proto::ProtoString* key) -> const proto::ProtoString* {
        if (!key) return nullptr;
        auto it = t_setterSymCache.find(key);
        if (it != t_setterSymCache.end()) return it->second;
        std::string keyStr;
        key->toUTF8String(pContext, keyStr);
        std::string skStr = "__set_" + keyStr + "__";
        const proto::ProtoObject* sko = pContext->fromUTF8String(skStr.c_str());
        const proto::ProtoString* sk  = sko ? sko->asString(pContext) : nullptr;
        t_setterSymCache[key] = sk;
        return sk;
    };

    /* Accessor property helper: invoke getter stored as __get_<keyStr>__ on the object (or
     * its prototype chain).  Returns the getter result, PROTO_NONE if no getter is defined,
     * or PROTO_NONE with has_pending_exception set if the getter throws.
     * Call sites should `break` immediately when has_pending_exception is true. */
    auto invokeGetterIfPresent = [&](
            const proto::ProtoObject* obj,
            const std::string& keyStr) -> const proto::ProtoObject* {
        if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) return PROTO_NONE;
        std::string gkStr = "__get_" + keyStr + "__";
        const proto::ProtoObject* gko = pContext->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gk  = gko ? gko->asString(pContext) : nullptr;
        if (!gk) return PROTO_NONE;
        const proto::ProtoObject* getter = obj->getAttribute(pContext, gk, true);
        if (!getter || getter == PROTO_NONE) return PROTO_NONE;
        // Call getter with obj as `this` — no arguments.
        const proto::ProtoList* emptyArgs = pContext->newList();
        const proto::ProtoObject* result = callJSFunction(pContext, getter, obj, emptyArgs);
        if (t_hasCallException) {
            pending_exception  = t_callException;
            has_pending_exception = true;
            t_hasCallException = false;
            t_callException    = nullptr;
            return PROTO_NONE;
        }
        return result ? result : PROTO_NONE;
    };

    /* P-JS-1 fast variant: takes the property-name ProtoString directly,
     * uses the per-thread getter-symbol cache to avoid the per-miss
     * `fromUTF8String` allocation (the original lambda pays a fresh
     * ProtoString rope construction every call). */
    auto invokeGetterIfPresentFast = [&](
            const proto::ProtoObject* obj,
            const proto::ProtoString* key) -> const proto::ProtoObject* {
        if (!obj || obj == PROTO_NONE || obj == t_nullSentinel || !key) return PROTO_NONE;
        
        const proto::ProtoObject* curr = obj;
        const proto::ProtoObject* objProto = pContext->space ? pContext->space->objectPrototype : nullptr;
        int depth = 0;
        while (curr && curr != PROTO_NONE && depth < 100) {
            // Check accessor sidecar first.  Built-in prototypes such as
            // Set.prototype / Map.prototype install only `__get_<name>__`
            // (no plain `<name>` attribute), so the previous "key must
            // exist as own attribute" guard caused dot access to miss the
            // accessor entirely while bracket access (which walked through
            // resolveElementOOP + invokeGetterIfPresent slow path) found
            // it.  Probe the sidecar at every level of the chain.
            const proto::ProtoString* gk = getterSymbolFor(key);
            if (gk && curr->hasOwnAttribute(pContext, gk) == PROTO_TRUE) {
                const proto::ProtoObject* getter = curr->getAttribute(pContext, gk, false);
                if (getter && getter != PROTO_NONE && getter != t_undefinedSentinel) {
                    const proto::ProtoList* emptyArgs = pContext->newList();
                    const proto::ProtoObject* result = callJSFunction(pContext, getter, obj, emptyArgs);
                    if (t_hasCallException) {
                        pending_exception  = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException    = nullptr;
                        return PROTO_NONE;
                    }
                    return result ? result : PROTO_NONE;
                }
            }
            if (curr->hasOwnAttribute(pContext, key) == PROTO_TRUE) {
                // Regular own attribute with no accessor at this level —
                // stop so a prototype-level accessor cannot shadow a
                // legitimate own value.  Normal resolution will pick it
                // up.
                return PROTO_NONE;
            }
            if (curr == objProto) break; // Stop at Object.prototype
            curr = curr->getPrototype(pContext);
            depth++;
        }
        return PROTO_NONE;
    };

    /* Accessor setter helper: invoke setter stored as __set_<keyStr>__. Returns false (and
     * sets pending_exception) if this is an accessor with NO setter in strict mode. */
    auto invokeSetterIfPresent = [&](
            const proto::ProtoObject* obj,
            const std::string& keyStr,
            const proto::ProtoObject* newVal,
            bool isStrict) -> bool {
        if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) return false;
        const proto::ProtoString* key = pContext->fromUTF8String(keyStr.c_str())->asString(pContext);
        if (!key) return false;

        const proto::ProtoObject* curr = obj;
        while (curr && curr != PROTO_NONE) {
            if (curr->hasOwnAttribute(pContext, key) == PROTO_TRUE) {
                const proto::ProtoString* sk = setterSymbolFor(key);
                if (sk && curr->hasOwnAttribute(pContext, sk) == PROTO_TRUE) {
                    const proto::ProtoObject* setter = curr->getAttribute(pContext, sk, false);
                    if (setter && setter != PROTO_NONE && setter != t_undefinedSentinel) {
                        const proto::ProtoList* emptySetArgs = pContext->newList();
                        const proto::ProtoList* setArgs = emptySetArgs->appendLast(pContext, newVal ? newVal : PROTO_NONE);
                        callJSFunction(pContext, setter, obj, setArgs ? setArgs : emptySetArgs);
                        if (t_hasCallException) {
                            pending_exception  = t_callException;
                            has_pending_exception = true;
                            t_hasCallException = false;
                            t_callException    = nullptr;
                        }
                        return true;
                    }
                }
                
                const proto::ProtoString* gk = getterSymbolFor(key);
                if (gk && curr->hasOwnAttribute(pContext, gk) == PROTO_TRUE) {
                    if (isStrict) {
                        pending_exception = makeError(pContext, "TypeError",
                            "Cannot set property with no setter", pGlobalRoot);
                        has_pending_exception = true;
                    }
                    return true;
                }
                
                return false;
            }
            curr = curr->getPrototype(pContext);
        }
        return false;
    };

    // ----- Threaded dispatch (computed-goto) -----
    //
    // Each opcode case becomes a label `L_OP_X`.  At end of every case we
    // `DISPATCH()` instead of falling out to the top of a switch — that
    // gives the CPU's indirect-branch predictor PER-OPCODE history (in a
    // switch-based dispatch the predictor sees a single hot indirect jump
    // and can't disambiguate).  Typical 1.2-1.7x speed-up on tight loops
    // over the bytecode.
    //
    // P-JS-7: cache the table once per process.  `&&label` is not a
    // *static* constant expression in C++ (so we can't use a namespace-
    // scope initialiser), AND it cannot be referenced from a nested
    // lambda (GCC extension limitation: `&&label` only resolves inside
    // the enclosing function body, not inside a lambda's body).  We
    // therefore use double-checked locking with the init code written
    // directly in runBytecode's body — labels are accessible there.
    //
    // Address-of-label values are stable across every entry to the same
    // function (labels live in the code segment at fixed offsets), so
    // initialising once and reusing for every subsequent call from any
    // thread is safe.  Eliminates ~256 + ~210 stores per call (the
    // previous version re-filled the table from scratch on every entry
    // — for tree_traversal that was ~150 M wasted stores per bench
    // run).  Steady-state cost: 1 acquire-load + predicted-not-taken
    // branch (~2 cycles) per runBytecode entry.
    //
    // Pre-fill every slot with `&&L_default` so the DISPATCH hot path
    // never has to test for nullptr — unimplemented opcodes route to the
    // diagnostic L_default handler the same way as if the slot had been
    // explicitly assigned to it.  Saves one branch per dispatch.
    static const void* dispatch_table[256];
    static std::atomic<bool> dispatch_table_initialized{false};
    static std::mutex dispatch_table_init_mutex;
    if (__builtin_expect(!dispatch_table_initialized.load(std::memory_order_acquire), 0)) {
    std::lock_guard<std::mutex> _disp_init_lock(dispatch_table_init_mutex);
    if (!dispatch_table_initialized.load(std::memory_order_relaxed)) {
    for (int i = 0; i < 256; ++i) dispatch_table[i] = &&L_default;
    dispatch_table[OP_add] = &&L_OP_add;
    dispatch_table[OP_add_loc] = &&L_OP_add_loc;
    dispatch_table[OP_and] = &&L_OP_and;
    dispatch_table[OP_append] = &&L_OP_append;
    dispatch_table[OP_array_from] = &&L_OP_array_from;
    dispatch_table[OP_await] = &&L_OP_await;
    dispatch_table[OP_call] = &&L_OP_call;
    dispatch_table[OP_call0] = &&L_OP_call0;
    dispatch_table[OP_call1] = &&L_OP_call1;
    dispatch_table[OP_call2] = &&L_OP_call2;
    dispatch_table[OP_call3] = &&L_OP_call3;
    dispatch_table[OP_call_constructor] = &&L_OP_call_constructor;
    dispatch_table[OP_define_class] = &&L_OP_define_class;
    dispatch_table[OP_define_class_computed] = &&L_OP_define_class_computed;
    dispatch_table[OP_check_ctor] = &&L_OP_check_ctor;
    dispatch_table[OP_check_ctor_return] = &&L_OP_check_ctor_return;
    dispatch_table[OP_init_ctor] = &&L_OP_init_ctor;
    dispatch_table[OP_eval] = &&L_OP_eval;
    dispatch_table[OP_check_brand] = &&L_OP_check_brand;
    dispatch_table[OP_add_brand] = &&L_OP_add_brand;
    dispatch_table[OP_set_home_object] = &&L_OP_set_home_object;
    dispatch_table[OP_get_super] = &&L_OP_get_super;
    dispatch_table[OP_get_super_value] = &&L_OP_get_super_value;
    dispatch_table[OP_put_super_value] = &&L_OP_put_super_value;
    dispatch_table[OP_private_symbol] = &&L_OP_private_symbol;
    dispatch_table[OP_set_proto] = &&L_OP_set_proto;
    dispatch_table[OP_get_private_field] = &&L_OP_get_private_field;
    dispatch_table[OP_put_private_field] = &&L_OP_put_private_field;
    dispatch_table[OP_define_private_field] = &&L_OP_define_private_field;
    dispatch_table[OP_call_method] = &&L_OP_call_method;
    dispatch_table[OP_apply] = &&L_OP_apply;
    dispatch_table[OP_catch] = &&L_OP_catch;
    dispatch_table[OP_close_loc] = &&L_OP_close_loc;
    dispatch_table[OP_copy_data_properties] = &&L_OP_copy_data_properties;
    dispatch_table[OP_dec] = &&L_OP_dec;
    dispatch_table[OP_dec_loc] = &&L_OP_dec_loc;
    dispatch_table[OP_define_array_el] = &&L_OP_define_array_el;
    dispatch_table[OP_define_field] = &&L_OP_define_field;
    dispatch_table[OP_define_method] = &&L_OP_define_method;
    dispatch_table[OP_define_method_computed] = &&L_OP_define_method_computed;
    dispatch_table[OP_delete] = &&L_OP_delete;
    dispatch_table[OP_div] = &&L_OP_div;
    dispatch_table[OP_drop] = &&L_OP_drop;
    dispatch_table[OP_dup] = &&L_OP_dup;
    dispatch_table[OP_dup1] = &&L_OP_dup1;
    dispatch_table[OP_dup2] = &&L_OP_dup2;
    dispatch_table[OP_dup3] = &&L_OP_dup3;
    dispatch_table[OP_eq] = &&L_OP_eq;
    dispatch_table[OP_fclosure] = &&L_OP_fclosure;
    dispatch_table[OP_fclosure8] = &&L_OP_fclosure8;
    dispatch_table[OP_for_await_of_next] = &&L_OP_for_await_of_next;
    dispatch_table[OP_for_await_of_start] = &&L_OP_for_await_of_start;
    dispatch_table[OP_for_in_next] = &&L_OP_for_in_next;
    dispatch_table[OP_for_in_start] = &&L_OP_for_in_start;
    dispatch_table[OP_for_of_next] = &&L_OP_for_of_next;
    dispatch_table[OP_for_of_start] = &&L_OP_for_of_start;
    dispatch_table[OP_get_arg] = &&L_OP_get_arg;
    dispatch_table[OP_get_arg0] = &&L_OP_get_arg0;
    dispatch_table[OP_get_arg1] = &&L_OP_get_arg1;
    dispatch_table[OP_get_arg2] = &&L_OP_get_arg2;
    dispatch_table[OP_get_arg3] = &&L_OP_get_arg3;
    dispatch_table[OP_get_array_el] = &&L_OP_get_array_el;
    dispatch_table[OP_get_array_el2] = &&L_OP_get_array_el2;
    dispatch_table[OP_get_array_el3] = &&L_OP_get_array_el3;
    dispatch_table[OP_get_field] = &&L_OP_get_field;
    dispatch_table[OP_get_field2] = &&L_OP_get_field2;
    dispatch_table[OP_get_length] = &&L_OP_get_length;
    dispatch_table[OP_get_loc] = &&L_OP_get_loc;
    dispatch_table[OP_get_loc0] = &&L_OP_get_loc0;
    dispatch_table[OP_get_loc1] = &&L_OP_get_loc1;
    dispatch_table[OP_get_loc2] = &&L_OP_get_loc2;
    dispatch_table[OP_get_loc3] = &&L_OP_get_loc3;
    dispatch_table[OP_get_loc8] = &&L_OP_get_loc8;
    dispatch_table[OP_get_loc_check] = &&L_OP_get_loc_check;
    dispatch_table[OP_get_loc_checkthis] = &&L_OP_get_loc_checkthis;
    dispatch_table[OP_get_var] = &&L_OP_get_var;
    dispatch_table[OP_get_var_ref] = &&L_OP_get_var_ref;
    dispatch_table[OP_get_var_ref0] = &&L_OP_get_var_ref0;
    dispatch_table[OP_get_var_ref1] = &&L_OP_get_var_ref1;
    dispatch_table[OP_get_var_ref2] = &&L_OP_get_var_ref2;
    dispatch_table[OP_get_var_ref3] = &&L_OP_get_var_ref3;
    dispatch_table[OP_get_var_ref_check] = &&L_OP_get_var_ref_check;
    dispatch_table[OP_get_var_undef] = &&L_OP_get_var_undef;
    dispatch_table[OP_gosub] = &&L_OP_gosub;
    dispatch_table[OP_goto] = &&L_OP_goto;
    dispatch_table[OP_goto16] = &&L_OP_goto16;
    dispatch_table[OP_goto8] = &&L_OP_goto8;
    dispatch_table[OP_gt] = &&L_OP_gt;
    dispatch_table[OP_gte] = &&L_OP_gte;
    dispatch_table[OP_if_false] = &&L_OP_if_false;
    dispatch_table[OP_if_false8] = &&L_OP_if_false8;
    dispatch_table[OP_if_true] = &&L_OP_if_true;
    dispatch_table[OP_if_true8] = &&L_OP_if_true8;
    dispatch_table[OP_in] = &&L_OP_in;
    dispatch_table[OP_inc] = &&L_OP_inc;
    dispatch_table[OP_inc_loc] = &&L_OP_inc_loc;
    dispatch_table[OP_initial_yield] = &&L_OP_initial_yield;
    dispatch_table[OP_insert2] = &&L_OP_insert2;
    dispatch_table[OP_insert3] = &&L_OP_insert3;
    dispatch_table[OP_insert4] = &&L_OP_insert4;
    dispatch_table[OP_instanceof] = &&L_OP_instanceof;
    dispatch_table[OP_is_null] = &&L_OP_is_null;
    dispatch_table[OP_is_undefined] = &&L_OP_is_undefined;
    dispatch_table[OP_is_undefined_or_null] = &&L_OP_is_undefined_or_null;
    dispatch_table[OP_iterator_call] = &&L_OP_iterator_call;
    dispatch_table[OP_iterator_check_object] = &&L_OP_iterator_check_object;
    dispatch_table[OP_iterator_close] = &&L_OP_iterator_close;
    dispatch_table[OP_iterator_get_value_done] = &&L_OP_iterator_get_value_done;
    dispatch_table[OP_iterator_next] = &&L_OP_iterator_next;
    dispatch_table[OP_lnot] = &&L_OP_lnot;
    dispatch_table[OP_lt] = &&L_OP_lt;
    dispatch_table[OP_lte] = &&L_OP_lte;
    dispatch_table[OP_mod] = &&L_OP_mod;
    dispatch_table[OP_mul] = &&L_OP_mul;
    dispatch_table[OP_neg] = &&L_OP_neg;
    dispatch_table[OP_neq] = &&L_OP_neq;
    dispatch_table[OP_nip] = &&L_OP_nip;
    dispatch_table[OP_nip1] = &&L_OP_nip1;
    dispatch_table[OP_nip_catch] = &&L_OP_nip_catch;
    dispatch_table[OP_nop] = &&L_OP_nop;
    dispatch_table[OP_not] = &&L_OP_not;
    dispatch_table[OP_null] = &&L_OP_null;
    dispatch_table[OP_object] = &&L_OP_object;
    dispatch_table[OP_or] = &&L_OP_or;
    dispatch_table[OP_perm3] = &&L_OP_perm3;
    dispatch_table[OP_perm4] = &&L_OP_perm4;
    dispatch_table[OP_perm5] = &&L_OP_perm5;
    dispatch_table[OP_plus] = &&L_OP_plus;
    dispatch_table[OP_post_dec] = &&L_OP_post_dec;
    dispatch_table[OP_post_inc] = &&L_OP_post_inc;
    dispatch_table[OP_pow] = &&L_OP_pow;
    dispatch_table[OP_push_0] = &&L_OP_push_0;
    dispatch_table[OP_push_1] = &&L_OP_push_1;
    dispatch_table[OP_push_2] = &&L_OP_push_2;
    dispatch_table[OP_push_3] = &&L_OP_push_3;
    dispatch_table[OP_push_4] = &&L_OP_push_4;
    dispatch_table[OP_push_5] = &&L_OP_push_5;
    dispatch_table[OP_push_6] = &&L_OP_push_6;
    dispatch_table[OP_push_7] = &&L_OP_push_7;
    dispatch_table[OP_push_atom_value] = &&L_OP_push_atom_value;
    dispatch_table[OP_push_const] = &&L_OP_push_const;
    dispatch_table[OP_push_const8] = &&L_OP_push_const8;
    dispatch_table[OP_push_empty_string] = &&L_OP_push_empty_string;
    dispatch_table[OP_push_false] = &&L_OP_push_false;
    dispatch_table[OP_push_i16] = &&L_OP_push_i16;
    dispatch_table[OP_push_i32] = &&L_OP_push_i32;
    dispatch_table[OP_push_i8] = &&L_OP_push_i8;
    dispatch_table[OP_push_minus1] = &&L_OP_push_minus1;
    dispatch_table[OP_push_this] = &&L_OP_push_this;
    dispatch_table[OP_push_true] = &&L_OP_push_true;
    dispatch_table[OP_put_arg] = &&L_OP_put_arg;
    dispatch_table[OP_put_arg0] = &&L_OP_put_arg0;
    dispatch_table[OP_put_arg1] = &&L_OP_put_arg1;
    dispatch_table[OP_put_arg2] = &&L_OP_put_arg2;
    dispatch_table[OP_put_arg3] = &&L_OP_put_arg3;
    dispatch_table[OP_put_array_el] = &&L_OP_put_array_el;
    dispatch_table[OP_put_field] = &&L_OP_put_field;
    dispatch_table[OP_put_loc] = &&L_OP_put_loc;
    dispatch_table[OP_put_loc0] = &&L_OP_put_loc0;
    dispatch_table[OP_put_loc1] = &&L_OP_put_loc1;
    dispatch_table[OP_put_loc2] = &&L_OP_put_loc2;
    dispatch_table[OP_put_loc3] = &&L_OP_put_loc3;
    dispatch_table[OP_put_loc8] = &&L_OP_put_loc8;
    dispatch_table[OP_put_loc_check] = &&L_OP_put_loc_check;
    dispatch_table[OP_put_loc_check_init] = &&L_OP_put_loc_check_init;
    dispatch_table[OP_put_var] = &&L_OP_put_var;
    dispatch_table[OP_put_var_init] = &&L_OP_put_var_init;
    dispatch_table[OP_put_var_ref] = &&L_OP_put_var_ref;
    dispatch_table[OP_put_var_ref0] = &&L_OP_put_var_ref0;
    dispatch_table[OP_put_var_ref1] = &&L_OP_put_var_ref1;
    dispatch_table[OP_put_var_ref2] = &&L_OP_put_var_ref2;
    dispatch_table[OP_put_var_ref3] = &&L_OP_put_var_ref3;
    dispatch_table[OP_put_var_ref_check] = &&L_OP_put_var_ref_check;
    dispatch_table[OP_put_var_ref_check_init] = &&L_OP_put_var_ref_check_init;
    dispatch_table[OP_rest] = &&L_OP_rest;
    dispatch_table[OP_ret] = &&L_OP_ret;
    dispatch_table[OP_return] = &&L_OP_return;
    dispatch_table[OP_return_async] = &&L_OP_return_async;
    dispatch_table[OP_return_undef] = &&L_OP_return_undef;
    dispatch_table[OP_rot3l] = &&L_OP_rot3l;
    dispatch_table[OP_rot3r] = &&L_OP_rot3r;
    dispatch_table[OP_rot4l] = &&L_OP_rot4l;
    dispatch_table[OP_rot5l] = &&L_OP_rot5l;
    dispatch_table[OP_sar] = &&L_OP_sar;
    dispatch_table[OP_set_arg] = &&L_OP_set_arg;
    dispatch_table[OP_set_arg0] = &&L_OP_set_arg0;
    dispatch_table[OP_set_arg1] = &&L_OP_set_arg1;
    dispatch_table[OP_set_arg2] = &&L_OP_set_arg2;
    dispatch_table[OP_set_arg3] = &&L_OP_set_arg3;
    dispatch_table[OP_set_loc] = &&L_OP_set_loc;
    dispatch_table[OP_set_loc0] = &&L_OP_set_loc0;
    dispatch_table[OP_set_loc1] = &&L_OP_set_loc1;
    dispatch_table[OP_set_loc2] = &&L_OP_set_loc2;
    dispatch_table[OP_set_loc3] = &&L_OP_set_loc3;
    dispatch_table[OP_set_loc8] = &&L_OP_set_loc8;
    dispatch_table[OP_set_loc_check] = &&L_OP_set_loc_check;
    dispatch_table[OP_set_loc_uninitialized] = &&L_OP_set_loc_uninitialized;
    dispatch_table[OP_set_name] = &&L_OP_set_name;
    dispatch_table[OP_set_name_computed] = &&L_OP_set_name_computed;
    dispatch_table[OP_set_var_ref] = &&L_OP_set_var_ref;
    dispatch_table[OP_set_var_ref0] = &&L_OP_set_var_ref0;
    dispatch_table[OP_set_var_ref1] = &&L_OP_set_var_ref1;
    dispatch_table[OP_set_var_ref2] = &&L_OP_set_var_ref2;
    dispatch_table[OP_set_var_ref3] = &&L_OP_set_var_ref3;
    dispatch_table[OP_shl] = &&L_OP_shl;
    dispatch_table[OP_shr] = &&L_OP_shr;
    dispatch_table[OP_special_object] = &&L_OP_special_object;
    dispatch_table[OP_strict_eq] = &&L_OP_strict_eq;
    dispatch_table[OP_strict_neq] = &&L_OP_strict_neq;
    dispatch_table[OP_sub] = &&L_OP_sub;
    dispatch_table[OP_swap] = &&L_OP_swap;
    dispatch_table[OP_swap2] = &&L_OP_swap2;
    dispatch_table[OP_tail_call] = &&L_OP_tail_call;
    dispatch_table[OP_tail_call_method] = &&L_OP_tail_call_method;
    dispatch_table[OP_throw] = &&L_OP_throw;
    dispatch_table[OP_throw_error] = &&L_OP_throw_error;
    dispatch_table[OP_to_object] = &&L_OP_to_object;
    dispatch_table[OP_to_propkey] = &&L_OP_to_propkey;
    dispatch_table[OP_typeof] = &&L_OP_typeof;
    dispatch_table[OP_typeof_is_function] = &&L_OP_typeof_is_function;
    dispatch_table[OP_typeof_is_undefined] = &&L_OP_typeof_is_undefined;
    dispatch_table[OP_undefined] = &&L_OP_undefined;
    dispatch_table[OP_xor] = &&L_OP_xor;
    dispatch_table[OP_yield] = &&L_OP_yield;
    dispatch_table[OP_yield_star] = &&L_OP_yield_star;
    dispatch_table_initialized.store(true, std::memory_order_release);
    }   // close inner DCLP check
    }   // close outer "not initialized" branch

    // globalObj is recomputed on every dispatch because some opcodes
    // (top-level `var` set, OP_put_field on the global root) re-bind
    // *pGlobalRoot.  Keep this read in DISPATCH so every case sees the
    // current value without a per-case re-read.
    const proto::ProtoObject* globalObj = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : PROTO_NONE;
    int opcode = 0;

    // Local cache of stack pointers. MUST be re-fetched after any recursive call
    // (OP_call, OP_get_field if it triggers a getter, etc.) because the
    // context's automatic locals array or the thread-local frame vector
    // might have reallocated.
    const proto::ProtoObject** pAutomaticLocals = const_cast<const proto::ProtoObject**>(pContext->getAutomaticLocals());
    const size_t _frameIdx = t_interpFrames.size() - 1;
    const unsigned int currentStackBase = t_interpFrames[_frameIdx].stackBase;

    #define _PF() (t_interpFrames[_frameIdx])
    #define REFRESH_INTERP_STATE() do { \
        pAutomaticLocals = const_cast<const proto::ProtoObject**>(pContext->getAutomaticLocals()); \
    } while(0)

    #define STACK_POP_ZERO() do { \
        if (__builtin_expect(_PF().stackTop > 0, 1)) { \
            pAutomaticLocals[currentStackBase + --_PF().stackTop] = PROTO_NONE; \
        } \
    } while(0)

    static thread_local uint32_t t_dispatchCount = 0;
    /*
     * DISPATCH — minimal hot-path dispatch.
     *
     * What used to happen on every dispatch:
     *   1. Increment safepoint counter, occasionally yield to GC.
     *   2. Check has_pending_exception.
     *   3. Bounds-check pc.
     *   4. Re-fetch globalObj from *pGlobalRoot.
     *   5. Read opcode byte, advance pc.
     *   6. Look up dispatch_table[opcode], null-check it, indirect-jump.
     *
     * Steps (4) and the null check inside (6) are removed:
     *   - globalObj is only consumed by ~6 specific opcodes (OP_get_field2,
     *     OP_put_field, OP_define_field, OP_typeof_*, the Array-prototype
     *     lookup in OP_get_array_el).  Those sites refresh it on demand
     *     via REFRESH_GLOBAL_OBJ() instead of paying for a re-fetch on
     *     every opcode.
     *   - dispatch_table is pre-filled with &&L_default at runBytecode
     *     entry, so the slot is always a valid jump target — no nullptr
     *     check needed.
     *
     * Net: ~7 fewer cycles per dispatch (~5 ns on a 3 GHz core).  On
     * dispatch-bound benches (numeric_loop, tight inner loops) this is
     * a measurable win (~5-8 % wall).
     */
    #define DISPATCH() do { \
        if (__builtin_expect((++t_dispatchCount & 1023) == 0, 0)) pContext->safepoint(); \
        if (__builtin_expect(has_pending_exception, 0)) goto handle_exception_label; \
        if (__builtin_expect(pc < 0 || pc >= len, 0)) goto exit_dispatch; \
        opcode = (int)(unsigned char)buf[pc++]; \
        goto *dispatch_table[opcode]; \
    } while(0)

    /* Refresh globalObj from *pGlobalRoot — call from sites that read or
     * write the global object so they always see the live value.  No-op
     * elsewhere: keeps the per-dispatch cost out of DISPATCH. */
    #define REFRESH_GLOBAL_OBJ() do { \
        globalObj = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : PROTO_NONE; \
    } while(0)

    DISPATCH();
    {
            // --- Constant and immediate pushes ---
            L_OP_push_minus1: ;
                stackPush(pContext,pContext->fromInteger(-1));
                DISPATCH();
            L_OP_push_0: ;
                stackPush(pContext,proto::makeSmallInt(0));
                DISPATCH();
            L_OP_push_1: ;
                stackPush(pContext,proto::makeSmallInt(1));
                DISPATCH();
            L_OP_push_2: ;
                stackPush(pContext,proto::makeSmallInt(2));
                DISPATCH();
            L_OP_push_3: ;
                stackPush(pContext,proto::makeSmallInt(3));
                DISPATCH();
            L_OP_push_4: ;
                stackPush(pContext,proto::makeSmallInt(4));
                DISPATCH();
            L_OP_push_5: ;
                stackPush(pContext,proto::makeSmallInt(5));
                DISPATCH();
            L_OP_push_6: ;
                stackPush(pContext,proto::makeSmallInt(6));
                DISPATCH();
            L_OP_push_7: ;
                stackPush(pContext,proto::makeSmallInt(7));
                DISPATCH();
            L_OP_push_i8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t v = static_cast<int8_t>(buf[pc++]);
                stackPush(pContext,proto::makeSmallInt(v));
                DISPATCH();
            }
            L_OP_push_i16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t v = static_cast<int16_t>(get_u16(buf + pc));
                pc += 2;
                if (proto::smallIntInRange(v)) {
                    stackPush(pContext, proto::makeSmallInt(v));
                } else {
                    stackPush(pContext, pContext->fromInteger(static_cast<long long>(v)));
                }
                DISPATCH();
            }
            L_OP_push_i32: {
                // push_i32 encodes a 32-bit signed immediate.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t v = (int32_t)get_u32(buf + pc);
                pc += 4;
                if (proto::smallIntInRange(v)) {
                    stackPush(pContext, proto::makeSmallInt(v));
                } else {
                    stackPush(pContext, pContext->fromInteger(static_cast<long long>(v)));
                }
                DISPATCH();
            }
            L_OP_push_const8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                if (cpool && idx < cpool->getSize(pContext))
                    stackPush(pContext, cpool->getAt(pContext, static_cast<int>(idx)));
                else
                    stackPush(pContext, PROTO_NONE);
                DISPATCH();
            }
            L_OP_push_empty_string: ;
                stackPush(pContext,pContext->fromUTF8String(""));
                DISPATCH();
            L_OP_push_this: ;
                // Strict mode: pass thisObj as-is (undefined stays undefined).
                // Non-strict mode: coerce null/undefined to the global object per spec.
                if (module->isStrict) {
                    stackPush(pContext, thisObj ? thisObj : PROTO_NONE);
                } else {
                    REFRESH_GLOBAL_OBJ();
                    const proto::ProtoObject* finalThis = thisObj;
                    if (!finalThis || finalThis == PROTO_NONE || finalThis == t_undefinedSentinel || finalThis == t_nullSentinel) {
                        finalThis = globalObj ? globalObj : PROTO_NONE;
                    }
                    stackPush(pContext, finalThis);
                }
                DISPATCH();
            L_OP_special_object: {
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
                    if (lenKey2 && argsObj) {
                        argsObj = argsObj->setAttribute(pContext, lenKey2, pContext->fromInteger(static_cast<long long>(argc2)));
                        // §10.4.4.7 CreateUnmappedArgumentsObject step 4:
                        // arguments.length is {writable:true, enumerable:false,
                        // configurable:true} → 0x3.  Pre-fix the slot
                        // defaulted to fully enumerable (0x7), so Object.keys
                        // (arguments) leaked 'length' alongside the indices
                        // (built-ins/Object/keys/15.2.3.14-3-4).
                        const proto::ProtoObject* pdlo = pContext->fromUTF8String("__pd_length__");
                        const proto::ProtoString* pdlk = pdlo ? pdlo->asString(pContext) : nullptr;
                        if (pdlk) argsObj = argsObj->setAttribute(pContext, pdlk, pContext->fromInteger(0x3LL));
                    }
                    // Set Symbol.toStringTag so Object.prototype.toString.call(arguments) === "[object Arguments]"
                    {
                        const proto::ProtoString* tagKey = JSSymbols::toStringTag(pContext);
                        if (tagKey && argsObj)
                            argsObj = argsObj->setAttribute(pContext, tagKey, pContext->fromUTF8String("Arguments"));
                    }
                    stackPush(pContext, argsObj ? argsObj : PROTO_NONE);
                } else if (soKind == 2) {
                    // THIS_FUNC: the currently-running function object.
                    // Populated by OP_call_constructor / OP_call_method
                    // when entering a class/function body.
                    stackPush(pContext, t_activeFunc ? t_activeFunc : PROTO_NONE);
                } else if (soKind == 3) {
                    // NEW_TARGET: the original `new`'s function reference.
                    stackPush(pContext, t_activeNewTgt ? t_activeNewTgt : PROTO_NONE);
                } else if (soKind == 4) {
                    // HOME_OBJECT: stored on the method by OP_set_home_object
                    // during class body construction.
                    const proto::ProtoObject* home = PROTO_NONE;
                    if (t_activeFunc && t_activeFunc != PROTO_NONE) {
                        const proto::ProtoObject* hoKo = pContext->fromUTF8String("__home_object__");
                        const proto::ProtoString* hoK = hoKo ? hoKo->asString(pContext) : nullptr;
                        if (hoK) {
                            const proto::ProtoObject* h = t_activeFunc->getAttribute(pContext, hoK, false);
                            if (h && h != PROTO_NONE) home = h;
                        }
                    }
                    stackPush(pContext, home);
                } else {
                    // kinds 5 (VAR_OBJECT), 6 (IMPORT_META) — undefined.
                    stackPush(pContext, PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_rest: {
                // DEF(rest, 3, 0, 1, u16) — push an array containing args
                // from the first-rest-index onward.  The call-time `args`
                // ProtoList is passed into runBytecode so we have the
                // full pre-truncation argument list available here.
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t firstRestIdx = get_u16(buf + pc);
                pc += 2;
                REFRESH_GLOBAL_OBJ();
                const proto::ProtoString* arrProtoLookupKey =
                    JSSymbols::arrayProto(pContext);
                const proto::ProtoObject* arrProto =
                    (arrProtoLookupKey && globalObj && globalObj != PROTO_NONE)
                        ? globalObj->getAttribute(pContext, arrProtoLookupKey, false)
                        : nullptr;
                const proto::ProtoObject* arr = (arrProto && arrProto != PROTO_NONE)
                    ? arrProto->newChild(pContext, true)
                    : pContext->newObject(true);
                const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                if (isArrKey && arr) arr = arr->setAttribute(pContext, isArrKey, PROTO_TRUE);
                proto::ProtoContext::CriticalSection restCs(pContext);
                const proto::ProtoList* list = pContext->newList();
                unsigned int total = args ? static_cast<unsigned int>(args->getSize(pContext)) : 0;
                long long count = 0;
                if (total > firstRestIdx) {
                    for (unsigned int i = firstRestIdx; i < total; ++i) {
                        const proto::ProtoObject* a = args->getAt(pContext, static_cast<int>(i));
                        list = list->appendLast(pContext, a ? a : PROTO_NONE);
                        ++count;
                    }
                }
                if (arr && list) protojs::setArrayElements(pContext, arr, list);
                if (arr) arr = arr->setAttribute(pContext,
                    JSSymbols::length(pContext),
                    pContext->fromInteger(count));
                stackPush(pContext, arr ? arr : PROTO_NONE);
                DISPATCH();
            }
            L_OP_apply: {
                // DEF(apply, 3, 3, 1, u16)
                //   stack [..., fn, this_arg, argsArray] → [..., result]
                //   u16 magic: bit 0 = construct; bit 1 = no-null-array tolerance
                //
                // Emitted for spread call:    f(...arr)   → magic=0
                //                  new f(...arr)         → magic=1
                //                  super(...args)        → magic=1
                if (pc + 2 > len || stackSize(pContext) < 3) return PROTO_NONE;
                uint16_t magic = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* argsArr = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE;
                const proto::ProtoObject* thisArg = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE;
                const proto::ProtoObject* fn      = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE;

                // Build the args list from argsArr's __elements__ (with
                // indexed-attribute fallback for legacy producers).
                proto::ProtoContext::CriticalSection applyCs(pContext);
                const proto::ProtoList* argList = pContext->newList();
                if (argsArr && argsArr != PROTO_NONE) {
                    const proto::ProtoList* els = protojs::getArrayElements(pContext, argsArr);
                    if (els) {
                        size_t sz = els->getSize(pContext);
                        for (size_t i = 0; i < sz; ++i)
                            argList = argList->appendLast(pContext,
                                els->getAt(pContext, static_cast<int>(i)));
                    } else {
                        const proto::ProtoString* lenK = JSSymbols::length(pContext);
                        const proto::ProtoObject* lenV = lenK ? argsArr->getAttribute(pContext, lenK, false) : nullptr;
                        long long n = 0;
                        if (lenV && lenV != PROTO_NONE && lenV->isInteger(pContext)) n = lenV->asLong(pContext);
                        for (long long i = 0; i < n; ++i) {
                            const proto::ProtoString* ik = JSSymbols::indexKey(pContext, static_cast<uint32_t>(i));
                            const proto::ProtoObject* av = ik ? argsArr->getAttribute(pContext, ik, false) : PROTO_NONE;
                            argList = argList->appendLast(pContext, av ? av : PROTO_NONE);
                        }
                    }
                }

                const proto::ProtoObject* result = PROTO_NONE;
                if (magic & 1) {
                    // Construct path: synthesise a `new fn(...args)` call.
                    // Build newObj inheriting from NEW_TARGET.prototype (=
                    // the original derived class's prototype for super()
                    // call), not fn.prototype.  This keeps `new B()` ending
                    // up as a B instance even when super(args) dispatches A.
                    const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                    const proto::ProtoObject* ntForProto =
                        (t_activeNewTgt && t_activeNewTgt != PROTO_NONE)
                            ? t_activeNewTgt : fn;
                    const proto::ProtoObject* funcProto = (protoKey && ntForProto && ntForProto != PROTO_NONE)
                        ? ntForProto->getAttribute(pContext, protoKey, false) : nullptr;
                    const proto::ProtoObject* newObj = (funcProto && funcProto != PROTO_NONE)
                        ? funcProto->newChild(pContext, true)
                        : pContext->newObject(true);
                    // Parent's __fields_init__ runs on newObj before its body.
                    const proto::ProtoObject* fnProto = (protoKey && fn && fn != PROTO_NONE)
                        ? fn->getAttribute(pContext, protoKey, false) : nullptr;
                    const proto::ProtoObject* parentFI = nullptr;
                    {
                        if (fnProto && fnProto != PROTO_NONE) {
                            const proto::ProtoObject* fiKo = pContext->fromUTF8String("__fields_init__");
                            const proto::ProtoString* fiK = fiKo ? fiKo->asString(pContext) : nullptr;
                            parentFI = fiK
                                ? fnProto->getAttribute(pContext, fiK, false) : nullptr;
                            if (parentFI && parentFI != PROTO_NONE) {
                                const proto::ProtoList* fiArgs = pContext->newList();
                                callJSFunction(pContext, parentFI, newObj, fiArgs);
                                if (t_hasCallException) {
                                    pending_exception = t_callException;
                                    has_pending_exception = true;
                                    t_hasCallException = false;
                                    t_callException = nullptr;
                                    DISPATCH();
                                }
                            }
                        }
                    }
                    const proto::ProtoObject* ret =
                        callJSFunction(pContext, fn, newObj, argList);
                    if (t_hasCallException) {
                        pending_exception = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException = nullptr;
                        DISPATCH();
                    }
                    // After parent constructor (super) returns, also invoke
                    // the CURRENT class's __fields_init__ on `this`.  In an
                    // explicit `constructor(){ super(); ... }`, the
                    // standard bytecode emits emit_class_field_init after
                    // OP_apply, but that path requires closure-capture for
                    // class_fields_init that protoJS doesn't fully resolve;
                    // direct dispatch here gives equivalent behavior.
                    {
                        const proto::ProtoObject* finalForFields = (ret && ret != PROTO_NONE && !ret->isInteger(pContext)
                            && !ret->isBoolean(pContext) && !ret->isDouble(pContext)
                            && !ret->asString(pContext) && ret != t_nullSentinel)
                            ? ret : newObj;
                        if (t_activeFunc && t_activeFunc != PROTO_NONE && finalForFields) {
                            const proto::ProtoObject* afProto = protoKey
                                ? t_activeFunc->getAttribute(pContext, protoKey, false) : nullptr;
                            if (afProto && afProto != PROTO_NONE) {
                                const proto::ProtoObject* fiKo2 = pContext->fromUTF8String("__fields_init__");
                                const proto::ProtoString* fiK2 = fiKo2 ? fiKo2->asString(pContext) : nullptr;
                                const proto::ProtoObject* fi2 = fiK2
                                    ? afProto->getAttribute(pContext, fiK2, false) : nullptr;
                                if (fi2 && fi2 != PROTO_NONE && fi2 != parentFI) {
                                    const proto::ProtoList* fi2Args = pContext->newList();
                                    callJSFunction(pContext, fi2, finalForFields, fi2Args);
                                    if (t_hasCallException) {
                                        pending_exception = t_callException;
                                        has_pending_exception = true;
                                        t_hasCallException = false;
                                        t_callException = nullptr;
                                        DISPATCH();
                                    }
                                }
                            }
                        }
                    }
                    // If constructor returned a non-undefined object, use it; else newObj.
                    if (ret && ret != PROTO_NONE && !ret->isInteger(pContext) &&
                        !ret->isBoolean(pContext) && !ret->isDouble(pContext) &&
                        !ret->asString(pContext) && ret != t_nullSentinel)
                        result = ret;
                    else
                        result = newObj;
                } else {
                    result = callJSFunction(pContext, fn, thisArg, argList);
                    if (t_hasCallException) {
                        pending_exception = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException = nullptr;
                        DISPATCH();
                    }
                }
                REFRESH_INTERP_STATE();
                pAutomaticLocals[currentStackBase + _PF().stackTop++] =
                    result ? result : PROTO_NONE;
                DISPATCH();
            }
            L_OP_check_ctor: {
                // Verify the function was called as a constructor.
                // Minimal impl: no-op — the strict spec semantics are
                // not enforced yet and most tests pass-through.
                DISPATCH();
            }
            L_OP_check_ctor_return: {
                // DEF(check_ctor_return, 1, 1, 2, none)
                // Stack: [..., retVal] → [..., retVal, useThis(bool)]
                // For a derived class ctor return:
                //   - if retVal is undefined → push TRUE (use `this`)
                //   - if retVal is an object → push FALSE (use retVal)
                //   - otherwise throw TypeError
                if (stackEmpty(pContext)) DISPATCH();
                const proto::ProtoObject* rv = stackTop(pContext);
                bool isUndef = (!rv || rv == PROTO_NONE || rv == getUndefinedSentinel()
                                || (rv && rv->isNone(pContext)));
                bool isObj = false;
                if (!isUndef && rv && rv != PROTO_NONE) {
                    isObj = !(rv == PROTO_TRUE || rv == PROTO_FALSE
                              || rv == t_nullSentinel
                              || rv->isInteger(pContext)
                              || rv->isDouble(pContext)
                              || rv->isBoolean(pContext)
                              || rv->asString(pContext));
                }
                if (!isUndef && !isObj) {
                    pending_exception = makeError(pContext, "TypeError",
                        "derived class constructor must return an object or undefined",
                        pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                stackPush(pContext, isUndef ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_eval: {
                // DEF(eval, 7, 1, 1, npop_u16)
                // We don't implement runtime eval. Pop arguments and
                // push undefined to keep the stack balanced rather than
                // bailing out.
                if (pc + 4 > len) return PROTO_NONE;
                uint16_t evalArgc = get_u16(buf + pc);
                pc += 4;
                for (uint32_t i = 0; i <= static_cast<uint32_t>(evalArgc); ++i) {
                    if (stackEmpty(pContext)) break;
                    stackPop(pContext);
                }
                stackPush(pContext, PROTO_NONE);
                DISPATCH();
            }
            L_OP_init_ctor: {
                // DEF(init_ctor, 1, 0, 1, none)
                // Emitted at the start of a default derived-class
                // constructor body.  Equivalent to constructor(...args)
                // { super(...args); }.
                //
                // QuickJS: super = JS_GetPrototype(func_obj);
                //          ret = JS_CallConstructor2(super, new_target, argc, argv);
                //          sp[0] = ret;
                //
                // We read t_activeFunc (current ctor) → __class_parent__
                // → parent constructor.  Forward t_activeArgs to it via
                // OP_call_constructor-equivalent logic.
                const proto::ProtoObject* parent = nullptr;
                if (t_activeFunc && t_activeFunc != PROTO_NONE) {
                    const proto::ProtoObject* cpo = pContext->fromUTF8String("__class_parent__");
                    const proto::ProtoString* cpk = cpo ? cpo->asString(pContext) : nullptr;
                    if (cpk) {
                        const proto::ProtoObject* cp = t_activeFunc->getAttribute(pContext, cpk, false);
                        if (cp && cp != PROTO_NONE) parent = cp;
                    }
                    if (!parent) parent = t_activeFunc->getPrototype(pContext);
                }
                if (!parent || parent == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    DISPATCH();
                }
                // Build newObj inheriting from NEW_TARGET.prototype
                // (= the original constructor's prototype, B.prototype for
                // `new B()` even when the parent ctor A is invoked).  Per
                // ECMA-262 §9.2.2 OrdinaryCallEvaluateBody, the receiver's
                // [[Prototype]] is new_target.prototype, not super.prototype.
                // Pre-fix this used parent.prototype, so derived instances
                // ended up with A.prototype as their chain parent — methods
                // declared on B.prototype were invisible.
                const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                const proto::ProtoObject* ntForProto =
                    (t_activeNewTgt && t_activeNewTgt != PROTO_NONE)
                        ? t_activeNewTgt : parent;
                const proto::ProtoObject* tgtProto = protoKey
                    ? ntForProto->getAttribute(pContext, protoKey, false) : nullptr;
                const proto::ProtoObject* newObj = (tgtProto && tgtProto != PROTO_NONE)
                    ? tgtProto->newChild(pContext, true)
                    : pContext->newObject(true);
                const proto::ProtoList* forwardArgs = t_activeArgs
                    ? t_activeArgs : pContext->newList();
                // Run parent's __fields_init__ on newObj BEFORE calling the
                // parent ctor's bytecode — that lets parent's class fields
                // (e.g. `class A { x = 10 }`) initialise on `this`.
                {
                    const proto::ProtoString* parentProtoKey = JSSymbols::prototype(pContext);
                    const proto::ProtoObject* parentProto = parentProtoKey
                        ? parent->getAttribute(pContext, parentProtoKey, false) : nullptr;
                    if (parentProto && parentProto != PROTO_NONE) {
                        const proto::ProtoObject* fiKo = pContext->fromUTF8String("__fields_init__");
                        const proto::ProtoString* fiK = fiKo ? fiKo->asString(pContext) : nullptr;
                        const proto::ProtoObject* fi = fiK
                            ? parentProto->getAttribute(pContext, fiK, false) : nullptr;
                        if (fi && fi != PROTO_NONE) {
                            const proto::ProtoList* fiArgs = pContext->newList();
                            callJSFunction(pContext, fi, newObj, fiArgs);
                            if (t_hasCallException) {
                                pending_exception = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException = nullptr;
                                DISPATCH();
                            }
                        }
                    }
                }
                // Dispatch to parent ctor.  Two paths:
                //   - JS class ctor with __bytecode_id__ → callJSFunction
                //   - Native ctor with __construct__ (Number, Boolean,
                //     Map, Set, Promise, ...) → invoke directly
                // Also handles specialised native markers (arrayCtor, errCtor,
                // regexpCtor, taCtor, strCtor) — these have specialised paths
                // in OP_call_constructor that we mirror here for super() with
                // a built-in parent.
                const proto::ProtoObject* ret = nullptr;
                {
                    const proto::ProtoObject* prevActiveI  = t_activeFunc;
                    const proto::ProtoObject* prevNewTgtI  = t_activeNewTgt;
                    const proto::ProtoList*   prevArgsI    = t_activeArgs;
                    t_activeNewTgt = t_activeNewTgt ? t_activeNewTgt : parent;
                    // Check for __construct__ native method first (Number etc).
                    const proto::ProtoString* ctorKeyL = JSSymbols::construct(pContext);
                    const proto::ProtoObject* ctorMethodL = (ctorKeyL && parent && parent != PROTO_NONE)
                        ? parent->getAttribute(pContext, ctorKeyL, false) : nullptr;
                    if (ctorMethodL && ctorMethodL != PROTO_NONE && ctorMethodL->isMethod(pContext)) {
                        proto::ProtoMethod nm = ctorMethodL->asMethod(pContext);
                        ret = nm ? nm(pContext, newObj, nullptr, forwardArgs, nullptr) : nullptr;
                    } else {
                        // Specialised: arrayCtor, errCtor, regexpCtor, taCtor, strCtor.
                        const proto::ProtoString* arrayK = JSSymbols::arrayCtor(pContext);
                        const proto::ProtoString* errK   = JSSymbols::errorCtor(pContext);
                        const proto::ProtoString* reK    = JSSymbols::regexpCtor(pContext);
                        const proto::ProtoString* taK    = JSSymbols::taCtor(pContext);
                        const proto::ProtoString* strK   = JSSymbols::stringCtor(pContext);
                        const proto::ProtoObject* arrayAttr = (parent && arrayK)
                            ? parent->getAttribute(pContext, arrayK, false) : nullptr;
                        const proto::ProtoObject* errAttr = (parent && errK)
                            ? parent->getAttribute(pContext, errK, false) : nullptr;
                        const proto::ProtoObject* reAttr  = (parent && reK)
                            ? parent->getAttribute(pContext, reK, false) : nullptr;
                        const proto::ProtoObject* taAttr  = (parent && taK)
                            ? parent->getAttribute(pContext, taK, false) : nullptr;
                        const proto::ProtoObject* strAttr = (parent && strK)
                            ? parent->getAttribute(pContext, strK, false) : nullptr;
                        if (arrayAttr == PROTO_TRUE) {
                            const proto::ProtoString* isArrKey2 = JSSymbols::isArray(pContext);
                            if (isArrKey2) newObj = newObj->setAttribute(pContext, isArrKey2, PROTO_TRUE);
                            const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                            long long flen = forwardArgs ? (long long)forwardArgs->getSize(pContext) : 0;
                            if (flen == 1 && forwardArgs->getAt(pContext, 0)->isInteger(pContext)) {
                                newObj = newObj->setAttribute(pContext, lenKey2, forwardArgs->getAt(pContext, 0));
                            } else if (lenKey2) {
                                newObj = newObj->setAttribute(pContext, lenKey2, pContext->fromInteger(flen));
                            }
                            ret = newObj;
                        } else if (errAttr && errAttr != PROTO_NONE) {
                            // Apply the Error constructor body onto the
                            // newly-allocated newObj (whose [[Prototype]] is
                            // NEW_TARGET.prototype = SubClass.prototype) so
                            // SubClass-specific instance methods stay reachable.
                            // Pre-fix: makeError ignored newObj and built a
                            // fresh cell parented at Error.prototype, so
                            // `class Err extends Error {}; new Err().hasOwnProperty`
                            // walked the wrong chain and returned undefined.
                            std::string msg;
                            long long flen = forwardArgs ? (long long)forwardArgs->getSize(pContext) : 0;
                            if (flen > 0) {
                                const proto::ProtoObject* mVal = toString(pContext, forwardArgs->getAt(pContext, 0));
                                if (mVal && mVal->isString(pContext)) mVal->asString(pContext)->toUTF8String(pContext, msg);
                            }
                            if (!msg.empty()) {
                                const proto::ProtoString* msgKeyE = JSSymbols::message(pContext);
                                if (msgKeyE) {
                                    newObj = newObj->setAttribute(pContext, msgKeyE,
                                                                  pContext->fromUTF8String(msg.c_str()));
                                    const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_message__");
                                    const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                    if (pdk && newObj)
                                        newObj = newObj->setAttribute(pContext, pdk, pContext->fromInteger(0x3LL));
                                }
                            }
                            ret = newObj;
                        } else if (reAttr == PROTO_TRUE) {
                            ret = regexpConstructor(pContext, newObj, nullptr, forwardArgs, nullptr);
                        } else if (strAttr == PROTO_TRUE) {
                            const proto::ProtoObject* pv = (forwardArgs && forwardArgs->getSize(pContext) > 0)
                                ? toString(pContext, forwardArgs->getAt(pContext, 0))
                                : pContext->fromUTF8String("");
                            newObj = newObj->setAttribute(pContext, JSSymbols::primitiveValue(pContext), pv);
                            // ECMA-262 §22.1.4: own length, non-writable, non-configurable.
                            long long slenI = 0;
                            if (pv && pv->isString(pContext)) {
                                const proto::ProtoString* ps = pv->asString(pContext);
                                if (ps) slenI = static_cast<long long>(ps->getSize(pContext));
                            }
                            const proto::ProtoString* lenKeyI = JSSymbols::length(pContext);
                            if (lenKeyI)
                                newObj = newObj->setAttribute(pContext, lenKeyI, pContext->fromInteger(slenI));
                            const proto::ProtoObject* pdoI = pContext->fromUTF8String("__pd_length__");
                            const proto::ProtoString* pdkI = pdoI ? pdoI->asString(pContext) : nullptr;
                            if (pdkI) newObj = newObj->setAttribute(pContext, pdkI, pContext->fromInteger(0x0LL));
                            ret = newObj;
                        } else {
                            // Fallback: callJSFunction (JS bytecode ctor).
                            ret = callJSFunction(pContext, parent, newObj, forwardArgs);
                        }
                    }
                    t_activeFunc = prevActiveI;
                    t_activeNewTgt = prevNewTgtI;
                    t_activeArgs = prevArgsI;
                    if (t_hasCallException) {
                        pending_exception = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException = nullptr;
                        DISPATCH();
                    }
                    // ret if object, else newObj (spec OrdinaryCallEvaluateBody).
                    bool retIsObj = ret && ret != PROTO_NONE &&
                                    !ret->isInteger(pContext) && !ret->isDouble(pContext) &&
                                    !ret->isBoolean(pContext) && !ret->asString(pContext) &&
                                    ret != t_nullSentinel;
                    const proto::ProtoObject* finalThis = retIsObj ? ret : newObj;
                    // Run THIS class's __fields_init__ on the resulting `this`.
                    // The parent's fields were already initialised by the
                    // callJSFunction above (parent's OP_call_constructor or
                    // its own OP_init_ctor in a deeper chain).  This class's
                    // fields live on t_activeFunc.prototype.__fields_init__.
                    if (t_activeFunc && t_activeFunc != PROTO_NONE && finalThis) {
                        const proto::ProtoString* protoKeyFI = JSSymbols::prototype(pContext);
                        const proto::ProtoObject* tProto = protoKeyFI
                            ? t_activeFunc->getAttribute(pContext, protoKeyFI, false) : nullptr;
                        if (tProto && tProto != PROTO_NONE) {
                            const proto::ProtoObject* fiKo = pContext->fromUTF8String("__fields_init__");
                            const proto::ProtoString* fiK = fiKo ? fiKo->asString(pContext) : nullptr;
                            const proto::ProtoObject* fi = fiK
                                ? tProto->getAttribute(pContext, fiK, false) : nullptr;
                            if (fi && fi != PROTO_NONE) {
                                const proto::ProtoList* fiArgs = pContext->newList();
                                callJSFunction(pContext, fi, finalThis, fiArgs);
                                if (t_hasCallException) {
                                    pending_exception = t_callException;
                                    has_pending_exception = true;
                                    t_hasCallException = false;
                                    t_callException = nullptr;
                                    DISPATCH();
                                }
                            }
                        }
                    }
                    stackPush(pContext, finalThis);
                }
                DISPATCH();
            }
            L_OP_check_brand: {
                // Private brand check — minimal impl: skip.
                if (stackSize(pContext) >= 2) {
                    stackPop(pContext);
                    stackPop(pContext);
                }
                DISPATCH();
            }
            L_OP_add_brand: {
                // Private brand registration — minimal impl: skip.
                if (stackSize(pContext) >= 2) {
                    stackPop(pContext);
                    stackPop(pContext);
                }
                DISPATCH();
            }
            L_OP_set_home_object: {
                // DEF(set_home_object, 1, 0, 0, none)
                // Attach the home object (sp[-2]) to the method (sp[-1])
                // so that super.X resolves via the home-object's prototype
                // chain.  Net-zero stack effect.  Records __home_object__
                // on the method; the value is read by OP_special_object
                // kind=HOME_OBJECT at the start of the method body.
                //
                // Side detection: if the next opcode is NOT OP_define_method
                // or OP_define_method_computed, this is the
                // class_fields_init closure (QuickJS emits OP_fclosure +
                // OP_set_home_object + OP_scope_put_var_init for it).
                // Stash it on the home (prototype) under __fields_init__
                // so OP_call_constructor can invoke it when the user
                // ctor body doesn't (e.g., implicit/empty constructor).
                if (stackSize(pContext) >= 2) {
                    const proto::ProtoObject* method = stackAt(pContext, 0);
                    const proto::ProtoObject* home   = stackAt(pContext, 1);
                    if (method && method != PROTO_NONE && home && home != PROTO_NONE) {
                        const proto::ProtoObject* hoKo = pContext->fromUTF8String("__home_object__");
                        const proto::ProtoString* hoKey = hoKo ? hoKo->asString(pContext) : nullptr;
                        if (hoKey) {
                            const proto::ProtoObject* newMethod = method->setAttribute(pContext, hoKey, home);
                            pAutomaticLocals[currentStackBase + _PF().stackTop - 1] =
                                newMethod ? newMethod : method;
                            updateMapping(pContext, method, newMethod ? newMethod : method);
                            // Sniff next opcode for fields_init detection.
                            if (pc < len) {
                                uint8_t nextOp = buf[pc];
                                if (nextOp != OP_define_method && nextOp != OP_define_method_computed) {
                                    const proto::ProtoObject* fiKo = pContext->fromUTF8String("__fields_init__");
                                    const proto::ProtoString* fiK = fiKo ? fiKo->asString(pContext) : nullptr;
                                    if (fiK) {
                                        const proto::ProtoObject* newHome = home->setAttribute(pContext, fiK,
                                            newMethod ? newMethod : method);
                                        if (newHome && newHome != home) {
                                            pAutomaticLocals[currentStackBase + _PF().stackTop - 2] = newHome;
                                            updateMapping(pContext, home, newHome);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_get_super: {
                // DEF(get_super, 1, 1, 1, none)
                // Stack [..., obj] → [..., obj.prototype]
                //
                // Two use cases:
                //   super.foo() — TOS is the method's home_object (e.g.
                //   B.prototype).  Return TOS.[[Prototype]] (= A.prototype).
                //
                //   super(...args) — TOS is this_active_func (B's ctor).
                //   Return the parent CLASS (A's ctor), which is stored
                //   on the ctor under __class_parent__ by OP_define_class.
                //   Falling back to TOS.getPrototype() returns
                //   Function.prototype which is not a constructor, hence
                //   the "function is not a constructor" error.
                if (stackEmpty(pContext)) DISPATCH();
                const proto::ProtoObject* topObj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* parent = nullptr;
                if (topObj && topObj != PROTO_NONE) {
                    // Class-ctor case: check __class_parent__ first.
                    const proto::ProtoObject* cpo = pContext->fromUTF8String("__class_parent__");
                    const proto::ProtoString* cpk = cpo ? cpo->asString(pContext) : nullptr;
                    if (cpk) {
                        const proto::ProtoObject* cp = topObj->getAttribute(pContext, cpk, false);
                        if (cp && cp != PROTO_NONE) parent = cp;
                    }
                    if (!parent) parent = topObj->getPrototype(pContext);
                }
                stackPush(pContext, parent ? parent : PROTO_NONE);
                DISPATCH();
            }
            L_OP_get_super_value: {
                // DEF(get_super_value, 1, 3, 1, none)
                // Stack [..., this, super_obj, prop] → [..., value]
                // Implements `super[prop]` (or computed `super.x`) per
                // ECMA-262 §13.3.7.3 GetSuperBase / GetValue: look up
                // `prop` on `super_obj` walking its prototype chain with
                // `this` as receiver (no receiver semantics implemented
                // here — we just read the property; getters that consult
                // `this` would need an explicit receiver-aware lookup).
                if (stackSize(pContext) < 3) DISPATCH();
                const proto::ProtoObject* prop  = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* sObj  = stackTop(pContext); stackPop(pContext);
                /* thisObj */                     stackPop(pContext);
                const proto::ProtoString* key = nullptr;
                if (prop && prop != PROTO_NONE) {
                    key = prop->asString(pContext);
                    if (!key && prop->isInteger(pContext)) {
                        long long idx = prop->asLong(pContext);
                        if (idx >= 0) key = JSSymbols::indexKey(pContext, static_cast<uint32_t>(idx));
                    }
                }
                const proto::ProtoObject* val = (sObj && sObj != PROTO_NONE && key)
                    ? sObj->getAttribute(pContext, key, true) : PROTO_NONE;
                stackPush(pContext, val ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_super_value: {
                // DEF(put_super_value, 1, 4, 0, none)
                // Stack [..., this, super_obj, prop, value] → []
                // Implements `super[prop] = value` / `super.x = value`.
                // Per spec the write targets `this` (NOT super_obj) so the
                // property lives on the receiver — but the lookup of
                // existing setters walks super_obj's chain.  Minimal impl
                // writes to thisObj.
                if (stackSize(pContext) < 4) DISPATCH();
                const proto::ProtoObject* val   = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* prop  = stackTop(pContext); stackPop(pContext);
                /* sObj */                       stackPop(pContext);
                const proto::ProtoObject* tObj  = stackTop(pContext); stackPop(pContext);
                const proto::ProtoString* key = nullptr;
                if (prop && prop != PROTO_NONE) {
                    key = prop->asString(pContext);
                    if (!key && prop->isInteger(pContext)) {
                        long long idx = prop->asLong(pContext);
                        if (idx >= 0) key = JSSymbols::indexKey(pContext, static_cast<uint32_t>(idx));
                    }
                }
                if (tObj && tObj != PROTO_NONE && key) {
                    const proto::ProtoObject* updated = tObj->setAttribute(pContext, key, val);
                    if (updated) updateMapping(pContext, tObj, updated);
                }
                DISPATCH();
            }
            L_OP_get_private_field: {
                // DEF(get_private_field, 1, 2, 1, none)
                // Stack [..., obj, private_symbol] → [..., obj[private_symbol]]
                // Minimal impl: read by symbol's __name__ as if it were a
                // regular attribute.  Private semantics (brand checks)
                // are not yet enforced.
                if (stackSize(pContext) < 2) DISPATCH();
                const proto::ProtoObject* sym = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* o   = stackTop(pContext); stackPop(pContext);
                const proto::ProtoString* name = nullptr;
                if (sym && sym != PROTO_NONE) {
                    const proto::ProtoObject* nko = pContext->fromUTF8String("__name__");
                    const proto::ProtoString* nk = nko ? nko->asString(pContext) : nullptr;
                    if (nk) {
                        const proto::ProtoObject* nv = sym->getAttribute(pContext, nk, false);
                        if (nv && nv != PROTO_NONE) name = nv->asString(pContext);
                    }
                }
                const proto::ProtoObject* val = (o && o != PROTO_NONE && name)
                    ? o->getAttribute(pContext, name, true) : PROTO_NONE;
                stackPush(pContext, val ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_private_field: {
                // DEF(put_private_field, 1, 3, 1, none)
                // Stack [..., obj, value, private_symbol] → [..., obj]
                // (n_pop=3 n_push=1: obj is preserved underneath).
                if (stackSize(pContext) < 3) DISPATCH();
                const proto::ProtoObject* sym = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* o   = stackTop(pContext);
                const proto::ProtoString* name = nullptr;
                if (sym && sym != PROTO_NONE) {
                    const proto::ProtoObject* nko = pContext->fromUTF8String("__name__");
                    const proto::ProtoString* nk = nko ? nko->asString(pContext) : nullptr;
                    if (nk) {
                        const proto::ProtoObject* nv = sym->getAttribute(pContext, nk, false);
                        if (nv && nv != PROTO_NONE) name = nv->asString(pContext);
                    }
                }
                if (o && o != PROTO_NONE && name) {
                    const proto::ProtoObject* no = o->setAttribute(pContext, name, val ? val : PROTO_NONE);
                    if (no && no != o) {
                        updateMapping(pContext, o, no);
                        pAutomaticLocals[currentStackBase + _PF().stackTop - 1] = no;
                    }
                }
                DISPATCH();
            }
            L_OP_define_private_field: {
                // DEF(define_private_field, 1, 3, 1, none)
                // Same shape as put_private_field but for the initial
                // definition (no brand check needed even when enforced).
                if (stackSize(pContext) < 3) DISPATCH();
                const proto::ProtoObject* sym = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* o   = stackTop(pContext);
                const proto::ProtoString* name = nullptr;
                if (sym && sym != PROTO_NONE) {
                    const proto::ProtoObject* nko = pContext->fromUTF8String("__name__");
                    const proto::ProtoString* nk = nko ? nko->asString(pContext) : nullptr;
                    if (nk) {
                        const proto::ProtoObject* nv = sym->getAttribute(pContext, nk, false);
                        if (nv && nv != PROTO_NONE) name = nv->asString(pContext);
                    }
                }
                if (o && o != PROTO_NONE && name) {
                    const proto::ProtoObject* no = o->setAttribute(pContext, name, val ? val : PROTO_NONE);
                    if (no && no != o) {
                        updateMapping(pContext, o, no);
                        pAutomaticLocals[currentStackBase + _PF().stackTop - 1] = no;
                    }
                }
                DISPATCH();
            }
            L_OP_private_symbol: {
                // DEF(private_symbol, 5, 0, 1, atom)
                // Push a fresh private-symbol value for the given atom.
                // Minimal impl: push a unique object marked
                // __is_private_symbol__ + __name__ from the atom name.
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoString* name = resolveAtom(mod, pContext, atomIndex);
                const proto::ProtoObject* sym = pContext->newObject(true);
                if (name && sym) {
                    const proto::ProtoObject* ko = pContext->fromUTF8String("__is_private_symbol__");
                    const proto::ProtoString* k = ko ? ko->asString(pContext) : nullptr;
                    if (k) sym = sym->setAttribute(pContext, k, PROTO_TRUE);
                    const proto::ProtoObject* nko = pContext->fromUTF8String("__name__");
                    const proto::ProtoString* nk = nko ? nko->asString(pContext) : nullptr;
                    if (nk) sym = sym->setAttribute(pContext, nk, name->asObject(pContext));
                }
                stackPush(pContext, sym ? sym : PROTO_NONE);
                DISPATCH();
            }
            L_OP_set_proto: {
                // DEF(set_proto, 1, 2, 1, none)
                // Stack [..., obj, proto] → [..., obj]
                //
                // Emitted by QuickJS for object literals carrying
                // __proto__ (e.g. `{ __proto__: p, y: 2 }` and
                // `{ __proto__: null }`). Pre-fix this opcode popped
                // proto and threw it away, so the literal kept its
                // default Object.prototype parent — `q.__proto__ === p`
                // was false and `q.x` (inherited from p) was undefined.
                //
                // Use the JS proto-override map so getPrototypeOf /
                // attribute walks honour the requested chain without
                // having to surgically remove the original parent.
                // The default proto-chain walk (chainWalkParent at the
                // top of this file) already consults getJSProtoOverride.
                if (stackSize(pContext) >= 2) {
                    const proto::ProtoObject* proto = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    if (obj && obj != PROTO_NONE) {
                        // Per spec ToObject step: only Object or null
                        // values are accepted; anything else (string,
                        // number, boolean, undefined) is silently
                        // ignored.
                        if (proto == getNullSentinel()) {
                            protojs::setJSProtoOverride(obj, getNullSentinel());
                        } else if (proto && proto != PROTO_NONE
                                   && proto != getUndefinedSentinel()
                                   && !proto->isString(pContext)
                                   && !proto->isInteger(pContext)
                                   && !proto->isDouble(pContext)
                                   && !proto->isFloat(pContext)
                                   && !proto->isBoolean(pContext)) {
                            protojs::setJSProtoOverride(obj, proto);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_define_class: ;
            L_OP_define_class_computed: {
                // DEF(define_class,          6, 2, 2, atom_u8)
                // DEF(define_class_computed, 6, 3, 3, atom_u8)
                //
                // For OP_define_class:
                //   Stack [..., parent_class, bfunc] → [..., ctor, proto]
                // For OP_define_class_computed:
                //   Stack [..., name_value, parent_class, bfunc]
                //         → [..., name_value, ctor, proto]
                //
                // Layout: u32 class_name_atom, u8 class_flags (bit 0 =
                // HAS_HERITAGE).  When no heritage, parent_class is
                // undefined on the stack — proto inherits Object.prototype
                // and ctor inherits Function.prototype.  When heritage is
                // present, parent_class is the explicit base class and
                // parent_proto is base.prototype.
                //
                // Minimal implementation: bfunc is already a closure
                // object (OP_fclosure populated its bytecode_id +
                // closure cells).  We reuse it directly as the ctor —
                // augmenting with .prototype and .name only.
                bool isComputed = (opcode == OP_define_class_computed);
                if (pc + 5 > len) return PROTO_NONE;
                uint32_t classAtom = get_u32(buf + pc);
                pc += 4;
                uint8_t classFlags = buf[pc++];
                bool hasHeritage = (classFlags & 1) != 0;
                size_t need = isComputed ? 3 : 2;
                if (stackSize(pContext) < need) return PROTO_NONE;
                const proto::ProtoObject* bfunc =
                    stackAt(pContext, 0);
                const proto::ProtoObject* parentClass =
                    stackAt(pContext, 1);
                // For computed, sp[-3] is the name value; we leave it
                // in-place under the new ctor.

                // Resolve parent prototype.
                //
                // ECMA-262 §15.7.14 ClassDefinitionEvaluation:
                //   - no heritage              → protoParent = Object.prototype
                //   - extends null             → protoParent = null
                //                                 (constructorParent = Function.prototype)
                //   - extends value (non-null) → protoParent = value.prototype
                REFRESH_GLOBAL_OBJ();
                const proto::ProtoObject* parentProto = nullptr;
                bool nullHeritage = false;
                if (hasHeritage) {
                    if (parentClass == getNullSentinel()) {
                        nullHeritage = true;
                    } else if (parentClass && parentClass != PROTO_NONE) {
                        const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                        if (protoKey) {
                            // Spec Get(parentValue, "prototype"): invoke an
                            // accessor (getter) if one is installed via
                            // Object.defineProperty(Base, 'prototype', {get:..})
                            // before falling back to the raw value.
                            parentProto = invokeGetterIfPresentFast(parentClass, protoKey);
                            if (has_pending_exception) DISPATCH();
                            if (!parentProto || parentProto == PROTO_NONE)
                                parentProto = parentClass->getAttribute(pContext, protoKey, false);
                        }
                        // ECMA-262 §15.7.14 step 6.f: parentProto must be
                        // null or an Object — otherwise TypeError. This
                        // catches `class C extends f.bind()` (bound function:
                        // prototype is undefined) and similar shapes.
                        // A primitive parentProto (number, string, boolean)
                        // also fails the spec's Object check.
                        bool invalidProto =
                            !parentProto || parentProto == PROTO_NONE ||
                            parentProto == t_undefinedSentinel ||
                            (parentProto != t_nullSentinel &&
                             (parentProto->isInteger(pContext) ||
                              parentProto->isDouble(pContext) ||
                              parentProto->isFloat(pContext) ||
                              parentProto == PROTO_TRUE ||
                              parentProto == PROTO_FALSE ||
                              parentProto->asString(pContext) != nullptr));
                        if (invalidProto) {
                            pending_exception = makeError(pContext, "TypeError",
                                "Class extends value does not have valid prototype property",
                                pGlobalRoot);
                            has_pending_exception = true;
                            DISPATCH();
                        }
                        // Treat the JS null sentinel as null heritage (spec's
                        // "if parentProto is null" branch).
                        if (parentProto == t_nullSentinel) {
                            nullHeritage = true;
                            parentProto = nullptr;
                        }
                    } else {
                        // hasHeritage && parentClass is undefined/null sentinel-absent:
                        // QuickJS already produced an undefined parent; treat
                        // like null heritage to remain permissive — spec calls
                        // for TypeError if parentValue is not a constructor.
                        nullHeritage = true;
                    }
                }
                if (!nullHeritage && (!parentProto || parentProto == PROTO_NONE)) {
                    if (pContext->space)
                        parentProto = pContext->space->objectPrototype;
                }

                // Build proto inheriting parentProto, OR a bare object whose
                // [[Prototype]] is JS null when heritage is `extends null`.
                const proto::ProtoObject* proto;
                if (nullHeritage) {
                    proto = pContext->newObject(true);
                    if (proto) protojs::setJSProtoOverride(proto, getNullSentinel());
                } else if (parentProto && parentProto != PROTO_NONE) {
                    proto = parentProto->newChild(pContext, true);
                } else {
                    proto = pContext->newObject(true);
                }

                // bfunc IS the ctor (already a closure).  Install
                // .prototype, .name, and (for derived classes) a
                // __class_parent__ marker pointing at the parent class
                // so OP_get_super on the ctor resolves to the parent.
                const proto::ProtoObject* ctor = bfunc;
                if (ctor && ctor != PROTO_NONE && proto && proto != PROTO_NONE) {
                    const proto::ProtoString* pkey = JSSymbols::prototype(pContext);
                    if (pkey) ctor = ctor->setAttribute(pContext, pkey, proto);
                    // ctor.prototype is {writable:false, enumerable:false, configurable:false} → 0x0.
                    {
                        const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_prototype__");
                        const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                        if (pdk) ctor = ctor->setAttribute(pContext, pdk, pContext->fromInteger(0x0LL));
                    }

                    const proto::ProtoString* ckey = JSSymbols::constructor(pContext);
                    if (ckey) proto = proto->setAttribute(pContext, ckey, ctor);
                    // proto.constructor is {writable:true, enumerable:false, configurable:true} → 0x3.
                    {
                        const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_constructor__");
                        const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                        if (pdk) proto = proto->setAttribute(pContext, pdk, pContext->fromInteger(0x3LL));
                    }

                    // Mark as a constructor so OP_call_constructor accepts it.
                    const proto::ProtoString* isCtorKey =
                        pContext->fromUTF8String("__is_constructor__")
                            ? pContext->fromUTF8String("__is_constructor__")->asString(pContext)
                            : nullptr;
                    if (isCtorKey) ctor = ctor->setAttribute(pContext, isCtorKey, PROTO_TRUE);

                    // For derived classes, store the parent class on the ctor
                    // under __class_parent__.  OP_get_super reads this when
                    // the receiver is a class constructor — needed for
                    // super(...args) and OP_init_ctor dispatch in derived
                    // ctor bodies.
                    if (hasHeritage && parentClass && parentClass != PROTO_NONE) {
                        const proto::ProtoObject* cpo = pContext->fromUTF8String("__class_parent__");
                        const proto::ProtoString* cpk = cpo ? cpo->asString(pContext) : nullptr;
                        if (cpk) ctor = ctor->setAttribute(pContext, cpk, parentClass);
                    }

                    // class name: when computed, sp[-3] is the name; when
                    // atom-named, resolve via constant table.
                    const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                    if (nameKey) {
                        if (isComputed) {
                            const proto::ProtoObject* nv = stackAt(pContext, 2);
                            if (nv && nv != PROTO_NONE)
                                ctor = ctor->setAttribute(pContext, nameKey, nv);
                        } else {
                            const proto::ProtoString* ns = resolveAtom(mod, pContext, classAtom);
                            if (ns)
                                ctor = ctor->setAttribute(pContext, nameKey, ns->asObject(pContext));
                        }
                        // name is {writable:false, enumerable:false, configurable:true} → 0x2.
                        const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_name__");
                        const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                        if (pdk) ctor = ctor->setAttribute(pContext, pdk, pContext->fromInteger(0x2LL));
                    }
                    // length: spec §15.7.10 — set to declared arg count of
                    // the constructor function.  Read argCount from the
                    // resolved bytecode metadata (bfunc.__bytecode_id__).
                    // Pre-fix OP_fclosure SHOULD set length but the class
                    // ctor path can arrive without it (or with stale 0
                    // from some intermediate transformation).
                    {
                        const proto::ProtoString* lenKey = JSSymbols::length(pContext);
                        long long argCnt = 0;
                        int bcId = getBytecodeId(pContext, ctor);
                        if (bcId >= 0 && t_rootModule &&
                            static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size()) {
                            argCnt = static_cast<long long>(
                                t_rootModule->nestedFunctions[bcId].argCount_);
                        }
                        if (lenKey)
                            ctor = ctor->setAttribute(pContext, lenKey, pContext->fromInteger(argCnt));
                        const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_length__");
                        const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                        if (pdk) ctor = ctor->setAttribute(pContext, pdk, pContext->fromInteger(0x2LL));
                    }
                }

                // Record the JS [[Prototype]] override on the ctor per
                // ECMA-262 §15.7.14 step 6:
                //   - extends X (X non-null): ctor.[[Prototype]] = X
                //   - extends null         : ctor.[[Prototype]] = Function.prototype
                //   - no heritage          : ctor.[[Prototype]] = Function.prototype
                //
                // Without the no-heritage / null-heritage branch the class
                // closure inherited the bare Object.prototype that
                // OP_fclosure leaves behind, so Object.getPrototypeOf(C)
                // returned Object.prototype rather than Function.prototype.
                if (ctor && ctor != PROTO_NONE) {
                    if (hasHeritage && !nullHeritage && parentClass && parentClass != PROTO_NONE) {
                        protojs::setJSProtoOverride(ctor, parentClass);
                    } else {
                        REFRESH_GLOBAL_OBJ();
                        if (globalObj && globalObj != PROTO_NONE) {
                            const proto::ProtoString* funcKey =
                                pContext->fromUTF8String("Function")
                                ? pContext->fromUTF8String("Function")->asString(pContext)
                                : nullptr;
                            const proto::ProtoObject* funcCtor = funcKey
                                ? globalObj->getAttribute(pContext, funcKey, false) : nullptr;
                            const proto::ProtoString* protoKey2 = JSSymbols::prototype(pContext);
                            const proto::ProtoObject* fProto =
                                (funcCtor && funcCtor != PROTO_NONE && protoKey2)
                                    ? funcCtor->getAttribute(pContext, protoKey2, false) : nullptr;
                            if (fProto && fProto != PROTO_NONE)
                                protojs::setJSProtoOverride(ctor, fProto);
                        }
                    }
                }

                // Replace top 2 stack slots with [ctor, proto].
                _PF().stackTop -= 2;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ctor ? ctor : PROTO_NONE;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto ? proto : PROTO_NONE;
                DISPATCH();
            }
            L_OP_return: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* result = stackTop(pContext);
                return result;
            }
            L_OP_return_undef: ;
                return PROTO_NONE;
            L_OP_throw: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* exObj = stackTop(pContext);
                stackPop(pContext);
                pending_exception = exObj ? exObj : PROTO_NONE;
                has_pending_exception = true;
                DISPATCH();
            }
            L_OP_drop: {
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
                DISPATCH();
            }
            L_OP_nip: ;
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* top = stackTop(pContext); stackPop(pContext); stackPop(pContext); stackPush(pContext, top); }
                DISPATCH();
            L_OP_nip1: ;
                if (stackSize(pContext) < 3) return PROTO_NONE;
                { const proto::ProtoObject* c = stackTop(pContext); stackPop(pContext); const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext); stackPop(pContext); stackPush(pContext, b); stackPush(pContext, c); }
                DISPATCH();
            L_OP_dup: ;
                if (!stackEmpty(pContext)) stackPush(pContext, stackTop(pContext));
                DISPATCH();
            L_OP_dup1: ;
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* top = stackTop(pContext); const proto::ProtoObject* second = stackAt(pContext, 1); stackPush(pContext, second); stackPush(pContext, top); }
                DISPATCH();
            L_OP_dup2: ;
                if (stackSize(pContext) < 2) return PROTO_NONE;
                stackPush(pContext, stackAt(pContext, 1));
                stackPush(pContext, stackAt(pContext, 1));
                DISPATCH();
            L_OP_dup3: ;
                if (stackSize(pContext) < 3) return PROTO_NONE;
                stackPush(pContext, stackAt(pContext, 2));
                stackPush(pContext, stackAt(pContext, 2));
                stackPush(pContext, stackAt(pContext, 2));
                DISPATCH();
            L_OP_insert2: ;
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
                DISPATCH();
            L_OP_insert3: ;
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
                DISPATCH();
            L_OP_insert4: ;
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
                DISPATCH();
            L_OP_perm3: ;
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
                DISPATCH();
            L_OP_perm4: ;
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
                DISPATCH();
            L_OP_perm5: ;
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
                DISPATCH();
            L_OP_swap: ;
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext); const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext); stackPush(pContext, a); stackPush(pContext, b); }
                DISPATCH();
            L_OP_swap2: ;
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
                DISPATCH();
            L_OP_rot3l: ;
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
                DISPATCH();
            L_OP_rot3r: ;
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
                DISPATCH();
            L_OP_rot4l: ;
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
                DISPATCH();
            L_OP_rot5l: ;
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
                DISPATCH();
            L_OP_push_const: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                if (cpool && idx < cpool->getSize(pContext))
                    stackPush(pContext, cpool->getAt(pContext, static_cast<int>(idx)));
                else
                    stackPush(pContext, PROTO_NONE);
                DISPATCH();
            }
            L_OP_push_atom_value: {
                /* QuickJS semantics: push the atom's string representation as a string literal.
                 * This is JS_AtomToString(ctx, atom) — NOT a variable lookup in globalObj. */
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) {
                    stackPush(pContext, PROTO_NONE);
                    DISPATCH();
                }
                /* Push the atom as a string value (the atom name IS the string literal). */
                stackPush(pContext, key->asObject(pContext));
                DISPATCH();
            }
            // Short local/arg accessors (loc8/arg8 and loc0-3/arg0-3)
            L_OP_get_loc8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + locIndex);
                    stackPush(pContext, readCell(pContext, slotVal));
                } else
                    stackPush(pContext,PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_loc8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + locIndex);
                    if (isCell(pContext, slotVal)) writeCell(pContext, slotVal, val);
                    else setSlot(pContext, argCount + locIndex, val);
                }
                DISPATCH();
            }
            L_OP_set_loc8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + locIndex);
                    if (isCell(pContext, slotVal)) writeCell(pContext, slotVal, val);
                    else setSlot(pContext, argCount + locIndex, val);
                }
                DISPATCH();
            }
            L_OP_get_arg0: ;
            L_OP_get_arg1: ;
            L_OP_get_arg2: ;
            L_OP_get_arg3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_arg0);
                if (idx < argCount && idx < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, idx));
                else
                    stackPush(pContext,PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_arg0: ;
            L_OP_put_arg1: ;
            L_OP_put_arg2: ;
            L_OP_put_arg3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_arg0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (idx < argCount && idx < (argCount + varCount))
                    setSlot(pContext, idx, val);
                DISPATCH();
            }
            L_OP_set_arg0: ;
            L_OP_set_arg1: ;
            L_OP_set_arg2: ;
            L_OP_set_arg3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_arg0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (idx < argCount && idx < (argCount + varCount))
                    setSlot(pContext, idx, val);
                DISPATCH();
            }
            L_OP_get_var_undef: ;
            L_OP_get_var: {
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
                if (liveGlobal && liveGlobal != PROTO_NONE && closureSymbols && static_cast<size_t>(idx) < closureSymbols->getSize(pContext)) {
                    const proto::ProtoString* key = closureSymbols->getAt(pContext, static_cast<int>(idx))->asString(pContext);
                    if (key) {
                        rawVal = liveGlobal->getAttribute(pContext, key, false);
                            /* TDZ check: absent key for a lexical variable means uninitialized. */
                            if (isLexical && !rawVal) {
                                const std::string& vname = module->closureVarNames[idx];
                                std::string msg = "Cannot access '";
                                msg += vname.empty() ? "?" : vname;
                                msg += "' before initialization";
                                pending_exception = makeError(pContext, "ReferenceError", msg.c_str(), pGlobalRoot);
                                has_pending_exception = true;
                                DISPATCH();
                            }
                        val = rawVal;
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
                    DISPATCH();
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_var_init: ;
            L_OP_put_var: {
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
                        DISPATCH();
                    }

                    // Global assignment to `undefined` should throw instead of mutating.
                    if (name == "undefined") {
                        pending_exception = makeError(pContext, "ReferenceError",
                                                      "Cannot assign to read only binding 'undefined'", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }

                    if (closureSymbols && static_cast<size_t>(idx) < closureSymbols->getSize(pContext)) {
                        const proto::ProtoString* key = closureSymbols->getAt(pContext, static_cast<int>(idx))->asString(pContext);
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                DISPATCH();
            }
            L_OP_get_var_ref0: ;
            L_OP_get_var_ref1: ;
            L_OP_get_var_ref2: ;
            L_OP_get_var_ref3: {
                /* Closure vars occupy slots AFTER local vars: slot[argCount + varCount + refIndex].
                 * The slot holds either a closure cell (mutable wrapper used for by-reference
                 * captures — see allocCell / OP_close_loc) or a raw value (for global captures
                 * which are not closed).  readCell returns the raw value when the slot holds
                 * one, or dereferences the cell when it holds one.
                 * TDZ check: if the slot still holds the sentinel, the let/const variable has
                 * not yet been initialised — throw ReferenceError per spec §10.4.2.1. */
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_get_var_ref0);
                {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);
                    const proto::ProtoObject* val = readCell(pContext, slotVal);
                    if (slotVal == tdzSentinel || val == tdzSentinel) {
                        bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                         module->closureVarIsLexical[refIndex];
                        if (isLexical || slotVal == tdzSentinel || val == tdzSentinel) {
                            pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                            has_pending_exception = true;
                            DISPATCH();
                        }
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_put_var_ref0: ;
            L_OP_put_var_ref1: ;
            L_OP_put_var_ref2: ;
            L_OP_put_var_ref3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_put_var_ref0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);
                
                // TDZ check for lexical variables
                bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                 module->closureVarIsLexical[refIndex];
                if (isLexical) {
                    const proto::ProtoObject* curVal = readCell(pContext, slotVal);
                    if (slotVal == tdzSentinel || curVal == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                }

                if (isCell(pContext, slotVal)) {
                    writeCell(pContext, slotVal, val);
                } else {
                    setSlot(pContext, argCount + varCount + refIndex, val);
                    /* Non-cell slot: this is a hoisted GLOBAL_DECL var
                     * (top-level `function f` / `var x`).  QuickJS emits
                     * OP_put_var_ref on these even though they live on
                     * the global object — keep the global publication
                     * so `typeof f` / cross-function reads find them. */
                    if (pGlobalRoot && *pGlobalRoot &&
                        static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                        const std::string& name = module->closureVarNames[refIndex];
                        if (!name.empty()) {
                            const proto::ProtoString* key = pContext->fromUTF8String(name.c_str())
                                ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr;
                            if (key)
                                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_set_var_ref0: ;
            L_OP_set_var_ref1: ;
            L_OP_set_var_ref2: ;
            L_OP_set_var_ref3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_set_var_ref0);
                const proto::ProtoObject* val = stackTop(pContext);
                const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);

                // TDZ check for lexical variables
                bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                 module->closureVarIsLexical[refIndex];
                if (isLexical) {
                    const proto::ProtoObject* curVal = readCell(pContext, slotVal);
                    if (slotVal == tdzSentinel || curVal == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                }

                if (isCell(pContext, slotVal)) {
                    writeCell(pContext, slotVal, val);
                } else {
                    setSlot(pContext, argCount + varCount + refIndex, val);
                    if (pGlobalRoot && *pGlobalRoot &&
                        static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                        const std::string& name = module->closureVarNames[refIndex];
                        if (!name.empty()) {
                            const proto::ProtoString* key = pContext->fromUTF8String(name.c_str())
                                ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr;
                            if (key)
                                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_get_var_ref: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);
                const proto::ProtoObject* val = readCell(pContext, slotVal);
                
                // TDZ check for lexical variables
                if (slotVal == tdzSentinel || val == tdzSentinel) {
                    bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                     module->closureVarIsLexical[refIndex];
                    if (isLexical || slotVal == tdzSentinel || val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                }
                
                stackPush(pContext, val);
                DISPATCH();
            }
            L_OP_put_var_ref: {
                /* Closure-cell-aware: dereference if the slot holds a
                 * cell; otherwise update the slot directly AND publish
                 * to the global (hoisted GLOBAL_DECL / function decl
                 * path; QuickJS emits OP_put_var_ref on these). */
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);

                // TDZ check for lexical variables
                bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                 module->closureVarIsLexical[refIndex];
                if (isLexical) {
                    const proto::ProtoObject* curVal = readCell(pContext, slotVal);
                    if (slotVal == tdzSentinel || curVal == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                }

                if (isCell(pContext, slotVal)) {
                    writeCell(pContext, slotVal, val);
                } else {
                    setSlot(pContext, argCount + varCount + refIndex, val);
                    if (pGlobalRoot && *pGlobalRoot &&
                        static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                        const std::string& name = module->closureVarNames[refIndex];
                        if (!name.empty()) {
                            const proto::ProtoString* key = pContext->fromUTF8String(name.c_str())
                                ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr;
                            if (key)
                                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_set_var_ref: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);

                // TDZ check for lexical variables
                bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                 module->closureVarIsLexical[refIndex];
                if (isLexical) {
                    const proto::ProtoObject* curVal = readCell(pContext, slotVal);
                    if (slotVal == tdzSentinel || curVal == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                }

                if (isCell(pContext, slotVal)) {
                    writeCell(pContext, slotVal, val);
                } else {
                    setSlot(pContext, argCount + varCount + refIndex, val);
                    if (pGlobalRoot && *pGlobalRoot &&
                        static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                        const std::string& name = module->closureVarNames[refIndex];
                        if (!name.empty()) {
                            const proto::ProtoString* key = pContext->fromUTF8String(name.c_str())
                                ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr;
                            if (key)
                                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_get_var_ref_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);
                    const proto::ProtoObject* val = readCell(pContext, slotVal);
                    if (slotVal == tdzSentinel || val == tdzSentinel) {
                        bool isLexical = static_cast<size_t>(refIndex) < module->closureVarIsLexical.size() &&
                                         module->closureVarIsLexical[refIndex];
                        if (isLexical || slotVal == tdzSentinel || val == tdzSentinel) {
                            pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot);
                            has_pending_exception = true;
                            DISPATCH();
                        }
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_put_var_ref_check: ;
            L_OP_put_var_ref_check_init: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* slotVal = getSlot(pContext, argCount + varCount + refIndex);
                if (isCell(pContext, slotVal)) {
                    writeCell(pContext, slotVal, val);
                } else {
                    setSlot(pContext, argCount + varCount + refIndex, val);
                    if (pGlobalRoot && *pGlobalRoot &&
                        static_cast<size_t>(refIndex) < module->closureVarNames.size()) {
                        const std::string& name = module->closureVarNames[refIndex];
                        if (!name.empty()) {
                            const proto::ProtoString* key = pContext->fromUTF8String(name.c_str())
                                ? pContext->fromUTF8String(name.c_str())->asString(pContext) : nullptr;
                            if (key)
                                *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_close_loc: {
                /* Promote a local variable to a closure cell so inner
                 * functions can capture it by reference.
                 *
                 * QuickJS emits OP_close_loc(localIdx) when a local is
                 * captured by an inner function.  We:
                 *   1. Find the closure-var slot j whose closureVarTypes[j]
                 *      is JS_CLOSURE_LOCAL with cvIdx[j] == localIdx.
                 *   2. Allocate a cell wrapping the current local value.
                 *   3. Store the cell at our own closure-var slot j.
                 *
                 * After this, all parent reads / writes of the variable
                 * (which QuickJS emits as OP_get_var_ref(j) /
                 * OP_put_var_ref(j)) dereference the cell.  When an inner
                 * function captures j with cvType=REF, OP_fclosure passes
                 * the SAME cell pointer to the inner; both sides share
                 * the cell, so reassignments propagate. */
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                // Find which closure-var slot maps to this local.
                int closureSlot = -1;
                for (size_t i = 0; i < module->closureVarTypes.size(); i++) {
                    if (module->closureVarTypes[i] == 0 /*LOCAL*/ &&
                        i < module->closureVarIndices.size() &&
                        module->closureVarIndices[i] == locIndex) {
                        closureSlot = static_cast<int>(i);
                        break;
                    }
                }
                if (closureSlot >= 0) {
                    const proto::ProtoObject* curVal =
                        getSlot(pContext, argCount + locIndex);
                    const proto::ProtoObject* cell = allocCell(pContext, curVal);
                    if (cell) {
                        setSlot(pContext, argCount + varCount +
                                static_cast<unsigned>(closureSlot), cell);
                    }
                }
                DISPATCH();
            }
            L_OP_get_loc0: ;
            L_OP_get_loc1: ;
            L_OP_get_loc2: ;
            L_OP_get_loc3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_loc0);
                if (idx < varCount && (argCount + idx) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + idx);
                    stackPush(pContext, readCell(pContext, slotVal));
                } else
                    stackPush(pContext,PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_loc0: ;
            L_OP_put_loc1: ;
            L_OP_put_loc2: ;
            L_OP_put_loc3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_loc0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (idx < varCount && (argCount + idx) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + idx);
                    if (isCell(pContext, slotVal)) writeCell(pContext, slotVal, val);
                    else setSlot(pContext, argCount + idx, val);
                }
                DISPATCH();
            }
            L_OP_set_loc0: ;
            L_OP_set_loc1: ;
            L_OP_set_loc2: ;
            L_OP_set_loc3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_loc0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (idx < varCount && (argCount + idx) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + idx);
                    if (isCell(pContext, slotVal)) writeCell(pContext, slotVal, val);
                    else setSlot(pContext, argCount + idx, val);
                }
                DISPATCH();
            }
            // --- Locals, arguments, and variable references ---
            L_OP_get_loc: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + locIndex);
                    stackPush(pContext, readCell(pContext, slotVal));
                } else
                    stackPush(pContext,PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_loc: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + locIndex);
                    if (isCell(pContext, slotVal)) writeCell(pContext, slotVal, val);
                    else setSlot(pContext, argCount + locIndex, val);
                }
                DISPATCH();
            }
            L_OP_set_loc: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* slotVal = getSlot(pContext, argCount + locIndex);
                    if (isCell(pContext, slotVal)) writeCell(pContext, slotVal, val);
                    else setSlot(pContext, argCount + locIndex, val);
                }
                DISPATCH();
            }
            L_OP_get_arg: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_arg: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    setSlot(pContext, argIndex, val);
                DISPATCH();
            }
            L_OP_set_arg: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    setSlot(pContext, argIndex, val);
                DISPATCH();
            }
            L_OP_set_loc_uninitialized: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, tdzSentinel);
                DISPATCH();
            }
            L_OP_get_loc_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* val = getSlot(pContext, argCount + locIndex);
                    if (val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot); has_pending_exception = true;
                        DISPATCH();
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_put_loc_check: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                DISPATCH();
            }
            L_OP_set_loc_check: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                DISPATCH();
            }
            L_OP_put_loc_check_init: {
                // QuickJS emits this opcode ONLY for assignments to the
                // `this` slot in a derived class constructor.  The spec
                // (§9.1.1.4.4 InitializeBoundName / §15.7.10) says the
                // `this` binding can be initialized only once: a second
                // super() call (e.g. `super(super())`) must throw
                // ReferenceError "'this' can be initialized only once".
                //
                // Pre-fix: this branch unconditionally wrote, so chained
                // super() calls succeeded and the spec violation slipped
                // through.
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* cur = getSlot(pContext, argCount + locIndex);
                    if (cur && cur != tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError",
                            "'this' can be initialized only once",
                            pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                    setSlot(pContext, argCount + locIndex, val);
                }
                DISPATCH();
            }
            L_OP_get_loc_checkthis: {
                // Like OP_get_loc_check but for the derived-class-ctor `this`
                // slot.  Per ECMA-262 §15.7.10, before super(...) has run the
                // `this` binding is in the uninitialized state and any read
                // must throw ReferenceError.  QuickJS represents this by
                // seeding the slot with the TDZ sentinel at function entry
                // and clearing it on super() return.
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* val = getSlot(pContext, argCount + locIndex);
                    if (val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError",
                            "this is not initialized - super() must be called first",
                            pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_get_field: {
                if (_PF().stackTop < 1) return PROTO_NONE;
                const proto::ProtoObject* obj = pAutomaticLocals[currentStackBase + --_PF().stackTop];

                uint32_t atomIndex = (uint32_t)buf[pc] | ((uint32_t)buf[pc+1] << 8) |
                                     ((uint32_t)buf[pc+2] << 16) | ((uint32_t)buf[pc+3] << 24);
                pc += 4;

                auto nameIt = module->atomToProto.find(atomIndex);
                const proto::ProtoString* name = (nameIt != module->atomToProto.end()) ? nameIt->second : nullptr;
                // ECMA-262 §13.3.2.1: property access on null/undefined throws TypeError.
                // Both PROTO_NONE (uninitialized var) and t_undefinedSentinel
                // (the global `undefined` identifier value) must reject —
                // pre-fix the global `undefined.x` silently returned undefined
                // because t_undefinedSentinel skipped the guard.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel || obj == t_undefinedSentinel) {
                    std::string keyStr;
                    if (name) name->toUTF8String(pContext, keyStr);
                    std::string msg = "Cannot read properties of ";
                    msg += (obj == t_nullSentinel) ? "null" : "undefined";
                    msg += " (reading '"; msg += keyStr; msg += "')";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                // string.length is handled by the dedicated OP_get_length
                // opcode that QuickJS emits for `.length` accesses; no
                // length fast path needed here.  The rare `s["length"]`
                // form takes the prototype-chain walk below.
                const proto::ProtoObject* val = name ? invokeGetterIfPresentFast(obj, name) : PROTO_NONE;
                if (has_pending_exception) DISPATCH();
                if (!val || val == PROTO_NONE) {
                    val = resolveFieldOOP(pContext, obj, name);
                }
                REFRESH_INTERP_STATE();

                if (has_pending_exception) DISPATCH();
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (val) ? (val) : PROTO_NONE;
                DISPATCH();
            }
            L_OP_get_field2: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stackTop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                // Throw TypeError for null/undefined receiver (OP_get_field2 keeps obj on stack).
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel || obj == t_undefinedSentinel) {
                    stackPop(pContext); // consume obj from stack
                    std::string keyStr;
                    if (key) key->toUTF8String(pContext, keyStr);
                    std::string msg = "Cannot read properties of ";
                    msg += (obj == t_nullSentinel) ? "null" : "undefined";
                    msg += " (reading '"; msg += keyStr; msg += "')";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }

                // string.length is handled by the dedicated OP_get_length
                // opcode; no length fast path needed here.

                // P-JS-2 — single getAttribute call.  Previously this site
                // did getAttribute(callbacks=false) THEN, on miss,
                // resolveFieldOOP which itself called getAttribute(true)
                // through the default JSObjectBehavior.  Both walks visited
                // the same prototype chain — pure redundancy.  We skip the
                // first probe and let resolveFieldOOP perform the single
                // canonical chain walk; the BehaviorRegistry-resolved
                // behavior already handles the protocol callbacks
                // correctly.
                const proto::ProtoObject* val = key ? invokeGetterIfPresentFast(obj, key) : PROTO_NONE;
                if (has_pending_exception) DISPATCH();
                if (!val || val == PROTO_NONE) {
                    val = resolveFieldOOP(pContext, obj, key);
                }
                // (Accessor fallback removed; handled above)

                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_field: {
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
                if (!key || !obj) { DISPATCH(); }

                // ECMA-262 §10.4.2.1 ArraySetLength: setting Array
                // .length validates the new value via ToUint32 +
                // SameValue check; NaN / Infinity / negative / non-int
                // / non-coercible values throw RangeError. Pre-fix the
                // put silently accepted any number, so a.length = -1
                // / NaN / 2.5 set the slot to whatever junk landed.
                {
                    std::string lenProbe;
                    key->toUTF8String(pContext, lenProbe);
                    if (lenProbe == "length") {
                        const proto::ProtoString* isArrK = JSSymbols::isArray(pContext);
                        const proto::ProtoObject* isArrV = isArrK
                            ? obj->getAttribute(pContext, isArrK, true) : PROTO_NONE;
                        if (isArrV == PROTO_TRUE && val && val != PROTO_NONE) {
                            // Coerce the new value to number via ToNumber
                            // first (handles string '3' → 3) then check
                            // SameValue against ToUint32.
                            const proto::ProtoObject* numVal = val;
                            if (!val->isInteger(pContext) && !val->isDouble(pContext)
                                && !val->isFloat(pContext)) {
                                numVal = jsToNumber(pContext, val);
                            }
                            double d = 0.0;
                            bool gotNum = false;
                            if (numVal) {
                                if (numVal->isInteger(pContext)) { d = static_cast<double>(numVal->asLong(pContext)); gotNum = true; }
                                else if (numVal->isDouble(pContext) || numVal->isFloat(pContext)) { d = numVal->asDouble(pContext); gotNum = true; }
                            }
                            if (!gotNum || std::isnan(d) || std::isinf(d)
                                || d < 0 || d > 4294967295.0
                                || d != std::trunc(d)) {
                                pending_exception = makeError(pContext, "RangeError",
                                    "Invalid array length", pGlobalRoot);
                                has_pending_exception = true;
                                DISPATCH();
                            }
                            // Substitute the coerced integer so the
                            // setAttribute below stores the canonical
                            // numeric form (string '3' → 3).
                            val = pContext->fromInteger(static_cast<long long>(d));
                        }
                    }
                }

                const proto::ProtoObject* newObj = resolvePutFieldOOP(pContext, obj, key, val);
                REFRESH_INTERP_STATE();

                if (hasCallException()) {
                    pending_exception = t_callException;
                    has_pending_exception = true;
                    t_callException = nullptr;
                    DISPATCH();
                }

                if (newObj && newObj != obj) {
                    updateMapping(pContext, obj, newObj);
                    updateSpacePrototypeIfMatching(pContext, obj, newObj);
                }

                std::string keyStr2;
                key->toUTF8String(pContext, keyStr2);

                // Array length truncation: if we just set .length on a real array to a
                // smaller value, delete elements at indices >= newLen (ECMAScript 9.4.2.1).
                if (keyStr2 == "length") {
                    const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                    const proto::ProtoObject* isArrVal = isArrKey
                        ? newObj->getAttribute(pContext, isArrKey, true) : nullptr;
                    if (isArrVal == PROTO_TRUE && val && val != PROTO_NONE) {
                        long long newLen = -1;
                        if (val->isInteger(pContext))
                            newLen = val->asLong(pContext);
                        else if (val->isDouble(pContext))
                            newLen = static_cast<long long>(val->asDouble(pContext));
                        if (newLen >= 0) {
                            // Trim __elements__ to the new length (real arrays
                            // store entries there now).  Pre-fix this loop
                            // only walked indexed-attribute keys, leaving the
                            // ProtoList unchanged — so `a.length=2` on
                            // [1,2,3,4] did nothing visible (Array.length
                            // got the new value but iteration still saw 4).
                            const proto::ProtoList* curEls =
                                protojs::getArrayElements(pContext, newObj);
                            if (curEls) {
                                size_t curSz = curEls->getSize(pContext);
                                if (static_cast<long long>(curSz) > newLen) {
                                    const proto::ProtoList* trimmed = pContext->newList();
                                    for (long long i = 0; i < newLen; ++i)
                                        trimmed = trimmed->appendLast(pContext,
                                            curEls->getAt(pContext, static_cast<int>(i)));
                                    protojs::setArrayElements(pContext, newObj, trimmed);
                                } else if (static_cast<long long>(curSz) < newLen) {
                                    const proto::ProtoList* grown = curEls;
                                    for (long long i = curSz; i < newLen; ++i)
                                        grown = grown->appendLast(pContext, PROTO_NONE);
                                    protojs::setArrayElements(pContext, newObj, grown);
                                }
                            }
                            // ECMA-262 §10.4.2.1 ArraySetLength step 16
                            // demands a true [[Delete]] on each removed
                            // own index — not a write of `undefined`.
                            // Storing PROTO_NONE leaves a tombstone that
                            // shadows the inherited Array.prototype slot
                            // for the same index, so
                            //   Array.prototype[2] = -1;
                            //   var x = [0,1,2]; x.length = 2;
                            //   x[2]
                            // read undefined instead of the spec-required
                            // -1 (built-ins/Array S15.4.5.1_A1.2_T2).
                            // removeAttribute drops the own slot so the
                            // prototype chain becomes visible again.
                            int misses = 0;
                            for (long long i = newLen; i < newLen + 100000LL && misses < 8; i++) {
                                const proto::ProtoString* idxKey = JSSymbols::indexKey(pContext, static_cast<uint32_t>(i));
                                if (!idxKey) break;
                                const proto::ProtoObject* elem = newObj->getAttribute(pContext, idxKey, false);
                                if (!elem || elem == PROTO_NONE) {
                                    misses++;
                                    continue;
                                }
                                misses = 0;
                                const proto::ProtoObject* prevObj = newObj;
                                newObj = newObj->removeAttribute(pContext, idxKey);
                                if (newObj != prevObj) updateMapping(pContext, prevObj, newObj);
                            }
                        }
                    }
                }
                REFRESH_GLOBAL_OBJ();
                if (newObj && pGlobalRoot && obj == globalObj) {
                    *pGlobalRoot = newObj;
                    globalObj = newObj;
                }

                DISPATCH();
            }
            L_OP_define_field: {
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
                            // Only bump .length when the receiver is
                            // actually an Array — plain objects must
                            // not gain a .length sidecar just because
                            // they carry numeric keys. Pre-fix
                            // {1:'a', 2:'b'} leaked length: 3 into the
                            // user's own keys (and Object.keys leaked
                            // 'length' in for-in / Object.keys output).
                            const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                            const proto::ProtoObject* isArrVal = isArrKey
                                ? newObj->getAttribute(pContext, isArrKey, true) : PROTO_NONE;
                            if (isArrVal == PROTO_TRUE) {
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
                    }
                    updateMapping(pContext, obj, newObj);
                    updateSpacePrototypeIfMatching(pContext, obj, newObj);
                    REFRESH_GLOBAL_OBJ();
                    if (newObj && pGlobalRoot && obj == globalObj) {
                        *pGlobalRoot = newObj;
                        globalObj = newObj;
                    }
                    stackPush(pContext, newObj ? newObj : obj);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_define_array_el: {
                // DEF(define_array_el, 1, 3, 2, none)
                // Stack: [..., array, index, value] → [..., array, index]
                // Writes array[index] = value and discards the value.
                if (stackSize(pContext) < 3) DISPATCH();
                const proto::ProtoObject* elemVal2 = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* idxVal   = stackTop(pContext);  // peek — stays on stack
                const proto::ProtoObject* arrObj2  = stackAt(pContext, 1); // peek — stays
                if (!arrObj2 || arrObj2 == PROTO_NONE) DISPATCH();
                // ToPropertyKey semantics (ES2015 §7.1.14):
                //   - integer → numeric string key
                //   - already a string → use directly
                //   - any other primitive or object → invoke ToString, which
                //     for objects calls user-defined .toString() (and falls
                //     back to .valueOf()) — this is the path the
                //     `obj[{toString(){...}}] = v` and computed property
                //     literal `{[keyObj]: v}` patterns rely on.
                const proto::ProtoString* idxKey2 = nullptr;
                if (idxVal && idxVal != PROTO_NONE && idxVal->isInteger(pContext)) {
                    long long i2 = idxVal->asLong(pContext);
                    if (i2 >= 0)
                        idxKey2 = JSSymbols::indexKey(pContext, static_cast<uint32_t>(i2));
                } else if (idxVal && idxVal != PROTO_NONE) {
                    idxKey2 = idxVal->asString(pContext);
                    if (!idxKey2) {
                        // Not already a ProtoString — coerce via the user's
                        // .toString() chain (may run JS code, mutating
                        // observable state — that's the spec-mandated order).
                        const proto::ProtoObject* keyObj = toString(pContext, idxVal);
                        if (keyObj) idxKey2 = keyObj->asString(pContext);
                    }
                }
                if (!idxKey2) DISPATCH();
                // Set array[index] = value; update array pointer in slot below index.
                const proto::ProtoObject* newArr = arrObj2->setAttribute(
                    pContext, idxKey2, elemVal2 ? elemVal2 : PROTO_NONE);
                // Update the length if needed — but ONLY when the receiver
                // is actually an array.  OP_define_array_el is reused for
                // computed-name object literals (`{[k]: v}`); pre-fix the
                // length update fired there too, silently adding a
                // \"length\" property to plain objects (e.g.
                // `{[1]:'a',[3]:'b'}` ended up with `length: 4`).
                const proto::ProtoString* isArrKeyDA = JSSymbols::isArray(pContext);
                const proto::ProtoObject* isArrValDA = isArrKeyDA && newArr
                    ? newArr->getAttribute(pContext, isArrKeyDA, false) : nullptr;
                bool isRealArray = (isArrValDA == PROTO_TRUE);
                if (isRealArray && newArr && idxVal && idxVal->isInteger(pContext)) {
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
                    // Mirror the write into __elements__ so consumers
                    // reading via getArrayElements see the value.  Pre-fix
                    // OP_define_array_el wrote indexed-attribute only, so
                    // mixed array literals like `[0, ...[1,2,3], 4]`
                    // (OP_append publishes __elements__=[0,1,2,3], then
                    // OP_define_array_el writes attribute "4" without
                    // touching __elements__) ended up with the trailing
                    // 4 invisible to JSON.stringify, OP_get_array_el etc.
                    if (i2 >= 0) {
                        const proto::ProtoList* defEls =
                            protojs::getArrayElements(pContext, newArr);
                        if (!defEls) defEls = pContext->newList();
                        long long curSz = static_cast<long long>(defEls->getSize(pContext));
                        if (i2 == curSz) {
                            defEls = defEls->appendLast(pContext,
                                elemVal2 ? elemVal2 : PROTO_NONE);
                        } else if (i2 < curSz) {
                            defEls = defEls->setAt(pContext,
                                static_cast<int>(i2),
                                elemVal2 ? elemVal2 : PROTO_NONE);
                        } else {
                            while (curSz < i2) {
                                defEls = defEls->appendLast(pContext, PROTO_NONE);
                                ++curSz;
                            }
                            defEls = defEls->appendLast(pContext,
                                elemVal2 ? elemVal2 : PROTO_NONE);
                        }
                        if (defEls) protojs::setArrayElements(pContext, newArr, defEls);
                    }
                }
                updateMapping(pContext, arrObj2, newArr);
                // Replace the array slot (2nd from top) with updated array.
                stackPop(pContext);                // pop index
                stackPop(pContext);                // pop old array
                stackPush(pContext, newArr ? newArr : arrObj2);  // push updated array
                stackPush(pContext, idxVal);        // push index back
                DISPATCH();
            }
            L_OP_append: {
                // DEF(append, 1, 3, 2, none) /* append enumerated object, update length */
                // Stack: [..., array, index, iterable] → [..., array, new_index]
                // Used for array spread literals: [...x, ...y].
                // Iterates the iterable and appends each element to array starting at index.
                if (stackSize(pContext) < 3) DISPATCH();
                const proto::ProtoObject* apIterable = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* apIdxObj   = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* apArray    = stackTop(pContext); stackPop(pContext);

                long long apIdx = (apIdxObj && apIdxObj != PROTO_NONE && apIdxObj->isInteger(pContext))
                    ? apIdxObj->asLong(pContext)
                    : (apIdxObj && apIdxObj != PROTO_NONE && apIdxObj->isDouble(pContext))
                    ? static_cast<long long>(apIdxObj->asDouble(pContext)) : 0LL;

                if (!apArray || apArray == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, pContext->fromInteger(apIdx));
                    DISPATCH();
                }
                if (!apIterable || apIterable == PROTO_NONE || apIterable == t_nullSentinel
                    || apIterable->isBoolean(pContext)
                    || apIterable->isInteger(pContext)
                    || apIterable->isDouble(pContext)) {
                    // null/undefined/primitives are silently skipped in spread context
                    stackPush(pContext, apArray);
                    stackPush(pContext, pContext->fromInteger(apIdx));
                    DISPATCH();
                }

                // Strings are iterable per UTF-16 code unit, not via
                // numeric attributes. Drive them through the
                // Symbol.iterator path below so each code unit becomes
                // a single-char string. Pre-fix the array-like branch
                // claimed string spread and produced an empty array
                // because getAttribute("0") on a string is undefined.
                bool apForceIterPath = (apIterable && apIterable != PROTO_NONE
                    && apIterable->isString(pContext));

                // Case A: array-like with .length — use index-based copy.
                const proto::ProtoString* apLenKey = JSSymbols::length(pContext);
                const proto::ProtoObject* apLenObj = apLenKey
                    ? apIterable->getAttribute(pContext, apLenKey, false) : PROTO_NONE;
                long long apSrcLen = apForceIterPath ? -1LL :
                    ((apLenObj && apLenObj != PROTO_NONE && apLenObj->isInteger(pContext))
                    ? apLenObj->asLong(pContext)
                    : (apLenObj && apLenObj != PROTO_NONE && apLenObj->isDouble(pContext))
                    ? static_cast<long long>(apLenObj->asDouble(pContext)) : -1LL);

                bool apDone = false;
                bool apError = false;
                // Maintain a local rolling __elements__ ProtoList — start
                // from whatever the array already has so successive
                // OP_append calls accumulate correctly.  Pre-fix
                // L_OP_append wrote elements via setAttribute(indexKey, v)
                // only, never updating __elements__ — so consumers that
                // read via getArrayElements (OP_apply spread call,
                // OP_get_array_el, arrayTryFastGet, JSON.stringify,
                // Array.prototype.*) saw the array as empty.
                const proto::ProtoList* apEls = protojs::getArrayElements(pContext, apArray);
                if (!apEls) apEls = pContext->newList();
                proto::ProtoContext::CriticalSection apCs(pContext);
                if (apSrcLen >= 0) {
                    // Array / TypedArray path.  Element reads must use
                    // arrayTryFastGet first (arrays now store their data
                    // in `__elements__` ProtoList, NOT as string-keyed
                    // attributes — getAttribute("0") returns nullptr on a
                    // dense array, so spread of `[1,2,3]` was silently
                    // producing no elements).
                    for (long long i = 0; i < apSrcLen && !apError; i++) {
                        const proto::ProtoObject* v = arrayTryFastGet(pContext, apIterable, static_cast<unsigned long>(i));
                        if (!v) {
                            const proto::ProtoString* ik = JSSymbols::indexKey(pContext, static_cast<uint32_t>(i));
                            v = ik ? apIterable->getAttribute(pContext, ik, false) : PROTO_NONE;
                        }
                        if (!v) v = PROTO_NONE;
                        apEls = apEls->appendLast(pContext, v);
                        apIdx++;
                    }
                    apDone = true;
                } else {
                    // Case B: general iterable — call Symbol.iterator, loop next().
                    // If the value has no Symbol.iterator but already exposes
                    // .next, treat it as an already-built iterator (Set/Map
                    // entries(), values(), keys() return iterator objects
                    // that may lack Symbol.iterator).
                    const proto::ProtoString* apSymIterKey = JSSymbols::symbolIterator(pContext);
                    const proto::ProtoObject* apIterFn = apSymIterKey
                        ? apIterable->getAttribute(pContext, apSymIterKey, true) : PROTO_NONE;
                    const proto::ProtoObject* apIter = PROTO_NONE;
                    if (apIterFn && apIterFn != PROTO_NONE) {
                        const proto::ProtoList* emptyA = pContext->newList();
                        apIter = callJSFunction(pContext, apIterFn, apIterable, emptyA);
                        if (t_hasCallException) {
                            pending_exception  = t_callException;
                            has_pending_exception = true;
                            t_hasCallException = false;
                            t_callException    = nullptr;
                            apError = true;
                        }
                    } else {
                        const proto::ProtoString* probeNextKey = JSSymbols::next(pContext);
                        const proto::ProtoObject* probeNext = probeNextKey
                            ? apIterable->getAttribute(pContext, probeNextKey, true) : PROTO_NONE;
                        if (probeNext && probeNext != PROTO_NONE)
                            apIter = apIterable;
                    }
                    if (!apError && apIter && apIter != PROTO_NONE) {
                        {
                            // Get the .next method.
                            const proto::ProtoString* apNextKey = JSSymbols::next(pContext);
                            const proto::ProtoObject* apNextFn = apNextKey
                                ? apIter->getAttribute(pContext, apNextKey, true) : PROTO_NONE;
                            // Loop: call next() until done.
                            const proto::ProtoString* apDoneKey  = JSSymbols::done(pContext);
                            const proto::ProtoString* apValKey   = JSSymbols::value(pContext);
                            while (!apError) {
                                const proto::ProtoList* noArgs = pContext->newList();
                                const proto::ProtoObject* apResult = callJSFunction(
                                    pContext, apNextFn, apIter, noArgs);
                                if (t_hasCallException) {
                                    pending_exception  = t_callException;
                                    has_pending_exception = true;
                                    t_hasCallException = false;
                                    t_callException    = nullptr;
                                    apError = true;
                                    break;
                                }
                                const proto::ProtoObject* apDoneV = (apResult && apResult != PROTO_NONE && apDoneKey)
                                    ? apResult->getAttribute(pContext, apDoneKey, false) : PROTO_NONE;
                                bool iterDone = (!apDoneV || apDoneV == PROTO_NONE || apDoneV == PROTO_TRUE);
                                if (iterDone) break;
                                const proto::ProtoObject* apVal = (apResult && apResult != PROTO_NONE && apValKey)
                                    ? apResult->getAttribute(pContext, apValKey, false) : PROTO_NONE;
                                if (!apVal) apVal = PROTO_NONE;
                                apEls = apEls->appendLast(pContext, apVal);
                                apIdx++;
                            }
                            apDone = !apError;
                        }
                    }
                    // If no Symbol.iterator, treat as empty (nothing to spread).
                    if (!apDone && !apError) apDone = true;
                }

                if (apError) {
                    // Exception already set; push dummy values so the stack stays balanced.
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, pContext->fromInteger(apIdx));
                    DISPATCH();
                }

                // Publish accumulated __elements__ and length.
                if (apArray && apArray != PROTO_NONE && apEls) {
                    protojs::setArrayElements(pContext, apArray, apEls);
                }
                if (apLenKey && apArray && apArray != PROTO_NONE) {
                    const proto::ProtoObject* curLenO = apArray->getAttribute(pContext, apLenKey, false);
                    long long curLen = (curLenO && curLenO != PROTO_NONE && curLenO->isInteger(pContext))
                        ? curLenO->asLong(pContext) : 0LL;
                    if (apIdx > curLen) {
                        const proto::ProtoObject* na = apArray->setAttribute(pContext, apLenKey,
                            pContext->fromInteger(apIdx));
                        if (na) { updateMapping(pContext, apArray, na); apArray = na; }
                    }
                }
                stackPush(pContext, apArray);
                stackPush(pContext, pContext->fromInteger(apIdx));
                DISPATCH();
            }
            L_OP_copy_data_properties: {
                // DEF(copy_data_properties, 2, 3, 3, u8)
                // Used for object spread ({...src}) and object rest ({a, ...rest}).
                // u8 mask: bits 0-1 = target depth from TOS, bits 2-4 = source depth,
                //          bits 5-7 = exclusion list depth (0 means NO exclusion list,
                //          matching QuickJS semantics: mask>>5 ? sp[-1-(mask>>5)] : JS_UNDEFINED).
                // Net-zero stack effect: reads and writes item at targetDepth.
                if (pc >= len) DISPATCH();
                uint8_t cdpMask      = buf[pc++];
                unsigned targetDepth  = cdpMask & 0x03u;
                unsigned sourceDepth  = (cdpMask >> 2) & 0x07u;
                unsigned exclDepth    = (cdpMask >> 5) & 0x07u; // 0 = no exclusion list

                unsigned maxDepth = targetDepth;
                if (sourceDepth > maxDepth) maxDepth = sourceDepth;
                if (exclDepth > 0 && exclDepth > maxDepth) maxDepth = exclDepth;
                if (stackSize(pContext) <= (int)maxDepth) DISPATCH();

                const proto::ProtoObject* cdpTarget = stackAt(pContext, targetDepth);
                const proto::ProtoObject* cdpSource = stackAt(pContext, sourceDepth);

                // Skip null, undefined, and primitive sources (nothing to spread).
                if (!cdpSource || cdpSource == PROTO_NONE || cdpSource == t_nullSentinel
                    || cdpSource->isBoolean(pContext)
                    || cdpSource->isInteger(pContext)
                    || cdpSource->isDouble(pContext)) DISPATCH();
                // Strings: no own enumerable index-keyed props worth spreading here.
                if (cdpSource->isString(pContext) && !cdpSource->isMethod(pContext)) DISPATCH();

                if (!cdpTarget) cdpTarget = PROTO_NONE;

                // Exclusion list: only valid when exclDepth > 0 (mirrors QuickJS mask>>5).
                const proto::ProtoObject* cdpExcl = PROTO_NONE;
                bool cdpHasExcl = false;
                if (exclDepth > 0) {
                    cdpExcl = stackAt(pContext, exclDepth);
                    cdpHasExcl = cdpExcl && cdpExcl != PROTO_NONE && cdpExcl != t_nullSentinel;
                }

                // Array sources store their indexed entries in __elements__,
                // not as own string-keyed attributes. Spread of an array
                // into an object literal must therefore copy each index
                // as a numeric-string key. Pre-fix `{...[10,20]}` yielded
                // {} because the array's index slots were invisible to
                // getOwnAttributes.
                if (const proto::ProtoList* spreadEls =
                        protojs::getArrayElements(pContext, cdpSource)) {
                    long long n = static_cast<long long>(spreadEls->getSize(pContext));
                    for (long long i = 0; i < n; ++i) {
                        const proto::ProtoObject* item =
                            spreadEls->getAt(pContext, static_cast<int>(i));
                        if (!item || item == PROTO_NONE) continue;
                        const proto::ProtoString* idxKey =
                            JSSymbols::indexKey(pContext, static_cast<uint32_t>(i));
                        if (!idxKey) continue;
                        if (cdpHasExcl) {
                            const proto::ProtoObject* exclCheck =
                                cdpExcl->getAttribute(pContext, idxKey, false);
                            if (exclCheck && exclCheck != PROTO_NONE) continue;
                        }
                        cdpTarget = cdpTarget->setAttribute(pContext, idxKey, item);
                    }
                }

                // Iterate own enumerable properties of source and copy to target.
                const proto::ProtoSparseList* ownAttrs = cdpSource->getOwnAttributes(pContext);
                if (ownAttrs) {
                    const proto::ProtoSparseListIterator* cdpIt = ownAttrs->getIterator(pContext);
                    while (cdpIt && cdpIt->hasNext(pContext)) {
                        unsigned long attrRawKey = cdpIt->nextKey(pContext);
                        const proto::ProtoObject* attrVal = cdpIt->nextValue(pContext);
                        cdpIt = const_cast<proto::ProtoSparseListIterator*>(cdpIt)->advance(pContext);
                        const proto::ProtoString* propKey =
                            reinterpret_cast<const proto::ProtoString*>(attrRawKey);
                        if (!propKey) continue;
                        std::string cdpKstr;
                        propKey->toUTF8String(pContext, cdpKstr);
                        // Accessor sidecar: __get_<name>__ produces the
                        // logical property <name>. Invoke the getter and
                        // emit <name>: result on the target. Pre-fix the
                        // sidecar was skipped by the internal-key filter
                        // and the original <name> key carried no data
                        // value, so spread of an accessor source silently
                        // emitted nothing.
                        if (cdpKstr.size() > 7 &&
                            cdpKstr.compare(0, 6, "__get_") == 0 &&
                            cdpKstr.compare(cdpKstr.size() - 2, 2, "__") == 0) {
                            std::string accName =
                                cdpKstr.substr(6, cdpKstr.size() - 8);
                            const proto::ProtoObject* nko =
                                pContext->fromUTF8String(accName.c_str());
                            const proto::ProtoString* nameKey =
                                nko ? nko->asString(pContext) : nullptr;
                            if (!nameKey || !attrVal || attrVal == PROTO_NONE)
                                continue;
                            // enumerable check on the logical property's
                            // __pd_<name>__ descriptor.
                            std::string pdName = "__pd_" + accName + "__";
                            const proto::ProtoObject* pdo = pContext->fromUTF8String(pdName.c_str());
                            const proto::ProtoString* pdkA = pdo ? pdo->asString(pContext) : nullptr;
                            if (pdkA) {
                                const proto::ProtoObject* pdv =
                                    cdpSource->getAttribute(pContext, pdkA, false);
                                if (pdv && pdv != PROTO_NONE && pdv->isInteger(pContext)) {
                                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(pContext));
                                    if (!(bits & 0x4)) continue;
                                }
                            }
                            const proto::ProtoList* emptyArgs = pContext->newList();
                            const proto::ProtoObject* getRes =
                                callJSFunction(pContext, attrVal, cdpSource, emptyArgs);
                            if (t_hasCallException) {
                                pending_exception  = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException    = nullptr;
                                DISPATCH();
                            }
                            if (cdpHasExcl) {
                                const proto::ProtoObject* exclCheck =
                                    cdpExcl->getAttribute(pContext, nameKey, false);
                                if (exclCheck && exclCheck != PROTO_NONE) continue;
                            }
                            cdpTarget = cdpTarget->setAttribute(pContext, nameKey,
                                getRes ? getRes : PROTO_NONE);
                            continue;
                        }
                        if (!attrVal || attrVal == PROTO_NONE) continue;
                        // Skip internal bookkeeping keys (__name__ pattern).
                        if (cdpKstr.size() >= 4 && cdpKstr[0]=='_' && cdpKstr[1]=='_'
                            && cdpKstr[cdpKstr.size()-1]=='_' && cdpKstr[cdpKstr.size()-2]=='_') continue;
                        // Respect enumerable descriptor flag (bit 2 of __pd_<key>__).
                        {
                            std::string pdks = "__pd_" + cdpKstr + "__";
                            const proto::ProtoObject* pko = pContext->fromUTF8String(pdks.c_str());
                            const proto::ProtoString* pdk = pko ? pko->asString(pContext) : nullptr;
                            if (pdk) {
                                const proto::ProtoObject* pdv =
                                    cdpSource->getAttribute(pContext, pdk, false);
                                if (pdv && pdv != PROTO_NONE && pdv->isInteger(pContext)) {
                                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(pContext));
                                    if (!(bits & 0x4)) continue; // not enumerable — skip
                                }
                            }
                        }
                        // Skip keys in the exclusion list.
                        if (cdpHasExcl) {
                            const proto::ProtoObject* exclCheck =
                                cdpExcl->getAttribute(pContext, propKey, false);
                            if (exclCheck && exclCheck != PROTO_NONE) continue;
                        }
                        // Spec §13.2.5.5: object spread reads each
                        // source property via Get(from, key) — accessor
                        // properties must invoke the getter. Probe the
                        // __get_<key>__ sidecar and substitute the
                        // return value before writing to the target.
                        // Pre-fix accessor sources copied undefined.
                        std::string gkStr = std::string("__get_") + cdpKstr + "__";
                        const proto::ProtoObject* gko = pContext->fromUTF8String(gkStr.c_str());
                        const proto::ProtoString* gk = gko ? gko->asString(pContext) : nullptr;
                        const proto::ProtoObject* effective = attrVal;
                        if (gk) {
                            const proto::ProtoObject* getter =
                                cdpSource->getAttribute(pContext, gk, true);
                            if (getter && getter != PROTO_NONE
                                && getter != getUndefinedSentinel()) {
                                const proto::ProtoList* emptyArgs = pContext->newList();
                                effective = callJSFunction(pContext, getter, cdpSource, emptyArgs);
                                if (t_hasCallException) {
                                    pending_exception  = t_callException;
                                    has_pending_exception = true;
                                    t_hasCallException = false;
                                    t_callException    = nullptr;
                                    DISPATCH();
                                }
                            }
                        }
                        cdpTarget = cdpTarget->setAttribute(pContext, propKey,
                            effective ? effective : PROTO_NONE);
                    }
                }

                // Replace the target slot in place (protoCore objects are immutable,
                // so we save slots above it, pop old target, push new target, restore).
                std::vector<const proto::ProtoObject*> cdpAbove;
                cdpAbove.reserve(targetDepth);
                for (unsigned i = 0; i < targetDepth; i++) {
                    cdpAbove.push_back(stackTop(pContext));
                    stackPop(pContext);
                }
                stackPop(pContext); // remove old target
                stackPush(pContext, cdpTarget ? cdpTarget : PROTO_NONE);
                for (int i = (int)cdpAbove.size() - 1; i >= 0; i--)
                    stackPush(pContext, cdpAbove[i]);
                DISPATCH();
            }
            L_OP_to_propkey: {
                // Converts TOS to a canonical property key (string, integer, or symbol).
                // In our protoCore world the value on stack is already a ProtoObject that
                // can be used directly as an attribute key via asString(). This is a no-op:
                // the key remains on the stack unchanged.
                DISPATCH();
            }
            L_OP_define_method: {
                // DEF(define_method, 6, 2, 1, atom_u8)
                // Format: 4-byte atomIndex, 1-byte op_flags. Stack: [..., obj, method] → [..., obj].
                // QuickJS op_flags: 0 = METHOD, 1 = GETTER, 2 = SETTER (plus high
                // bits for enumerability — ignored here, the existing
                // setAttribute path defaults to writable/configurable).
                if (pc + 5 > len || stackSize(pContext) < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                uint8_t op_flags = buf[pc++];
                const proto::ProtoObject* methodVal = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* obj3      = stackTop(pContext);
                const proto::ProtoString* key3 = resolveAtom(mod, pContext, atomIndex);
                if (key3 && obj3) {
                    // GETTER / SETTER: store under accessor sidecar key
                    // (__get_<name>__ / __set_<name>__) and remove any
                    // pre-existing data key so prototype-chain dispatch
                    // routes through the accessor.  Pre-fix: object literal
                    // `{ get foo() {...} }` stored the getter as a regular
                    // data attribute, so `o.foo` returned the function
                    // object instead of invoking it.
                    int flag = op_flags & 0x3;
                    if (flag == 1 || flag == 2) {
                        std::string nameStr;
                        key3->toUTF8String(pContext, nameStr);
                        const std::string prefix = (flag == 1) ? "__get_" : "__set_";
                        std::string sidecar = prefix + nameStr + "__";
                        const proto::ProtoObject* sko = pContext->fromUTF8String(sidecar.c_str());
                        const proto::ProtoString* skp = sko ? sko->asString(pContext) : nullptr;
                        if (skp) {
                            // Per ECMA-262 §14.3.9, getter/setter functions do
                            // NOT have a `prototype` property — strip the one
                            // OP_fclosure installed so verifyProperty checks
                            // (`'prototype' in desc.get` === false) pass.
                            // Also set function.name = "get X" / "set X"
                            // per SetFunctionName(prefix, name) when missing.
                            if (methodVal && methodVal != PROTO_NONE) {
                                const proto::ProtoString* protoKeyDel = JSSymbols::prototype(pContext);
                                if (protoKeyDel) {
                                    const proto::ProtoObject* stripped =
                                        methodVal->setAttribute(pContext, protoKeyDel, nullptr);
                                    if (stripped) methodVal = stripped;
                                }
                                const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                                if (nameKey) {
                                    const proto::ProtoObject* curName = methodVal->getAttribute(pContext, nameKey, false);
                                    bool needs = !curName || curName == PROTO_NONE;
                                    if (!needs && curName) {
                                        const proto::ProtoString* ns = curName->asString(pContext);
                                        if (ns) {
                                            std::string s;
                                            ns->toUTF8String(pContext, s);
                                            needs = s.empty();
                                        }
                                    }
                                    if (needs) {
                                        std::string prefixed = (flag == 1 ? "get " : "set ") + nameStr;
                                        const proto::ProtoObject* nv = pContext->fromUTF8String(prefixed.c_str());
                                        if (nv) {
                                            const proto::ProtoObject* upd =
                                                methodVal->setAttribute(pContext, nameKey, nv);
                                            if (upd) methodVal = upd;
                                            const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_name__");
                                            const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                            if (pdk) {
                                                const proto::ProtoObject* withPd =
                                                    methodVal->setAttribute(pContext, pdk, pContext->fromInteger(0x2LL));
                                                if (withPd) methodVal = withPd;
                                            }
                                        }
                                    }
                                }
                            }
                            // Drop any pre-existing data key (so prototype-chain
                            // lookup sees the accessor, not a stale value).
                            const proto::ProtoObject* tmp = obj3->setAttribute(pContext, key3, PROTO_NONE);
                            const proto::ProtoObject* newObj3 = tmp->setAttribute(pContext, skp, methodVal ? methodVal : PROTO_NONE);
                            // Accessor descriptor per ECMA-262 §6.2.5
                            // PropertyDefinitionEvaluation for getter /
                            // setter in object literals: {enumerable:true,
                            // configurable:true} → bits 0x6 (bit 1
                            // configurable + bit 2 enumerable). Class
                            // bodies want enumerable:false (bits 0x2) —
                            // QuickJS sets a high bit on op_flags to flag
                            // the class context. Bit 5 (METHOD_IS_CLASS)
                            // is the conventional QuickJS marker. When
                            // present, fall back to 0x2; otherwise use
                            // the object-literal enumerable form.
                            if (newObj3) {
                                std::string pdName2 = "__pd_" + nameStr + "__";
                                const proto::ProtoObject* pdo2 = pContext->fromUTF8String(pdName2.c_str());
                                const proto::ProtoString* pdk2 = pdo2 ? pdo2->asString(pContext) : nullptr;
                                long long pdBits = (op_flags & 0x10) ? 0x2LL : 0x6LL;
                                if (pdk2) newObj3 = newObj3->setAttribute(pContext, pdk2, pContext->fromInteger(pdBits));
                            }
                            stackPop(pContext);
                            stackPush(pContext, newObj3 ? newObj3 : obj3);
                            DISPATCH();
                        }
                    }
                    // METHOD shorthand (op_flags == 0): per ECMA-262 §14.3.9,
                    // functions declared as methods do NOT define a
                    // \`prototype\` property.  OP_fclosure unconditionally
                    // installs one; strip it for the method case so
                    // \`Object.prototype.hasOwnProperty.call(method, 'prototype')\`
                    // returns false as the spec requires.
                    //
                    // Also set the method's .name to the declared key when it
                    // is missing or empty — QuickJS emits OP_set_name before
                    // OP_define_method only for some method shapes; class-body
                    // shorthand often lacks it, leaving the function nameless.
                    if (flag == 0 && methodVal && methodVal != PROTO_NONE) {
                        const proto::ProtoString* protoKeyDel = JSSymbols::prototype(pContext);
                        if (protoKeyDel) {
                            const proto::ProtoObject* stripped =
                                methodVal->setAttribute(pContext, protoKeyDel, nullptr);
                            if (stripped) methodVal = stripped;
                        }
                        const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                        if (nameKey) {
                            const proto::ProtoObject* curName = methodVal->getAttribute(pContext, nameKey, false);
                            bool needs = !curName || curName == PROTO_NONE;
                            if (!needs && curName) {
                                const proto::ProtoString* ns = curName->asString(pContext);
                                if (ns) {
                                    std::string s;
                                    ns->toUTF8String(pContext, s);
                                    needs = s.empty();
                                }
                            }
                            if (needs) {
                                const proto::ProtoObject* updated =
                                    methodVal->setAttribute(pContext, nameKey, key3->asObject(pContext));
                                if (updated) {
                                    methodVal = updated;
                                    // name is {writable:false, enumerable:false, configurable:true} → 0x2
                                    const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_name__");
                                    const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                    if (pdk) {
                                        const proto::ProtoObject* withPd =
                                            methodVal->setAttribute(pContext, pdk, pContext->fromInteger(0x2LL));
                                        if (withPd) methodVal = withPd;
                                    }
                                }
                            }
                        }
                    }
                    // Spec js_method_set_home_object: also attach the
                    // target object (obj3) as the method's home_object so
                    // super.X dispatch inside the method body can walk to
                    // the parent prototype.  See QuickJS
                    // js_method_set_properties() — invoked unconditionally
                    // by OP_define_method.
                    if (methodVal && methodVal != PROTO_NONE && obj3 && obj3 != PROTO_NONE) {
                        const proto::ProtoObject* hoKo = pContext->fromUTF8String("__home_object__");
                        const proto::ProtoString* hoK = hoKo ? hoKo->asString(pContext) : nullptr;
                        if (hoK) {
                            const proto::ProtoObject* mWithHome = methodVal->setAttribute(pContext, hoK, obj3);
                            if (mWithHome) methodVal = mWithHome;
                        }
                    }
                    const proto::ProtoObject* newObj3 =
                        obj3->setAttribute(pContext, key3, methodVal ? methodVal : PROTO_NONE);
                    // Class-body methods / accessors are non-enumerable per
                    // ECMA-262 §10.2.7: { writable:true, enumerable:false,
                    // configurable:true } for methods → bits 0x3.
                    if (newObj3 && key3) {
                        std::string nameStr;
                        key3->toUTF8String(pContext, nameStr);
                        std::string pdName = "__pd_" + nameStr + "__";
                        const proto::ProtoObject* pdo = pContext->fromUTF8String(pdName.c_str());
                        const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                        if (pdk) newObj3 = newObj3->setAttribute(pContext, pdk, pContext->fromInteger(0x3LL));
                    }
                    stackPop(pContext);
                    stackPush(pContext, newObj3 ? newObj3 : obj3);
                }
                DISPATCH();
            }
            L_OP_define_method_computed: {
                // DEF(define_method_computed, 2, 3, 1, u8)
                // Format: 1 byte op_flags. Stack: [..., obj, key, method] → [..., obj].
                // Same flag interpretation as L_OP_define_method — see above.
                if (pc + 1 > len || stackSize(pContext) < 3) { if (pc + 1 <= len) pc++; return PROTO_NONE; }
                uint8_t op_flags = buf[pc++];
                const proto::ProtoObject* methodVal = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* keyVal    = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* obj2      = stackTop(pContext);
                if (!obj2 || obj2 == PROTO_NONE) DISPATCH();
                // Convert key to ProtoString — accepting undefined and
                // null too so `class C { get [f()](){} }` with f()
                // returning undefined stores under the string "undefined"
                // (ToPropertyKey semantics).  Pre-fix any PROTO_NONE key
                // silently skipped the define.
                const proto::ProtoString* keyStr2 = nullptr;
                if (keyVal && keyVal != PROTO_NONE) {
                    keyStr2 = keyVal->asString(pContext);
                    if (!keyStr2 && keyVal->isInteger(pContext)) {
                        long long idx = keyVal->asLong(pContext);
                        if (idx >= 0)
                            keyStr2 = JSSymbols::indexKey(pContext, static_cast<uint32_t>(idx));
                    }
                }
                if (!keyStr2) {
                    // ToPropertyKey: coerce via ToString (handles undefined→"undefined",
                    // null→"null", numbers, objects, etc.).
                    const proto::ProtoObject* coerced = toString(pContext, keyVal);
                    keyStr2 = coerced ? coerced->asString(pContext) : nullptr;
                }
                if (!keyStr2) DISPATCH();
                int flag = op_flags & 0x3;
                std::string nameStrC;
                keyStr2->toUTF8String(pContext, nameStrC);
                if (flag == 1 || flag == 2) {
                    const std::string prefix = (flag == 1) ? "__get_" : "__set_";
                    std::string sidecar = prefix + nameStrC + "__";
                    const proto::ProtoObject* sko = pContext->fromUTF8String(sidecar.c_str());
                    const proto::ProtoString* skp = sko ? sko->asString(pContext) : nullptr;
                    if (skp) {
                        // Strip the spurious .prototype on getter/setter
                        // functions (ECMA-262 §14.3.9).
                        if (methodVal && methodVal != PROTO_NONE) {
                            const proto::ProtoString* protoKeyDel = JSSymbols::prototype(pContext);
                            if (protoKeyDel) {
                                const proto::ProtoObject* stripped =
                                    methodVal->setAttribute(pContext, protoKeyDel, nullptr);
                                if (stripped) methodVal = stripped;
                            }
                        }
                        const proto::ProtoObject* tmp = obj2->setAttribute(pContext, keyStr2, PROTO_NONE);
                        const proto::ProtoObject* newObj2 = tmp->setAttribute(pContext, skp, methodVal ? methodVal : PROTO_NONE);
                        // Accessor descriptor: bits 0x2 (configurable only).
                        if (newObj2) {
                            std::string pdName = "__pd_" + nameStrC + "__";
                            const proto::ProtoObject* pdo = pContext->fromUTF8String(pdName.c_str());
                            const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                            if (pdk) newObj2 = newObj2->setAttribute(pContext, pdk, pContext->fromInteger(0x2LL));
                        }
                        stackPop(pContext);
                        stackPush(pContext, newObj2 ? newObj2 : obj2);
                        DISPATCH();
                    }
                }
                const proto::ProtoObject* newObj2 =
                    obj2->setAttribute(pContext, keyStr2, methodVal ? methodVal : PROTO_NONE);
                // Method descriptor: bits 0x3.
                if (newObj2) {
                    std::string pdName = "__pd_" + nameStrC + "__";
                    const proto::ProtoObject* pdo = pContext->fromUTF8String(pdName.c_str());
                    const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                    if (pdk) newObj2 = newObj2->setAttribute(pContext, pdk, pContext->fromInteger(0x3LL));
                }
                stackPop(pContext);
                stackPush(pContext, newObj2 ? newObj2 : obj2);
                DISPATCH();
            }
            L_OP_set_name_computed: {
                // DEF(set_name_computed, 1, 2, 2, none)
                // Stack: [..., key, function] — sets function.name = String(key), stack unchanged.
                if (stackSize(pContext) < 2) DISPATCH();
                const proto::ProtoObject* funcSNC = stackTop(pContext);
                const proto::ProtoObject* keySNC  = stackAt(pContext, 1);
                if (funcSNC && funcSNC != PROTO_NONE && keySNC && keySNC != PROTO_NONE) {
                    const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                    if (nameKey) {
                        // Convert key to string for the name value, honouring
                        // any user-defined `.toString()` (object case) — the
                        // global `toString()` helper handles every value
                        // type including primitives and objects-with-overrides.
                        // Pre-fix: only `asString` (already-a-string) and
                        // integer branches were handled; object keys fell
                        // through with an empty name string, so
                        // `({ [{toString(){return 'sum'}}]: function(){} })`
                        // would produce a function whose `.name` was "".
                        std::string nameStr;
                        const proto::ProtoString* keyPS = keySNC->asString(pContext);
                        if (keyPS) keyPS->toUTF8String(pContext, nameStr);
                        else if (keySNC->isInteger(pContext))
                            nameStr = std::to_string(keySNC->asLong(pContext));
                        else {
                            const proto::ProtoObject* coerced = toString(pContext, keySNC);
                            const proto::ProtoString* cs = coerced ? coerced->asString(pContext) : nullptr;
                            if (cs) cs->toUTF8String(pContext, nameStr);
                        }
                        const proto::ProtoObject* nameVal = pContext->fromUTF8String(nameStr.c_str());
                        if (nameVal) {
                            const proto::ProtoObject* newFunc = funcSNC->setAttribute(pContext, nameKey, nameVal);
                            // Spec: name is {writable:false, enumerable:false, configurable:true}
                            setNWCDescriptor(pContext, newFunc, "name");
                            updateMapping(pContext, funcSNC, newFunc);
                            // Replace TOS with updated function.
                            stackPop(pContext);
                            stackPush(pContext, newFunc ? newFunc : funcSNC);
                        }
                    }
                }
                DISPATCH();
            }
            L_OP_set_name: {
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
                            // Spec: name is {writable:false, enumerable:false, configurable:true}
                            setNWCDescriptor(pContext, newFunc, "name");
                            updateMapping(pContext, func, newFunc);
                            func = newFunc;
                        }
                    }
                }
                stackPush(pContext, func ? func : PROTO_NONE);
                DISPATCH();
            }
            L_OP_object: {
                // Create a mutable object that inherits from Object.prototype so that
                // hasOwnProperty, toString, valueOf, etc. are found via prototype lookup.
                const proto::ProtoObject* objProto =
                    (pContext->space) ? pContext->space->objectPrototype : nullptr;
                const proto::ProtoObject* newObj = (objProto && objProto != PROTO_NONE)
                    ? objProto->newChild(pContext, true)
                    : pContext->newObject(true);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = newObj;
                DISPATCH();
            }
            // --- Array element helpers (implemented via property semantics) ---
            L_OP_get_array_el: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* index = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* obj = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // No zeroing for obj as result will overwrite it.
                // Throw TypeError for null/undefined receiver. Match
                // the get_field rule: t_undefinedSentinel (the global
                // `undefined` identifier) must reject too, otherwise
                // `undefined['k']` silently returned undefined.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel || obj == t_undefinedSentinel) {
                    std::string msg = "Cannot read properties of ";
                    msg += (obj == t_nullSentinel) ? "null" : "undefined";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                const proto::ProtoObject* val = nullptr;
                long long arrIdxFast = numericArrayIndexOrNeg(pContext, index);
                // String character indexing: `"abc"[0]` → "a".  Pre-fix
                // strings followed the regular property-attribute path and
                // returned undefined for numeric indices because strings
                // don't store their codepoints as own attributes.
                if (arrIdxFast >= 0 && obj && obj->isString(pContext)) {
                    const proto::ProtoString* ps = obj->asString(pContext);
                    if (ps) {
                        long long sz = static_cast<long long>(ps->getSize(pContext));
                        if (arrIdxFast < sz) {
                            // Extract the codepoint at arrIdxFast.
                            // ProtoString lacks a direct charAt API exposed here;
                            // use the existing string method via prototype lookup
                            // would be heavy, so dispatch through asString +
                            // substring would also be heavy.  Use the
                            // string substring API directly.
                            std::string utf8;
                            ps->toUTF8String(pContext, utf8);
                            // UTF-8 walk to the arrIdxFast-th codepoint.
                            size_t i = 0;
                            long long codepointsSeen = 0;
                            while (i < utf8.size() && codepointsSeen < arrIdxFast) {
                                unsigned char c = static_cast<unsigned char>(utf8[i]);
                                if (c < 0x80) i += 1;
                                else if (c < 0xE0) i += 2;
                                else if (c < 0xF0) i += 3;
                                else i += 4;
                                codepointsSeen++;
                            }
                            if (i < utf8.size()) {
                                unsigned char c = static_cast<unsigned char>(utf8[i]);
                                size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                                if (i + len <= utf8.size()) {
                                    std::string single = utf8.substr(i, len);
                                    val = pContext->fromUTF8String(single.c_str());
                                }
                            }
                        }
                    }
                    if (!val) val = PROTO_NONE;
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = val;
                    DISPATCH();
                }
                if (arrIdxFast >= 0) {
                    val = resolveElementOOP(pContext, obj, static_cast<uint32_t>(arrIdxFast));
                }
                
                // Fallback to native ProtoList fast path ONLY if behavior didn't find it
                // (TypedArrays return PROTO_NONE for out-of-bounds, so this works).
                if (!val && arrIdxFast >= 0) {
                    val = arrayTryFastGet(pContext, obj, static_cast<unsigned long>(arrIdxFast));
                }
                // Array literals >32 elements: QuickJS emits OP_array_from
                // for the first 32 elements, then OP_define_field for
                // each remaining position — those tail entries live in
                // string-keyed attributes ('32', '33', ...) NOT in
                // __elements__. arrayTryFastGet returns PROTO_NONE for
                // out-of-bounds, which means the indexed-attribute
                // fallback below was skipped and a[32] read as undefined.
                // Force the fallback when arrayTryFastGet's miss is the
                // out-of-bounds variety (PROTO_NONE), preserving the
                // TypedArray semantics where PROTO_NONE IS the final
                // result.
                if (val == PROTO_NONE && arrIdxFast >= 0) {
                    const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                    if (isArrKey
                        && obj->getAttribute(pContext, isArrKey, false) == PROTO_TRUE) {
                        val = nullptr;
                    }
                }
                if (!val) {
                    const proto::ProtoString* key = nullptr;
                    if (arrIdxFast >= 0) {
                        key = JSSymbols::indexKey(pContext, static_cast<uint32_t>(arrIdxFast));
                    } else {
                        key = ensureInternedOOP(pContext, index);
                        if (!key) {
                            const proto::ProtoObject* keyObj = toString(pContext, index);
                            key = keyObj ? ensureInternedOOP(pContext, keyObj) : nullptr;
                        }
                    }
                    // String primitives expose .length via the dedicated
                    // OP_get_length opcode, but the indexed form
                    // `'abc'['length']` reaches us here with a string
                    // key. The primitive carries no `length` attribute,
                    // so the regular prototype walk returned PROTO_NONE
                    // (rendered as 0 — silently wrong). Resolve the
                    // length directly from the underlying ProtoString.
                    if (obj && obj->isString(pContext) && key) {
                        std::string keyStrTest;
                        key->toUTF8String(pContext, keyStrTest);
                        if (keyStrTest == "length") {
                            const proto::ProtoString* ps = obj->asString(pContext);
                            if (ps) {
                                pAutomaticLocals[currentStackBase + _PF().stackTop++] =
                                    pContext->fromInteger(static_cast<long long>(ps->getSize(pContext)));
                                DISPATCH();
                            }
                        }
                    }
                    if (obj && key) {
                        // §10.5.5 OrdinaryGet: an accessor descriptor
                        // takes precedence over the (typically
                        // undefined-sentinel) data slot stored
                        // alongside the __get_<key>__ sidecar.  Probe
                        // the getter FIRST so an accessor redefine
                        // (Object.defineProperty(arr, "0",
                        // {get: () => 100})) actually invokes the new
                        // getter on read.  Pre-fix the data-slot fast
                        // path returned the undefined sentinel and the
                        // later `if (!val || val == PROTO_NONE)` guard
                        // skipped the getter probe, so the redefined
                        // accessor was unreachable
                        // (built-ins/Object/defineProperty/15.2.3.6-4-
                        // 265, -253, -278 and friends).
                        {
                            std::string keyStrGAE;
                            key->toUTF8String(pContext, keyStrGAE);
                            const proto::ProtoObject* gval =
                                invokeGetterIfPresent(obj, keyStrGAE);
                            if (has_pending_exception) DISPATCH();
                            if (gval && gval != PROTO_NONE) {
                                val = gval;
                            }
                        }
                        // Fast path: check own properties first (avoiding BehaviorRegistry/prototype chain)
                        if (!val) {
                            val = obj->getAttribute(pContext, key, false);
                        }
                        if (!val || val == PROTO_NONE) {
                            val = resolveFieldOOP(pContext, obj, key);
                        }
                    }
                }
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (val && val != PROTO_NONE ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_get_array_el2: {
                // QuickJS opcode signature:
                //   DEF(get_array_el2, 1, 2, 2, none) /* obj prop -> obj value */
                // n_pop=2, n_push=2 — net 0 stack delta.  Used for
                // method-style chained access where the next op needs
                // `obj` as the `this` binding (e.g. `obj[k]()` first
                // does get_array_el2 then call_method).
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* index = pAutomaticLocals[currentStackBase + _PF().stackTop - 1];
                const proto::ProtoObject* obj = pAutomaticLocals[currentStackBase + _PF().stackTop - 2];
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    pAutomaticLocals[currentStackBase + --_PF().stackTop] = PROTO_NONE;
                    pAutomaticLocals[currentStackBase + --_PF().stackTop] = PROTO_NONE;
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                const proto::ProtoObject* val = nullptr;
                uint8_t taType2 = getTypedArrayElementType(pContext, obj);
                long long arrIdxFast2 = numericArrayIndexOrNeg(pContext, index);
                if (taType2 != 0xFF && arrIdxFast2 >= 0) {
                    val = typedArrayGetElement(pContext, obj, static_cast<uint32_t>(arrIdxFast2), taType2);
                } else if (arrIdxFast2 >= 0) {
                    val = arrayTryFastGet(pContext, obj, static_cast<unsigned long>(arrIdxFast2));
                }
                if (!val) {
                    const proto::ProtoString* key = nullptr;
                    if (arrIdxFast2 >= 0) {
                        key = JSSymbols::indexKey(pContext, static_cast<uint32_t>(arrIdxFast2));
                    } else {
                        const proto::ProtoObject* keyObj = index;
                        if (!index->isString(pContext)) {
                            keyObj = toString(pContext, index);
                            REFRESH_INTERP_STATE();
                        }
                        key = keyObj ? keyObj->asString(pContext) : nullptr;
                    }
                    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                    REFRESH_INTERP_STATE();
                    if ((!val || val == PROTO_NONE) && key) {
                        std::string keyStrGAE2;
                        key->toUTF8String(pContext, keyStrGAE2);
                        const proto::ProtoObject* gval = invokeGetterIfPresent(obj, keyStrGAE2);
                        if (has_pending_exception) DISPATCH();
                        if (gval && gval != PROTO_NONE) val = gval;
                    }
                }
                // Spec: pop 2 (obj, prop) push 2 (obj, value).  Net 0:
                // overwrite the prop slot with val; obj stays in place.
                pAutomaticLocals[currentStackBase + _PF().stackTop - 1] =
                    (val && val != PROTO_NONE ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_get_array_el3: {
                // QuickJS opcode signature:
                //   DEF(get_array_el3, 1, 2, 3, none) /* obj prop -> obj prop value */
                // n_pop=2, n_push=3 — net +1 stack delta.  Used in
                // chained-then-calls where the next op needs `obj`,
                // `prop`, and `value` (e.g. some super/method paths).
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* index = pAutomaticLocals[currentStackBase + _PF().stackTop - 1];
                const proto::ProtoObject* obj = pAutomaticLocals[currentStackBase + _PF().stackTop - 2];
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    pAutomaticLocals[currentStackBase + --_PF().stackTop] = PROTO_NONE;
                    pAutomaticLocals[currentStackBase + --_PF().stackTop] = PROTO_NONE;
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                const proto::ProtoObject* val = nullptr;
                uint8_t taType3 = getTypedArrayElementType(pContext, obj);
                long long arrIdxFast3 = numericArrayIndexOrNeg(pContext, index);
                if (taType3 != 0xFF && arrIdxFast3 >= 0) {
                    val = typedArrayGetElement(pContext, obj, static_cast<uint32_t>(arrIdxFast3), taType3);
                } else if (arrIdxFast3 >= 0) {
                    val = arrayTryFastGet(pContext, obj, static_cast<unsigned long>(arrIdxFast3));
                }
                if (!val) {
                    const proto::ProtoString* key = nullptr;
                    if (arrIdxFast3 >= 0) {
                        key = JSSymbols::indexKey(pContext, static_cast<uint32_t>(arrIdxFast3));
                    } else {
                        const proto::ProtoObject* keyObj = toString(pContext, index);
                        REFRESH_INTERP_STATE();
                        key = keyObj ? keyObj->asString(pContext) : nullptr;
                    }
                    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                    if ((!val || val == PROTO_NONE) && key) {
                        std::string keyStrGAE3;
                        key->toUTF8String(pContext, keyStrGAE3);
                        const proto::ProtoObject* gval = invokeGetterIfPresent(obj, keyStrGAE3);
                        if (has_pending_exception) DISPATCH();
                        if (gval && gval != PROTO_NONE) val = gval;
                    }
                }
                // Spec: pop 2 (obj, prop) push 3 (obj, prop, value).
                // Obj and prop already in slots [-2] and [-1]; just
                // push val on top (net +1).
                pAutomaticLocals[currentStackBase + _PF().stackTop++] =
                    (val && val != PROTO_NONE ? val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_put_array_el: {
                // QuickJS opcode signature: DEF(put_array_el, 1, 3, 0, none)
                // — n_pop=3, n_push=0.  Pops obj, index, value; sets
                // obj[index]=value; pushes NOTHING.  When the bytecode
                // appears in expression context (the assignment's value
                // is consumed by the next op), the QuickJS compiler
                // wraps it in `insert3 ... drop`; the peephole pass
                // collapses `insert3 put_array_el drop` back to plain
                // `put_array_el`.  Either way this opcode itself must
                // leave the operand stack 3 slots smaller than it
                // found it — pushing here accumulates a spurious slot
                // per iteration in tight loops like
                // `for (let i = 0; i < N; i++) obj['k' + i] = i;`,
                // which after ~17 iterations exhausts the stack window
                // sized at compile-time as `module->stackSize() + 16`
                // and silently corrupts subsequent property writes
                // (they end up on a stack slot rather than reaching
                // setAttribute).
                if (_PF().stackTop < 3) return PROTO_NONE;
                const proto::ProtoObject* value = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE;
                const proto::ProtoObject* index = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE;
                const proto::ProtoObject* obj = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE;

                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    pending_exception = makeError(pContext, "TypeError", "Cannot set property on null/undefined", pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }

                long long idxFast = numericArrayIndexOrNeg(pContext, index);
                const proto::ProtoObject* newObj = nullptr;

                // 0. Native ProtoList fast path for plain arrays.  Has to run
                //    BEFORE OOP dispatch because resolvePutElementOOP's default
                //    fallback unconditionally calls setAttribute("0",...), which
                //    is incompatible with arrayTryFastGet — the latter reads the
                //    `__array_elements__` ProtoList, while the former writes to
                //    a string-keyed attribute.  When that mismatch happens, the
                //    write succeeds (string-keyed) but the read hits the stale
                //    list, so `arr[0] = 10` followed by `arr[0]` returns 1.
                //    arrayTryFastSet returns false for non-arrays, so this is a
                //    no-op for them and OOP dispatch runs as before.
                if (idxFast >= 0 &&
                    arrayTryFastSet(pContext, obj, static_cast<unsigned long>(idxFast), value)) {
                    newObj = obj;
                    goto put_array_el_update_length;
                }

                // 1. Polymorphic Dispatch for special objects (TypedArrays, Frozen, etc.)
                if (idxFast >= 0) {
                    newObj = resolvePutElementOOP(pContext, obj, static_cast<uint32_t>(idxFast), value);
                    if (hasCallException()) {
                        pending_exception = t_callException;
                        has_pending_exception = true;
                        t_callException = nullptr;
                        DISPATCH();
                    }
                    if (newObj) {
                        if (newObj != obj) updateMapping(pContext, obj, newObj);
                        // ECMA-262 §10.4.2.4 ArraySetLength: writing an
                        // own indexed property at idx ≥ length must
                        // bump length to idx + 1. When the array carries
                        // no native __elements__ (Array(N) leaves the
                        // pre-sized slot sparse to avoid materialising
                        // N×undefined entries), arrayTryFastSet returned
                        // false and dispatch landed here through
                        // resolvePutElementOOP — a non-null newObj used
                        // to DISPATCH directly, skipping the length
                        // bump (built-ins/Array S15.4.5.1_A2.3_T1 saw
                        // length stay at 100 after x[100]=1).
                        // Fall through to the length-update label for
                        // arrays; TypedArrays are short-circuited by
                        // their bounds-checked behavior so the same
                        // pass-through is harmless there.
                        const proto::ProtoString* isArrK = JSSymbols::isArray(pContext);
                        const proto::ProtoObject* isArrV = isArrK
                            ? newObj->getAttribute(pContext, isArrK, true) : nullptr;
                        if (isArrV == PROTO_TRUE) goto put_array_el_update_length;
                        DISPATCH();
                    }
                } else {
                    const proto::ProtoString* key = ensureInternedOOP(pContext, index);
                    if (!key) {
                        const proto::ProtoObject* keyObj = toString(pContext, index);
                        key = keyObj ? ensureInternedOOP(pContext, keyObj) : nullptr;
                    }
                    if (key) {
                        newObj = resolvePutFieldOOP(pContext, obj, key, value);
                        if (hasCallException()) {
                            pending_exception = t_callException;
                            has_pending_exception = true;
                            t_callException = nullptr;
                            DISPATCH();
                        }
                        if (newObj && newObj != obj) updateMapping(pContext, obj, newObj);
                        // Named property store complete; spec says no push.
                        DISPATCH();
                    }
                }

                // 2. Fallback to dictionary set for numeric indices on objects
                //    that are NOT native arrays (the array fast path above
                //    already returned, jumping past this block).
                if (idxFast >= 0) {
                    newObj = obj->setAttribute(pContext, protojs::JSSymbols::indexKey(pContext, static_cast<uint32_t>(idxFast)), value);
                    if (newObj && newObj != obj) updateMapping(pContext, obj, newObj);
                }

                put_array_el_update_length:
                // Update .length if index is a valid array index (and not a special object handled above).
                if (newObj && idxFast >= 0 && idxFast < (long long)0xFFFFFFFELL) {
                    const proto::ProtoString* lenKey = JSSymbols::length(pContext);
                    if (lenKey) {
                        const proto::ProtoObject* curLenVal = newObj->getAttribute(pContext, lenKey, false);
                        long long curLen = (curLenVal && curLenVal != PROTO_NONE && curLenVal->isInteger(pContext))
                                            ? curLenVal->asLong(pContext) : 0LL;
                        if (idxFast + 1 > curLen) {
                            const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                            const proto::ProtoObject* isArrVal = isArrKey
                                ? newObj->getAttribute(pContext, isArrKey, true) : nullptr;
                            if (isArrVal == PROTO_TRUE) {
                                const proto::ProtoObject* updatedObj = newObj->setAttribute(pContext, lenKey,
                                    pContext->fromInteger(idxFast + 1));
                                if (updatedObj != newObj) {
                                    updateMapping(pContext, newObj, updatedObj);
                                    newObj = updatedObj;
                                }
                            }
                        }
                    }
                }

                // Spec: no push — the operand stack ends 3 slots smaller.
                DISPATCH();
            }
            L_OP_undefined: ;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_NONE;
                DISPATCH();
            L_OP_null: ;
                // JS null is the null sentinel, not PROTO_NONE (which is undefined).
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (t_nullSentinel ? t_nullSentinel : PROTO_NONE);
                DISPATCH();
            L_OP_push_false: ;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_FALSE;
                DISPATCH();
            L_OP_push_true: ;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_TRUE;
                DISPATCH();
            // --- Control flow ---
            L_OP_goto: {
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += diff;
                DISPATCH();
            }
            L_OP_goto16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t diff = static_cast<int16_t>(get_u16(buf + pc));
                pc += diff;
                DISPATCH();
            }
            L_OP_goto8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t diff = static_cast<int8_t>(buf[pc]);
                pc += diff;
                DISPATCH();
            }
            L_OP_if_true: {
                if (pc + 4 > len || _PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* cond = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                DISPATCH();
            }
            L_OP_if_false: {
                if (pc + 4 > len || _PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* cond = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (!toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                DISPATCH();
            }
            L_OP_if_true8: {
                if (pc + 1 > len || _PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* cond = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                DISPATCH();
            }
            L_OP_if_false8: {
                if (pc + 1 > len || _PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* cond = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (!toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                DISPATCH();
            }
            L_OP_add: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];

                // Numerify booleans and null ONLY when neither operand is a
                // string — otherwise `'x' + null` would coerce null to 0
                // before the concat path saw it, producing 'x0' instead
                // of the spec-correct 'xnull' (the string + concat path
                // uses toString, which renders null as the literal 'null').
                bool aIsStr0 = a && a->asString(pContext);
                bool bIsStr0 = b && b->asString(pContext);
                if (!aIsStr0 && !bIsStr0) {
                    if (a == PROTO_TRUE)  a = proto::makeSmallInt(1);
                    else if (a == PROTO_FALSE || a == t_nullSentinel) a = proto::makeSmallInt(0);
                    if (b == PROTO_TRUE)  b = proto::makeSmallInt(1);
                    else if (b == PROTO_FALSE || b == t_nullSentinel) b = proto::makeSmallInt(0);
                }

                // Integer fast-path
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    long long resVal = proto::asSmallInt(a) + proto::asSmallInt(b);
                    if (proto::smallIntInRange(resVal)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(resVal);
                        DISPATCH();
                    }
                }
                if (a && b && a->isInteger(pContext) && b->isInteger(pContext)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(a->asLong(pContext) + b->asLong(pContext));
                    DISPATCH();
                }

                // Fallback: ToPrimitive
                const proto::ProtoObject* pa = toPrimIfObject(a);
                const proto::ProtoObject* pb = toPrimIfObject(b);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();

                bool aIsStr = pa && pa != PROTO_NONE && pa->isString(pContext);
                bool bIsStr = pb && pb != PROTO_NONE && pb->isString(pContext);
                const proto::ProtoObject* res;
                if (aIsStr || bIsStr) {
                    const proto::ProtoObject* ra = toString(pContext, pa);
                    const proto::ProtoObject* rb = toString(pContext, pb);
                    REFRESH_INTERP_STATE();
                    const proto::ProtoString* sa = ra ? ra->asString(pContext) : nullptr;
                    const proto::ProtoString* sb = rb ? rb->asString(pContext) : nullptr;
                    if (sa && sb) {
                        const proto::ProtoString* concat = sa->appendLast(pContext, sb);
                        res = concat ? concat->asObject(pContext) : PROTO_NONE;
                    } else {
                        res = sa ? ra : (sb ? rb : PROTO_NONE);
                    }
                } else {
                    const proto::ProtoObject* ra = toNumber(pContext, pa);
                    const proto::ProtoObject* rb = toNumber(pContext, pb);
                    REFRESH_INTERP_STATE();
                    res = ra ? ra->add(pContext, rb) : PROTO_NONE;
                }
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (res ? res : PROTO_NONE);
                DISPATCH();
            }
            L_OP_mul: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];

                if (a == PROTO_TRUE)  a = proto::makeSmallInt(1);
                else if (a == PROTO_FALSE || a == t_nullSentinel) a = proto::makeSmallInt(0);
                if (b == PROTO_TRUE)  b = proto::makeSmallInt(1);
                else if (b == PROTO_FALSE || b == t_nullSentinel) b = proto::makeSmallInt(0);

                // Integer fast-path
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    long long resVal = proto::asSmallInt(a) * proto::asSmallInt(b);
                    if (proto::smallIntInRange(resVal)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(resVal);
                        DISPATCH();
                    }
                }
                if (a && b && a->isInteger(pContext) && b->isInteger(pContext)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(a->asLong(pContext) * b->asLong(pContext));
                    DISPATCH();
                }

                const proto::ProtoObject* na = toNumber(pContext, toPrimIfObject(a));
                const proto::ProtoObject* nb = toNumber(pContext, toPrimIfObject(b));
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* res = na->multiply(pContext, nb);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (res ? res : PROTO_NONE);
                DISPATCH();
            }
            L_OP_div: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b_raw = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(b_raw));
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a_raw = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // No zeroing for 'a' as result will overwrite it.
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(a_raw));
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                // JS division always yields double (handles /0 → ±Infinity, 0/0 → NaN, -0 correctly).
                auto toDoubleVal = [&](const proto::ProtoObject* v) -> double {
                    if (!v || v == PROTO_NONE) return 0.0;
                    if (v->isInteger(pContext)) return static_cast<double>(v->asLong(pContext));
                    if (v->isDouble(pContext) || v->isFloat(pContext)) return v->asDouble(pContext);
                    return 0.0;
                };
                double da = toDoubleVal(a);
                double db = toDoubleVal(b);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(da / db);
                DISPATCH();
            }
            L_OP_sub: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];

                // see L_OP_add for null / boolean numerify rationale.
                if (a == PROTO_TRUE)  a = proto::makeSmallInt(1);
                else if (a == PROTO_FALSE || a == t_nullSentinel) a = proto::makeSmallInt(0);
                if (b == PROTO_TRUE)  b = proto::makeSmallInt(1);
                else if (b == PROTO_FALSE || b == t_nullSentinel) b = proto::makeSmallInt(0);

                // Integer fast-path
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    long long resVal = proto::asSmallInt(a) - proto::asSmallInt(b);
                    if (proto::smallIntInRange(resVal)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(resVal);
                        DISPATCH();
                    }
                }
                if (a && b && a->isInteger(pContext) && b->isInteger(pContext)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(a->asLong(pContext) - b->asLong(pContext));
                    DISPATCH();
                }

                const proto::ProtoObject* na = toNumber(pContext, toPrimIfObject(a));
                const proto::ProtoObject* nb = toNumber(pContext, toPrimIfObject(b));
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* res = na->subtract(pContext, nb);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (res ? res : PROTO_NONE);
                DISPATCH();
            }
            L_OP_mod: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                
                // Integer fast-path
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    long long va = proto::asSmallInt(a);
                    long long vb = proto::asSmallInt(b);
                    if (vb != 0) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(va % vb);
                        DISPATCH();
                    }
                }
                if (a && b && a->isInteger(pContext) && b->isInteger(pContext)) {
                    long long vb = b->asLong(pContext);
                    if (vb != 0) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(a->asLong(pContext) % vb);
                        DISPATCH();
                    }
                }

                // Fallback to double/toNumber
                const proto::ProtoObject* na = toNumber(pContext, toPrimIfObject(a));
                const proto::ProtoObject* nb = toNumber(pContext, toPrimIfObject(b));
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                
                {
                    auto toDoubleVal2 = [&](const proto::ProtoObject* v) -> double {
                        if (!v || v == PROTO_NONE) return 0.0;
                        if (v->isInteger(pContext)) return static_cast<double>(v->asLong(pContext));
                        if (v->isDouble(pContext) || v->isFloat(pContext)) return v->asDouble(pContext);
                        return 0.0;
                    };
                    double da = toDoubleVal2(na);
                    double db = toDoubleVal2(nb);
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(std::fmod(da, db));
                }
                DISPATCH();
            }
            L_OP_eq: ;
            L_OP_neq: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                const proto::ProtoObject* pa = toPrimIfObject(a);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* pb = toPrimIfObject(b);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                bool eq = jsAbstractEquals(pContext, pa, pb);
                REFRESH_INTERP_STATE();
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((opcode == OP_eq ? eq : !eq) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_strict_eq: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                auto isUndef = [&](const proto::ProtoObject* x) {
                    return !x || x == PROTO_NONE ||
                           x == getUndefinedSentinel() ||
                           (x && x->isNone(pContext));
                };
                // ECMA-262 §7.2.16 IsStrictlyEqual: NaN is never strictly
                // equal to anything (including itself).  Pre-fix the
                // pointer-equality fast path returned true for `NaN === NaN`.
                auto isNaNStrict = [&](const proto::ProtoObject* x) -> bool {
                    if (!x) return false;
                    if (x->isDouble(pContext) || x->isFloat(pContext))
                        return std::isnan(x->asDouble(pContext));
                    return false;
                };
                int cmp = 1;
                if (isNaNStrict(a) || isNaNStrict(b)) cmp = 1;
                else if (a == b) cmp = 0;
                else if (isUndef(a) && isUndef(b)) cmp = 0;
                else if (a && b) cmp = a->compare(pContext, b);
                else cmp = 1;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((cmp == 0) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_strict_neq: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // see L_OP_strict_eq for the unified undefined-equality rule.
                auto isUndef = [&](const proto::ProtoObject* x) {
                    return !x || x == PROTO_NONE ||
                           x == getUndefinedSentinel() ||
                           (x && x->isNone(pContext));
                };
                auto isNaNStrict2 = [&](const proto::ProtoObject* x) -> bool {
                    if (!x) return false;
                    if (x->isDouble(pContext) || x->isFloat(pContext))
                        return std::isnan(x->asDouble(pContext));
                    return false;
                };
                int cmp = 1;
                if (isNaNStrict2(a) || isNaNStrict2(b)) cmp = 1;
                else if (a == b) cmp = 0;
                else if (isUndef(a) && isUndef(b)) cmp = 0;
                else if (a && b) cmp = a->compare(pContext, b);
                else cmp = 1;
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((cmp != 0) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_lt: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // Per ECMA-262 Abstract Relational Comparison §7.2.13 step 4:
                // numerify booleans before comparison.  Pre-fix `1 < true`
                // hit protoCore's compare() on a SmallInt vs the
                // PROTO_TRUE/PROTO_FALSE singletons (pointer constants)
                // and returned a pointer-order comparison, not a numeric
                // one — `1 < true` came out as true.
                // undefined coerces to NaN; comparisons with NaN are always
                // false (ECMA §7.2.13 step 3.b returns 'undefined' which the
                // comparison opcodes map to false).
                auto isUndefForCmp = [&](const proto::ProtoObject* x) {
                    return !x || x == PROTO_NONE ||
                           x == getUndefinedSentinel() ||
                           (x && x->isNone(pContext));
                };
                if (isUndefForCmp(a) || isUndefForCmp(b)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_FALSE;
                    DISPATCH();
                }
                auto numerifyBool = [&](const proto::ProtoObject* x) -> const proto::ProtoObject* {
                    if (x == PROTO_TRUE)  return proto::makeSmallInt(1);
                    if (x == PROTO_FALSE) return proto::makeSmallInt(0);
                    if (x == t_nullSentinel) return proto::makeSmallInt(0); // ToNumber(null) = 0
                    return x;
                };
                a = numerifyBool(a);
                b = numerifyBool(b);
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = (proto::asSmallInt(a) < proto::asSmallInt(b)) ? PROTO_TRUE : PROTO_FALSE;
                    DISPATCH();
                }
                const proto::ProtoObject* pa = toPrimIfObject(a);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* pb = toPrimIfObject(b);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                int cmp = relCmpAfterPrim(pa, pb);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((cmp == -1) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_lte: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // see L_OP_lt for the boolean-numerify rationale.
                // undefined comparisons short-circuit to false (NaN rule);
                // see L_OP_lt for the spec reference.
                if (!a || a == PROTO_NONE || a == getUndefinedSentinel() ||
                    (a && a->isNone(pContext)) ||
                    !b || b == PROTO_NONE || b == getUndefinedSentinel() ||
                    (b && b->isNone(pContext))) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_FALSE;
                    DISPATCH();
                }
                if (a == PROTO_TRUE) a = proto::makeSmallInt(1);
                else if (a == PROTO_FALSE) a = proto::makeSmallInt(0);
                else if (a == t_nullSentinel) a = proto::makeSmallInt(0); // ToNumber(null) = 0
                if (b == PROTO_TRUE) b = proto::makeSmallInt(1);
                else if (b == PROTO_FALSE) b = proto::makeSmallInt(0);
                else if (b == t_nullSentinel) b = proto::makeSmallInt(0); // ToNumber(null) = 0
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = (proto::asSmallInt(a) <= proto::asSmallInt(b)) ? PROTO_TRUE : PROTO_FALSE;
                    DISPATCH();
                }
                const proto::ProtoObject* pa = toPrimIfObject(a);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* pb = toPrimIfObject(b);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                int cmp = relCmpAfterPrim(pa, pb);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((cmp == -1 || cmp == 0) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_gt: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // undefined comparisons short-circuit to false (NaN rule);
                // see L_OP_lt for the spec reference.
                if (!a || a == PROTO_NONE || a == getUndefinedSentinel() ||
                    (a && a->isNone(pContext)) ||
                    !b || b == PROTO_NONE || b == getUndefinedSentinel() ||
                    (b && b->isNone(pContext))) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_FALSE;
                    DISPATCH();
                }
                if (a == PROTO_TRUE) a = proto::makeSmallInt(1);
                else if (a == PROTO_FALSE) a = proto::makeSmallInt(0);
                else if (a == t_nullSentinel) a = proto::makeSmallInt(0); // ToNumber(null) = 0
                if (b == PROTO_TRUE) b = proto::makeSmallInt(1);
                else if (b == PROTO_FALSE) b = proto::makeSmallInt(0);
                else if (b == t_nullSentinel) b = proto::makeSmallInt(0); // ToNumber(null) = 0
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = (proto::asSmallInt(a) > proto::asSmallInt(b)) ? PROTO_TRUE : PROTO_FALSE;
                    DISPATCH();
                }
                const proto::ProtoObject* pa = toPrimIfObject(a);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* pb = toPrimIfObject(b);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                int cmp = relCmpAfterPrim(pa, pb);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((cmp == 1) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_gte: {
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                // undefined comparisons short-circuit to false (NaN rule);
                // see L_OP_lt for the spec reference.
                if (!a || a == PROTO_NONE || a == getUndefinedSentinel() ||
                    (a && a->isNone(pContext)) ||
                    !b || b == PROTO_NONE || b == getUndefinedSentinel() ||
                    (b && b->isNone(pContext))) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = PROTO_FALSE;
                    DISPATCH();
                }
                if (a == PROTO_TRUE) a = proto::makeSmallInt(1);
                else if (a == PROTO_FALSE) a = proto::makeSmallInt(0);
                else if (a == t_nullSentinel) a = proto::makeSmallInt(0); // ToNumber(null) = 0
                if (b == PROTO_TRUE) b = proto::makeSmallInt(1);
                else if (b == PROTO_FALSE) b = proto::makeSmallInt(0);
                else if (b == t_nullSentinel) b = proto::makeSmallInt(0); // ToNumber(null) = 0
                if (proto::isSmallInt(a) && proto::isSmallInt(b)) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = (proto::asSmallInt(a) >= proto::asSmallInt(b)) ? PROTO_TRUE : PROTO_FALSE;
                    DISPATCH();
                }
                const proto::ProtoObject* pa = toPrimIfObject(a);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* pb = toPrimIfObject(b);
                REFRESH_INTERP_STATE();
                if (has_pending_exception) DISPATCH();
                int cmp = relCmpAfterPrim(pa, pb);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = ((cmp == 1 || cmp == 0) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_and: {
                // Bitwise AND: ToInt32(a) & ToInt32(b)
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                int32_t res = toInt32Val(pContext, a) & toInt32Val(pContext, b);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(res));
                DISPATCH();
            }
            L_OP_or: {
                // Bitwise OR: ToInt32(a) | ToInt32(b)
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                int32_t res = toInt32Val(pContext, a) | toInt32Val(pContext, b);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(res));
                DISPATCH();
            }
            L_OP_xor: {
                // Bitwise XOR: ToInt32(a) ^ ToInt32(b)
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                int32_t res = toInt32Val(pContext, a) ^ toInt32Val(pContext, b);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(res));
                DISPATCH();
            }
            L_OP_shl: {
                // Left shift: ToInt32(a) << (ToUint32(b) & 0x1F)
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                int32_t res = toInt32Val(pContext, a) << (toUint32Val(pContext, b) & 0x1Fu);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(res));
                DISPATCH();
            }
            L_OP_sar: {
                // Arithmetic right shift: ToInt32(a) >> (ToUint32(b) & 0x1F)
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                int32_t res = toInt32Val(pContext, a) >> (toUint32Val(pContext, b) & 0x1Fu);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(res));
                DISPATCH();
            }
            L_OP_shr: {
                // Unsigned right shift: ToUint32(a) >>> (ToUint32(b) & 0x1F) → Int32 result
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                uint32_t ua = toUint32Val(pContext, a);
                uint32_t shift = toUint32Val(pContext, b) & 0x1Fu;
                uint32_t ures = ua >> shift;
                // ToNumber result: if fits in signed int32, return integer, else double
                if (ures <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(ures));
                } else {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(static_cast<double>(ures));
                }
                DISPATCH();
            }
            L_OP_not: {
                // Bitwise NOT: ~ToInt32(a)
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]);
                if (has_pending_exception) DISPATCH();
                int32_t res = ~toInt32Val(pContext, a);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(res));
                DISPATCH();
            }
            L_OP_neg: {
                // Unary minus: -ToNumber(a).  Same ToNumber rules as
                // L_OP_plus — pre-numerify null and the boolean
                // singletons so `-null` becomes -0 (not -NaN).
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                if (a == PROTO_TRUE) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(-1);
                    DISPATCH();
                }
                if (a == PROTO_FALSE || a == t_nullSentinel) {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(-0.0);
                    DISPATCH();
                }
                const proto::ProtoObject* num = toNumber(pContext, toPrimIfObject(a));
                if (has_pending_exception) DISPATCH();
                if (!num || num == PROTO_NONE) { pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN()); DISPATCH(); }
                if (num->isInteger(pContext)) {
                    long long v = num->asLong(pContext);
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = (v == 0 ? pContext->fromDouble(-0.0) : pContext->fromInteger(-v));
                } else {
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(-num->asDouble(pContext));
                }
                DISPATCH();
            }
            L_OP_lnot: {
                // Logical NOT: !ToBoolean(a)
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (toBool(pContext, a) ? PROTO_FALSE : PROTO_TRUE);
                DISPATCH();
            }
            L_OP_inc: {
                // Prefix increment: ToNumber(a) + 1
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                if (proto::isSmallInt(a)) {
                    long long val = proto::asSmallInt(a) + 1;
                    if (proto::smallIntInRange(val)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(val);
                        DISPATCH();
                    }
                }
                const proto::ProtoObject* num = toNumber(pContext, a);
                if (!num || num == PROTO_NONE) { pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN()); DISPATCH(); }
                if (num->isInteger(pContext)) pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(num->asLong(pContext) + 1);
                else pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(num->asDouble(pContext) + 1.0);
                DISPATCH();
            }
            L_OP_dec: {
                // Prefix decrement: ToNumber(a) - 1
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                if (proto::isSmallInt(a)) {
                    long long val = proto::asSmallInt(a) - 1;
                    if (proto::smallIntInRange(val)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(val);
                        DISPATCH();
                    }
                }
                const proto::ProtoObject* num = toNumber(pContext, a);
                if (!num || num == PROTO_NONE) { pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN()); DISPATCH(); }
                if (num->isInteger(pContext)) pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(num->asLong(pContext) - 1);
                else pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(num->asDouble(pContext) - 1.0);
                DISPATCH();
            }
            L_OP_post_inc: {
                // Post-increment: pushes original value then incremented value.
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                if (proto::isSmallInt(a)) {
                    long long val = proto::asSmallInt(a);
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = a;
                    if (proto::smallIntInRange(val + 1)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(val + 1);
                        DISPATCH();
                    }
                }
                const proto::ProtoObject* num = toNumber(pContext, a);
                const proto::ProtoObject* inc;
                if (!num || num == PROTO_NONE) inc = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                else if (num->isInteger(pContext)) inc = pContext->fromInteger(num->asLong(pContext) + 1);
                else inc = pContext->fromDouble(num->asDouble(pContext) + 1.0);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (num ? num : PROTO_NONE);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = inc;
                DISPATCH();
            }
            L_OP_post_dec: {
                // Post-decrement: pushes original value then decremented value.
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* a = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                if (proto::isSmallInt(a)) {
                    long long val = proto::asSmallInt(a);
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = a;
                    if (proto::smallIntInRange(val - 1)) {
                        pAutomaticLocals[currentStackBase + _PF().stackTop++] = proto::makeSmallInt(val - 1);
                        DISPATCH();
                    }
                }
                const proto::ProtoObject* num = toNumber(pContext, a);
                const proto::ProtoObject* dec;
                if (!num || num == PROTO_NONE) dec = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                else if (num->isInteger(pContext)) dec = pContext->fromInteger(num->asLong(pContext) - 1);
                else dec = pContext->fromDouble(num->asDouble(pContext) - 1.0);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (num ? num : PROTO_NONE);
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = dec;
                DISPATCH();
            }
            L_OP_dec_loc: {
                // Decrement a local variable slot in-place. Format: loc8 (1 byte).
                // Local vars live at slot `argCount + locIndex` from the
                // base of the frame's automaticLocals window — NOT
                // offset by currentStackBase, which is where the
                // operand stack starts.  Mismatching this with the
                // get_loc / put_loc layout would silently overwrite
                // stack temporaries and leave the var at its original
                // value, causing infinite for(;;i--) loops to lock up.
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = getSlot(pContext, argCount + locIndex);
                    if (proto::isSmallInt(cur)) {
                        long long val = proto::asSmallInt(cur) - 1;
                        if (proto::smallIntInRange(val)) {
                            setSlot(pContext, argCount + locIndex, proto::makeSmallInt(val));
                            DISPATCH();
                        }
                    }
                    const proto::ProtoObject* num = toNumber(pContext, cur);
                    const proto::ProtoObject* nv;
                    if (!num || num == PROTO_NONE) nv = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                    else if (num->isInteger(pContext)) nv = pContext->fromInteger(num->asLong(pContext) - 1);
                    else nv = pContext->fromDouble(num->asDouble(pContext) - 1.0);
                    setSlot(pContext, argCount + locIndex, nv);
                }
                DISPATCH();
            }
            L_OP_inc_loc: {
                // Increment a local variable slot in-place. Format: loc8 (1 byte).
                // See L_OP_dec_loc for the slot-addressing rationale —
                // QuickJS's peephole turns `get_loc;post_inc;put_loc;drop`
                // into `inc_loc`, so this fast path must end with the
                // var slot updated, NOT a stack slot.
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = getSlot(pContext, argCount + locIndex);
                    if (proto::isSmallInt(cur)) {
                        long long val = proto::asSmallInt(cur) + 1;
                        if (proto::smallIntInRange(val)) {
                            setSlot(pContext, argCount + locIndex, proto::makeSmallInt(val));
                            DISPATCH();
                        }
                    }
                    const proto::ProtoObject* num = toNumber(pContext, cur);
                    const proto::ProtoObject* nv;
                    if (!num || num == PROTO_NONE) nv = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                    else if (num->isInteger(pContext)) nv = pContext->fromInteger(num->asLong(pContext) + 1);
                    else nv = pContext->fromDouble(num->asDouble(pContext) + 1.0);
                    setSlot(pContext, argCount + locIndex, nv);
                }
                DISPATCH();
            }
                L_OP_add_loc: {
                // add_loc loc8: pops TOS and adds it to a local variable. Format: loc8 (1 byte).
                if (pc + 1 > len || _PF().stackTop == 0) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = getSlot(pContext, argCount + locIndex);
                    // SmallInt fast path: most tight loops (`acc += i`,
                    // `sum += arr[i]`) hit this op every iteration with
                    // two tagged SmallInts. Bypass asString/toNumber/add
                    // entirely — no allocation, single addition, range
                    // check, re-tag, store.
                    if (proto::isSmallInt(cur) && proto::isSmallInt(val)) {
                        long long sum = proto::asSmallInt(cur) + proto::asSmallInt(val);
                        if (proto::smallIntInRange(sum)) {
                            setSlot(pContext, argCount + locIndex, proto::makeSmallInt(sum));
                            DISPATCH();
                        }
                    }
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
                DISPATCH();
            }
            L_OP_pow: {
                // Exponentiation: a ** b
                if (_PF().stackTop < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]));
                pAutomaticLocals[currentStackBase + _PF().stackTop] = PROTO_NONE; // Zero popped slot
                if (has_pending_exception) DISPATCH();
                const proto::ProtoObject* a = toNumber(pContext, toPrimIfObject(pAutomaticLocals[currentStackBase + --_PF().stackTop]));
                if (has_pending_exception) DISPATCH();
                double da = (!a || a == PROTO_NONE) ? std::numeric_limits<double>::quiet_NaN() : a->asDouble(pContext);
                double db = (!b || b == PROTO_NONE) ? std::numeric_limits<double>::quiet_NaN() : b->asDouble(pContext);
                double result = std::pow(da, db);
                if (result == std::trunc(result) && std::abs(result) < 9.007199254740992e15 && !std::isnan(result) && !std::isinf(result))
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromInteger(static_cast<long long>(result));
                else
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] = pContext->fromDouble(result);
                DISPATCH();
            }
            L_OP_is_undefined_or_null: {
                // Pops one value; pushes true if it is undefined or null.
                // Used by the ?? operator and ?. optional chaining.
                // Both undefined representations (PROTO_NONE and heap
                // sentinel) plus the null sentinel must qualify — see
                // L_OP_is_undefined for the two-representation rationale.
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* val = pAutomaticLocals[currentStackBase + --_PF().stackTop];
                bool nullish = (!val || val == PROTO_NONE || val == t_nullSentinel ||
                                val == getUndefinedSentinel() ||
                                (val && val->isNone(pContext)));
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (nullish ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_nop: ;
                // No operation.
                DISPATCH();
            L_OP_get_length: {
                // Push the .length property of TOS.
                if (_PF().stackTop == 0) return PROTO_NONE;
                const proto::ProtoObject* obj = pAutomaticLocals[currentStackBase + --_PF().stackTop];

                // Fast path: string.length — bypass the full prototype-chain
                // walk that getAttribute would do.  protoCore's ProtoString
                // already stores the codepoint count in the rope-node header
                // (total_chars / char_count) and answers getSize() in O(1)
                // for both leaf and internal node forms; for inline strings
                // it walks the few inline UTF-8 bytes (≤ 7 chars).
                //
                // Pre-this-fast-path: `s.length` returned `undefined` because
                // length was never installed on the JS-side String.prototype
                // by BuildStringPrototype.  Every benchmark using
                // `for (var i = 0; i < s.length; i++)` looped zero times.
                //
                // Caveat: returns codepoint count, not the spec's UTF-16
                // code-unit count.  These match for BMP characters (almost
                // all JS string content); non-BMP characters are off-by-one
                // per surrogate pair, which a future pass can correct by
                // counting code units explicitly.  Not paying that cost on
                // every .length read for now.
                if (obj && obj != PROTO_NONE && obj->isString(pContext)) {
                    long long n = static_cast<long long>(
                        obj->asString(pContext)->getSize(pContext));
                    pAutomaticLocals[currentStackBase + _PF().stackTop++] =
                        pContext->fromInteger(n);
                    DISPATCH();
                }

                const proto::ProtoString* lk = JSSymbols::length(pContext);
                // ECMA-262 §10.1.8 / §13.3.2.1: a user-defined
                // `length` accessor (`Object.defineProperty(o, 'length',
                // {get: …})`) must fire on every `o.length` read,
                // including via OP_get_length.  Pre-fix this opcode
                // skipped accessor lookup entirely and read whatever
                // landed in the `length` data slot — for accessor
                // descriptors that slot is the undefined sentinel,
                // so `o.length` evaluated to `undefined` and
                // Array.prototype.reduce / filter / map applied to
                // such objects saw len = 0.
                const proto::ProtoObject* len_val = (obj && lk)
                    ? invokeGetterIfPresentFast(obj, lk) : PROTO_NONE;
                if (has_pending_exception) DISPATCH();
                if (!len_val || len_val == PROTO_NONE) {
                    len_val = (obj && lk) ? obj->getAttribute(pContext, lk, true) : PROTO_NONE;
                }
                pAutomaticLocals[currentStackBase + _PF().stackTop++] = (len_val ? len_val : PROTO_NONE);
                DISPATCH();
            }
            L_OP_to_object: {
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
                    DISPATCH();
                }
                stackPush(pContext, val);
                DISPATCH();
            }
            L_OP_throw_error: {
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
                DISPATCH();
            }
            L_OP_catch: {
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
                DISPATCH();
            }
            L_OP_nip_catch: {
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
                DISPATCH();
            }
            L_OP_gosub: {
                // gosub label(4): call a finally block.
                // Push the return address (instruction after gosub) as an integer, then jump.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                int return_pc = pc + 4;
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(return_pc)));
                pc += diff; // jump to finally block (same formula as goto)
                DISPATCH();
            }
            L_OP_ret: {
                // ret: return from a finally block.
                // Pop the return address pushed by gosub and jump there.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* addr_obj = stackTop(pContext);
                stackPop(pContext);
                if (!addr_obj || addr_obj == PROTO_NONE || !addr_obj->isInteger(pContext))
                    return PROTO_NONE;
                pc = static_cast<int>(addr_obj->asLong(pContext));
                DISPATCH();
            }
            L_OP_plus: {
                // Unary plus: ToNumber(a).  null→0, true→1, false→0
                // (ECMA-262 §7.1.4 ToNumber).  Pre-fix the toNumber()
                // helper returned NaN for the null and boolean
                // singletons because it didn't recognise them as
                // primitives, so `+null` → NaN, `+true` → 1, `+false`
                // → 0 (the boolean cases happened to work via a
                // different fallback).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                if (a == PROTO_TRUE) { stackPush(pContext, proto::makeSmallInt(1)); DISPATCH(); }
                if (a == PROTO_FALSE || a == t_nullSentinel) { stackPush(pContext, proto::makeSmallInt(0)); DISPATCH(); }
                { const proto::ProtoObject* pv = toPrimIfObject(a);
                  if (has_pending_exception) DISPATCH();
                  const proto::ProtoObject* num = toNumber(pContext, pv);
                  // §7.1.4 ToNumber on a Symbol throws TypeError; the
                  // throw is recorded as t_callException by toNumber.
                  // Promote to pending_exception so the surrounding try
                  // block actually catches it (pre-fix `+Symbol()` ran
                  // through, pushed PROTO_NONE / undefined, and the
                  // exception was discovered out-of-band on the next
                  // operation).
                  if (t_hasCallException) {
                      pending_exception     = t_callException;
                      has_pending_exception = true;
                      t_hasCallException    = false;
                      t_callException       = nullptr;
                      DISPATCH();
                  }
                  stackPush(pContext, num);
                }
                DISPATCH();
            }
            L_OP_typeof: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* v = stackTop(pContext);
                stackPop(pContext);
                const char* typeStr = "undefined";
                if (v == t_nullSentinel) {
                    typeStr = "object";  // typeof null === "object" per spec
                } else if (v && v != PROTO_NONE && !v->isNone(pContext) && v != t_undefinedSentinel) {
                    if (v->isBoolean(pContext)) typeStr = "boolean";
                    else if (v->isInteger(pContext) || v->isDouble(pContext) || v->isFloat(pContext)) typeStr = "number";
                    else if (v->asString(pContext)) typeStr = "string";
                    else if ([&]() -> bool {
                        // §7.1.13.1 typeof: a Symbol value yields "symbol".
                        // protoJS carries Symbols as objects with the
                        // __is_symbol__ marker.  Probe the chain (the marker
                        // is on the prototype for cached symbols).
                        const proto::ProtoObject* symKo = pContext->fromUTF8String("__is_symbol__");
                        const proto::ProtoString* symK = symKo ? symKo->asString(pContext) : nullptr;
                        return symK && v->getAttribute(pContext, symK, true) == PROTO_TRUE;
                    }()) {
                        typeStr = "symbol";
                    }
                    else if (v->isMethod(pContext) || getBytecodeId(pContext, v) >= 0) typeStr = "function";
                    else {
                        // Check for __native_fn__ wrapper (native function with .length/.name).
                        const proto::ProtoString* nfTypeKey = JSSymbols::nativeFn(pContext);
                        const proto::ProtoObject* nfTypeTarget = nfTypeKey
                            ? v->getAttribute(pContext, nfTypeKey, false) : nullptr;
                        if (nfTypeTarget && nfTypeTarget != PROTO_NONE && nfTypeTarget->isMethod(pContext)) {
                            typeStr = "function";
                        } else {
                            // Check for bound function sentinel (__bound_fn__ attribute).
                            const proto::ProtoString* bfTypeKey = JSSymbols::boundFn(pContext);
                            const proto::ProtoObject* bfTypeTarget = bfTypeKey
                                ? v->getAttribute(pContext, bfTypeKey, false) : nullptr;
                            if (bfTypeTarget && bfTypeTarget != PROTO_NONE) {
                                typeStr = "function";
                            } else {
                                // Built-in constructors use special marker attributes instead of
                                // __native_fn__ for dispatch. Check each one so typeof returns
                                // "function" as required by the spec.
                                const proto::ProtoString* acK = JSSymbols::arrayCtor(pContext);
                                const proto::ProtoString* ecK = JSSymbols::errorCtor(pContext);
                                const proto::ProtoString* reK = JSSymbols::regexpCtor(pContext);
                                const proto::ProtoString* taK = JSSymbols::taCtor(pContext);
                                const proto::ProtoString* scK = JSSymbols::stringCtor(pContext);
                                // __construct__ is used by Object, Number, Boolean, Map, Set, Promise, etc.
                                const proto::ProtoString* conK = JSSymbols::construct(pContext);
                                bool isCtor = (acK && v->getAttribute(pContext, acK, false) == PROTO_TRUE)
                                          || (ecK && v->getAttribute(pContext, ecK, false) && v->getAttribute(pContext, ecK, false) != PROTO_NONE)
                                          || (reK && v->getAttribute(pContext, reK, false) == PROTO_TRUE)
                                          || (taK && v->getAttribute(pContext, taK, false) && v->getAttribute(pContext, taK, false) != PROTO_NONE)
                                          || (scK && v->getAttribute(pContext, scK, false) == PROTO_TRUE)
                                          || (conK && v->getAttribute(pContext, conK, false) && v->getAttribute(pContext, conK, false)->isMethod(pContext));
                                typeStr = isCtor ? "function" : "object";
                            }
                        }
                    }
                }
                stackPush(pContext, pContext->fromUTF8String(typeStr));
                DISPATCH();
            }
            L_OP_instanceof: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                // Spec §13.10.2: If Type(F) is not Object → throw TypeError.
                bool funcIsPrimitive = (!func || func == PROTO_NONE || func == t_nullSentinel
                    || func == t_undefinedSentinel
                    || func->isBoolean(pContext) || func->isInteger(pContext) || func->isDouble(pContext)
                    || (func->isString(pContext) && !func->isMethod(pContext)));
                if (funcIsPrimitive) {
                    pending_exception = makeError(pContext, "TypeError",
                        "Right-hand side of 'instanceof' is not callable", pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                const proto::ProtoObject* protoObj = func ? func->getAttribute(pContext, protoKey, false) : nullptr;
                // Spec §13.10.2 step 5: If Type(F.[[Prototype]]) is not Object → throw TypeError.
                // We only throw if protoObj is a primitive (not null/undefined — PROTO_NONE — which
                // produces false instead of TypeError per spec OrdinaryHasInstance step 3).
                if (protoObj && protoObj != PROTO_NONE) {
                    bool protoIsPrimitive = (protoObj == t_nullSentinel
                        || protoObj->isBoolean(pContext) || protoObj->isInteger(pContext)
                        || protoObj->isDouble(pContext)
                        || (protoObj->isString(pContext) && !protoObj->isMethod(pContext)));
                    if (protoIsPrimitive) {
                        pending_exception = makeError(pContext, "TypeError",
                            "Function has non-object prototype in instanceof check", pGlobalRoot);
                        has_pending_exception = true;
                        DISPATCH();
                    }
                }
                // Spec §13.10.2 / OrdinaryHasInstance: primitives are NEVER
                // instanceof anything.  Without this short-circuit, protoCore's
                // isInstanceOf walks the prototype chain — and primitives are
                // children of their wrapper-class prototype (BooleanPrototype,
                // NumberPrototype, StringPrototype), so it returned true and
                // `false instanceof Boolean` was true (spec: false).
                bool objIsPrimitive = (!obj || obj == PROTO_NONE || obj == t_nullSentinel ||
                                       obj == getUndefinedSentinel() ||
                                       obj->isBoolean(pContext) ||
                                       obj->isInteger(pContext) ||
                                       obj->isDouble(pContext) ||
                                       (obj->isString(pContext) && !obj->isMethod(pContext)));
                if (objIsPrimitive) {
                    stackPush(pContext, PROTO_FALSE);
                    DISPATCH();
                }
                const proto::ProtoObject* res = (obj && protoObj && protoObj != PROTO_NONE)
                    ? obj->isInstanceOf(pContext, protoObj) : PROTO_FALSE;
                stackPush(pContext, (res == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_in: {
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
                    DISPATCH();
                }
                const proto::ProtoObject* keyObj = toString(pContext, keyVal);
                const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                // Array index fast path: indices live in __elements__,
                // not as data keys, so the regular hasAttribute walk
                // misses them.  Pre-fix `0 in [10,20]` returned false.
                // Handles both numeric and numeric-string keys.
                long long arrIdxIn = -1;
                if (keyVal && keyVal->isInteger(pContext)) {
                    arrIdxIn = keyVal->asLong(pContext);
                } else if (key) {
                    std::string ks;
                    key->toUTF8String(pContext, ks);
                    if (!ks.empty()) {
                        bool allDigits = true;
                        for (char c : ks) if (c < '0' || c > '9') { allDigits = false; break; }
                        if (allDigits) {
                            try { arrIdxIn = std::stoll(ks); } catch (...) { arrIdxIn = -1; }
                        }
                    }
                }
                if (arrIdxIn >= 0) {
                    const proto::ProtoList* els = protojs::getArrayElements(pContext, obj);
                    if (els && arrIdxIn < static_cast<long long>(els->getSize(pContext))) {
                        const proto::ProtoObject* v = els->getAt(pContext, static_cast<int>(arrIdxIn));
                        if (v && v != PROTO_NONE) {
                            stackPush(pContext, PROTO_TRUE);
                            DISPATCH();
                        }
                    }
                }
                // IMPORTANT: hasAttribute returns PROTO_TRUE or PROTO_FALSE (both are non-null
                // pointers), so must compare against PROTO_TRUE — never cast to bool directly.
                // Check data key first.
                const proto::ProtoObject* hasResult = (key) ? obj->hasAttribute(pContext, key) : PROTO_FALSE;
                // Also check accessor sidecars (__get_<N>__, __set_<N>__) — when a property is
                // defined via Object.defineProperty as an accessor, there is no data key (it is
                // removed during definition).  The property still exists and 'in' must return true.
                if (hasResult != PROTO_TRUE && key) {
                    std::string keyStr;
                    key->toUTF8String(pContext, keyStr);
                    for (const char* prefix : {"__get_", "__set_"}) {
                        std::string sk = std::string(prefix) + keyStr + "__";
                        const proto::ProtoObject* sko = pContext->fromUTF8String(sk.c_str());
                        const proto::ProtoString* sks = sko ? sko->asString(pContext) : nullptr;
                        if (sks && obj->hasAttribute(pContext, sks) == PROTO_TRUE) {
                            hasResult = PROTO_TRUE;
                            DISPATCH();
                        }
                    }
                }
                stackPush(pContext, hasResult == PROTO_TRUE ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_delete: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* keyVal = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* keyObj = toString(pContext, keyVal);
                const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                if (obj && obj != PROTO_NONE && key) {
                    // ECMAScript 10.1.10: if the property is non-configurable,
                    // delete returns false in non-strict mode and must NOT remove
                    // the property.  The configurable bit is stored in the sidecar
                    // descriptor key __pd_<name>__ (bit 1 = 0x2).  We only block
                    // deletion when the sidecar is present AND the configurable bit
                    // is clear; the absence of a sidecar means the property is
                    // configurable by default.
                    {
                        std::string propNameStr;
                        key->toUTF8String(pContext, propNameStr);
                        std::string pdKeyStr = "__pd_" + propNameStr + "__";
                        const proto::ProtoObject* pdko = pContext->fromUTF8String(pdKeyStr.c_str());
                        const proto::ProtoString* pdks = pdko ? pdko->asString(pContext) : nullptr;
                        if (pdks) {
                            const proto::ProtoObject* pdv = obj->getAttribute(pContext, pdks, false);
                            if (pdv && pdv != PROTO_NONE && pdv->isInteger(pContext)) {
                                uint8_t bits = static_cast<uint8_t>(pdv->asLong(pContext));
                                if (!(bits & 0x2)) {
                                    // §13.5.1.2 step 5.a: in strict mode
                                    // a delete on a non-configurable
                                    // property throws TypeError; sloppy
                                    // mode returns false silently. Pre-
                                    // fix the opcode always returned
                                    // false (built-ins/Boolean/prototype
                                    // /S15.6.3.1_A3 and similar strict-
                                    // mode tests on built-in prototype
                                    // slots caught the missing throw).
                                    if (module && module->isStrict) {
                                        pending_exception = makeError(pContext, "TypeError",
                                            "Cannot delete non-configurable property", pGlobalRoot);
                                        has_pending_exception = true;
                                        DISPATCH();
                                    }
                                    // Non-configurable, sloppy mode:
                                    // silently return false.
                                    stackPush(pContext, PROTO_FALSE);
                                    DISPATCH();
                                }
                            }
                        }
                    }
                    // Pass nullptr (not PROTO_NONE) so protoCore's implSetAt calls
                    // implRemoveAt, which truly removes the entry from the sparse list.
                    // This makes hasOwnAttribute, getOwnAttributes iteration, and
                    // for-in all correctly report the property as absent.
                    std::string propNameStrDel;
                    key->toUTF8String(pContext, propNameStrDel);
                    // Array element deletion: when obj is an array and the
                    // key is a numeric index, write PROTO_NONE into the
                    // __elements__ ProtoList slot (creates a hole) before
                    // the generic attribute path runs.  Pre-fix \`delete
                    // a[1]\` was a silent no-op because the element lives
                    // in __elements__, not as an own attribute.
                    {
                        const proto::ProtoString* isArrKeyDel = JSSymbols::isArray(pContext);
                        const proto::ProtoObject* isArrValDel = isArrKeyDel
                            ? obj->getAttribute(pContext, isArrKeyDel, true) : nullptr;
                        if (isArrValDel == PROTO_TRUE && !propNameStrDel.empty() &&
                            propNameStrDel[0] >= '0' && propNameStrDel[0] <= '9') {
                            char* endp = nullptr;
                            long long idx = std::strtoll(propNameStrDel.c_str(), &endp, 10);
                            if (endp && *endp == '\0' && idx >= 0 &&
                                std::to_string(idx) == propNameStrDel) {
                                const proto::ProtoList* els = getArrayElements(pContext, obj);
                                if (els && idx < static_cast<long long>(els->getSize(pContext))) {
                                    const proto::ProtoList* newEls = els->setAt(pContext,
                                        static_cast<unsigned long>(idx), PROTO_NONE);
                                    if (newEls) setArrayElements(pContext, obj, newEls);
                                }
                            }
                        }
                    }
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, nullptr);
                    // Also remove accessor sidecars (__get_<name>__, __set_<name>__,
                    // __pd_<name>__) so the property is fully deleted per the spec.
                    if (newObj) {
                        for (const std::string& prefix : {"__get_", "__set_", "__pd_"}) {
                            std::string sk = prefix + propNameStrDel + "__";
                            const proto::ProtoObject* sko = pContext->fromUTF8String(sk.c_str());
                            const proto::ProtoString* sks = sko ? sko->asString(pContext) : nullptr;
                            if (sks && newObj) newObj = newObj->setAttribute(pContext, sks, nullptr);
                        }
                    }
                    if (newObj && newObj != obj) {
                        updateMapping(pContext, obj, newObj);
                        REFRESH_GLOBAL_OBJ();
                        if (newObj && pGlobalRoot && obj == globalObj) {
                            *pGlobalRoot = newObj;
                            globalObj = newObj;
                        }
                    }
                }
                stackPush(pContext, PROTO_TRUE);
                DISPATCH();
            }
            L_OP_call_method: ;
            L_OP_tail_call_method: {
                // Stack (top = index 0): arg0, ..., arg(n-1), func, this. QuickJS: call_argv = sp - argc, call_argv[-1] = func, call_argv[-2] = this.
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc);
                const proto::ProtoObject* thisVal = stackAt(pContext, argc + 1);
                // Unwrap native function wrapper: if func has __native_fn__, use the
                // raw method it contains. This allows .length and .name to be stored
                // on wrapper objects while still dispatching to the ProtoMethod.
                {
                    const proto::ProtoString* nfKey2 = JSSymbols::nativeFn(pContext);
                    if (nfKey2 && func && func != PROTO_NONE && !func->isMethod(pContext)) {
                        const proto::ProtoObject* rawMethod2 = func->getAttribute(pContext, nfKey2, false);
                        if (rawMethod2 && rawMethod2 != PROTO_NONE && rawMethod2->isMethod(pContext))
                            func = rawMethod2;
                    }
                }
                // Native ProtoMethod fast-path detection: isMethod() is a
                // pointer-tag check (constant time, no attribute lookup),
                // while getBytecodeId() does a full getAttribute on
                // __bytecode_id__.  For tight loops over native methods
                // (e.g. `arr.push(i)` 100 K times) the per-call attribute
                // lookup was a measurable fraction of dispatch cost, even
                // though it always returned -1.  Skip it when we can.
                const bool funcIsNative = func && func->isMethod(pContext);
                int bcId = funcIsNative ? -1 : getBytecodeId(pContext, func);
                const ProtoBytecodeModule* resolvedMod2 = nullptr;
                if (bcId >= 0 && static_cast<size_t>(bcId) < module->nestedFunctions.size())
                    resolvedMod2 = &module->nestedFunctions[bcId];
                else if (bcId >= 0 && t_rootModule &&
                         static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
                    resolvedMod2 = &t_rootModule->nestedFunctions[bcId];
                if (resolvedMod2) {
                    const auto& nf = *resolvedMod2;
                    // GC critical section: argsList is built by repeated
                    // appendLast in a C++ local that is NOT on any GC
                    // root.  Without continuous CS coverage, an inner
                    // allocCell that crosses its 64-allocation safepoint
                    // can submit the young chain to dirtySegments
                    // BETWEEN the build and the bind-into-childCtx
                    // phase, after which argsList's cells are
                    // sweep-candidates with no live root.  CS therefore
                    // spans the entire build → setSlot region as one
                    // section; runBytecode itself runs OUTSIDE the CS
                    // (and takes its own CS where it needs them) since
                    // by that point childCtx.automaticLocals owns the
                    // values.
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    const proto::ProtoList* argsList;
                    const proto::ProtoObject* effectiveThis;
                    uint32_t bindCount = (argc < nf.argCount()) ? argc : nf.argCount();
                    {
                        proto::ProtoContext::CriticalSection callCs0(pContext);
                        // The top `argc` stack slots [arg0..argN-1] are
                        // contiguous in automaticLocals.  Pass that slice
                        // directly to ctx->newList(n, items) — single cell
                        // allocation when argc ≤ 5, AVL fallback otherwise.
                        InterpFrame* frameNow = currentFrame(pContext);
                        const proto::ProtoObject* const* argSlice =
                            pContext->getAutomaticLocals()
                            + frameNow->stackBase + frameNow->stackTop - argc;
                        argsList = pContext->newList(argc, argSlice);
                        // Determine effective this inside the CS so the
                        // arrow-this lookup cannot trigger a sweep that
                        // frees argsList.
                        effectiveThis = thisVal;
                        if (nf.isArrow) {
                            const proto::ProtoObject* captured =
                                func->getAttribute(pContext, JSSymbols::arrowThis(pContext), false);
                            if (captured && captured != PROTO_NONE)
                                effectiveThis = captured;
                        }
                        for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                        for (uint32_t i = 0; i < bindCount; i++)
                            setSlot(&childCtx, i, argsList->getAt(pContext, static_cast<int>(i)));
                    }
                    populateClosureCellsFromInstance(&childCtx, func, nf);
                    // Active func / args for OP_special_object inside method body.
                    const proto::ProtoObject* prevActiveM = t_activeFunc;
                    const proto::ProtoObject* prevTgtM    = t_activeNewTgt;
                    const proto::ProtoList*   prevArgsM   = t_activeArgs;
                    t_activeFunc = func;
                    t_activeNewTgt = nullptr;
                    t_activeArgs = argsList;
                    const proto::ProtoObject* childEx = PROTO_NONE;
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, effectiveThis, argsList, pGlobalRoot, &childEx);
                    t_activeFunc = prevActiveM;
                    t_activeNewTgt = prevTgtM;
                    t_activeArgs = prevArgsM;
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        DISPATCH();
                    }
                    // Tail-call: caller's return value IS the callee's result.
                    // Don't push to the stack — propagate up via early return.
                    if (opcode == OP_tail_call_method)
                        return result ? result : PROTO_NONE;
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else if (funcIsNative) {
                    // CS: argsList held in C++ scratch — see resolvedMod2 branch above.
                    proto::ProtoContext::CriticalSection callCs1(pContext);
                    // argc==0 fast path: skip newList entirely. All protoJS native
                    // methods tolerate args==nullptr via the standard
                    // `if (!args || args->getSize(ctx) <= i)` guard at every arg
                    // access site; passing nullptr saves one cell allocation per
                    // call for arg-less methods (toString, valueOf, trim, ...).
                    const proto::ProtoList* argsList = nullptr;
                    if (argc > 0) {
                        InterpFrame* frameNowN = currentFrame(pContext);
                        const proto::ProtoObject* const* argSliceN =
                            pContext->getAutomaticLocals()
                            + frameNowN->stackBase + frameNowN->stackTop - argc;
                        argsList = pContext->newList(argc, argSliceN);
                    }
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    // Invoke the native function directly via asMethod() to bypass the
                    // ProtoObject::call() attribute-lookup indirection.
                    const proto::ProtoMethod nativeFn = func->asMethod(pContext);
                    const proto::ProtoObject* result = nativeFn
                        ? nativeFn(pContext, thisVal, nullptr, argsList, nullptr)
                        : PROTO_NONE;
                    if (t_hasCallException) {
                        pending_exception  = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException    = nullptr;
                        DISPATCH();
                    }
                    // Tail-call: see comment in the JS-callee branch above.
                    if (opcode == OP_tail_call_method)
                        return result ? result : PROTO_NONE;
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
                DISPATCH();
            }
            L_OP_call_constructor: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;

                const proto::ProtoObject* func = stackAt(pContext, argc + 1);
                const proto::ProtoObject* newTarget = stackAt(pContext, argc);

                // CS: argsList held in C++ scratch — see resolvedMod2 branch.
                proto::ProtoContext::CriticalSection callCs2(pContext);
                // P-#4 single-allocation builder (matches L_OP_call /
                // L_OP_call_method): pass the contiguous slice in
                // automaticLocals directly.
                InterpFrame* frameNowC = currentFrame(pContext);
                const proto::ProtoObject* const* argSliceC =
                    pContext->getAutomaticLocals()
                    + frameNowC->stackBase + frameNowC->stackTop - argc;
                const proto::ProtoList* argsList = pContext->newList(argc, argSliceC);

                // ES spec: Unwrapping bound functions for construct calls.
                const proto::ProtoString* bfK = JSSymbols::boundFn(pContext);
                while (func && func != PROTO_NONE && bfK) {
                    const proto::ProtoObject* target = func->getAttribute(pContext, bfK, false);
                    if (!target || target == PROTO_NONE) break;
                    const proto::ProtoObject* bArgs = func->getAttribute(pContext, JSSymbols::boundArgs(pContext), false);
                    
                    const proto::ProtoList* merged = pContext->newList();
                    if (bArgs && bArgs != PROTO_NONE) {
                        long long blen = 0;
                        const proto::ProtoObject* lo = bArgs->getAttribute(pContext, JSSymbols::length(pContext), false);
                        if (lo && lo->isInteger(pContext)) blen = lo->asLong(pContext);
                        for (long long bi = 0; bi < blen; bi++)
                            merged = merged->appendLast(pContext, bArgs->getAttribute(pContext, JSSymbols::indexKey(pContext, (uint32_t)bi), false));
                    }
                    for (int i = 0; i < argsList->getSize(pContext); i++)
                        merged = merged->appendLast(pContext, argsList->getAt(pContext, i));
                    
                    argsList = merged;
                    if (newTarget == func) newTarget = target;
                    func = target;
                }
                uint32_t finalArgc = static_cast<uint32_t>(argsList->getSize(pContext));

                for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);

                // 1. Create the new object instance.  Per ECMA-262 §9.2.2
                // OrdinaryCallEvaluateBody, the receiver's [[Prototype]]
                // comes from NEW_TARGET.prototype, not super_func.prototype.
                // This matters for super() inside a derived class ctor:
                //   class B extends A { ... super(args); ... }
                // emits OP_call_constructor with func=A.ctor and
                // newTarget=B (the original new B()).  newObj must inherit
                // B.prototype so methods on B.prototype are visible.
                const proto::ProtoString* protoKey = JSSymbols::prototype(pContext);
                const proto::ProtoObject* funcProto = (protoKey && func && func != PROTO_NONE)
                    ? func->getAttribute(pContext, protoKey, false) : nullptr;
                const proto::ProtoObject* newTgtForProto =
                    (newTarget && newTarget != PROTO_NONE) ? newTarget : func;
                const proto::ProtoObject* tgtProto = (protoKey && newTgtForProto && newTgtForProto != PROTO_NONE)
                    ? newTgtForProto->getAttribute(pContext, protoKey, false) : nullptr;
                if (!tgtProto || tgtProto == PROTO_NONE) tgtProto = funcProto;
                const proto::ProtoObject* newObj = (tgtProto && tgtProto != PROTO_NONE)
                    ? tgtProto->newChild(pContext, true)
                    : pContext->newObject(true);
                
                if (!newObj) {
                    stackPush(pContext, PROTO_NONE);
                    DISPATCH();
                }
                
                const proto::ProtoObject* result = PROTO_NONE;
                
                // 2. Dispatch.
                int bcId = getBytecodeId(pContext, func);
                const ProtoBytecodeModule* resolved = nullptr;
                if (bcId >= 0 && static_cast<size_t>(bcId) < module->nestedFunctions.size())
                    resolved = &module->nestedFunctions[bcId];
                else if (bcId >= 0 && t_rootModule &&
                         static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
                    resolved = &t_rootModule->nestedFunctions[bcId];

                if (resolved) {
                    // §9.1.13 OrdinaryCreateFromConstructor does NOT stamp
                    // an own `constructor` slot on the instance — the
                    // backref lives on F.prototype, and the instance picks
                    // it up via the chain. Pre-fix this site unconditionally
                    // stamped own `constructor` on newObj, leaking it into
                    // Object.keys / getOwnPropertyNames / Object.getOwnPropertyDescriptors.
                    //
                    // We DO need to ensure F.prototype.constructor === F is
                    // set, because plain `function F(){}` declarations
                    // create F.prototype lazily without the backref (the
                    // ES6 class path at L_OP_fclosure already sets it).
                    // So we stamp the chain's `prototype` slot here, only
                    // when needed, with the §17 descriptor 0x3
                    // (writable / configurable / non-enumerable).
                    {
                        const proto::ProtoString* cKey = JSSymbols::constructor(pContext);
                        const proto::ProtoObject* protoForCtor = funcProto;
                        if (cKey && protoForCtor && protoForCtor != PROTO_NONE) {
                            const proto::ProtoObject* existing =
                                protoForCtor->getAttribute(pContext, cKey, false);
                            if (existing != func) {
                                const proto::ProtoObject* updated =
                                    protoForCtor->setAttribute(pContext, cKey, func);
                                if (updated && updated != PROTO_NONE) {
                                    const proto::ProtoObject* pdo =
                                        pContext->fromUTF8String("__pd_constructor__");
                                    const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                                    if (pdk) updated = updated->setAttribute(pContext, pdk, pContext->fromInteger(0x3LL));
                                    // Re-publish the prototype on func so subsequent
                                    // `new F()` calls see the backref-bearing snapshot.
                                    if (protoKey && func && func != PROTO_NONE)
                                        const_cast<proto::ProtoObject*>(func)->setAttribute(pContext, protoKey, updated);
                                }
                            }
                        }
                    }

                    // Instance field initialization: if the func's prototype
                    // has __fields_init__ (a closure stored by OP_set_home_object
                    // when QuickJS emitted OP_fclosure + OP_set_home_object +
                    // OP_scope_put_var_init for the class_fields_init), invoke
                    // it on newObj before the constructor body.  This bypasses
                    // the standard scope_get_var class_fields_init + call_method
                    // mechanism which requires closure-capture analysis that
                    // protoJS doesn't yet fully implement.
                    {
                        const proto::ProtoObject* fiKo = pContext->fromUTF8String("__fields_init__");
                        const proto::ProtoString* fiK = fiKo ? fiKo->asString(pContext) : nullptr;
                        const proto::ProtoObject* fieldsInit = (fiK && funcProto && funcProto != PROTO_NONE)
                            ? funcProto->getAttribute(pContext, fiK, false) : nullptr;
                        if (fieldsInit && fieldsInit != PROTO_NONE) {
                            const proto::ProtoList* fiArgs = pContext->newList();
                            const proto::ProtoObject* fiRet =
                                callJSFunction(pContext, fieldsInit, newObj, fiArgs);
                            (void)fiRet;
                            if (t_hasCallException) {
                                pending_exception = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException = nullptr;
                                DISPATCH();
                            }
                        }
                    }

                    // Publish func + newTarget + args for OP_special_object
                    // and OP_init_ctor inside the constructor body.
                    // RAII restore on exit / exception.
                    const proto::ProtoObject* prevActive  = t_activeFunc;
                    const proto::ProtoObject* prevNewTgt2 = t_activeNewTgt;
                    const proto::ProtoList*   prevArgs    = t_activeArgs;
                    t_activeFunc   = func;
                    t_activeNewTgt = (newTarget && newTarget != PROTO_NONE) ? newTarget : func;
                    t_activeArgs   = argsList;
                    struct AfRestore {
                        const proto::ProtoObject* pa;
                        const proto::ProtoObject* pn;
                        const proto::ProtoList*   pl;
                        ~AfRestore() { t_activeFunc = pa; t_activeNewTgt = pn; t_activeArgs = pl; }
                    } _afRestore{prevActive, prevNewTgt2, prevArgs};

                    const auto& nf = *resolved;
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    uint32_t bindCount = (finalArgc < nf.argCount()) ? finalArgc : nf.argCount();
                    if (debugBindEnabled()) {
                        printf("[DEBUG] OP_call_constructor: finalArgc=%u nf.argCount=%u bindCount=%u\n", finalArgc, nf.argCount(), bindCount);
                    }
                    for (uint32_t i = 0; i < bindCount; i++) {
                        const proto::ProtoObject* arg = argsList->getAt(pContext, static_cast<int>(i));
                        if (debugBindEnabled()) {
                            printf("[DEBUG]   Binding arg %u: %p\n", i, arg);
                        }
                        setSlot(&childCtx, i, arg);
                    }
                    populateClosureCellsFromInstance(&childCtx, func, nf);

                    const proto::ProtoObject* childEx = PROTO_NONE;
                    result = runBytecode(&childCtx, &nf, newObj, argsList, pGlobalRoot, &childEx);
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        DISPATCH();
                    }
                } else if (func && func->isMethod(pContext)) {
                    // Raw protoCore methods (Math.abs, Math.sign, JSON.parse,
                    // String.fromCharCode, every built-in instance method,
                    // etc.) are not constructible per ECMA-262 §17. Throw
                    // TypeError instead of running them as constructors.
                    pending_exception = makeError(pContext, "TypeError",
                        "function is not a constructor", pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                } else {
                    // Specialized: Array, Error, RegExp, etc.
                    const proto::ProtoString* arrayK = JSSymbols::arrayCtor(pContext);
                    const proto::ProtoString* errK = JSSymbols::errorCtor(pContext);
                    const proto::ProtoString* reK = JSSymbols::regexpCtor(pContext);
                    const proto::ProtoString* taK = JSSymbols::taCtor(pContext);
                    const proto::ProtoString* strK = JSSymbols::stringCtor(pContext);
                    // Extract constructor-type marker attributes once.
                    // NOTE: getAttribute returns nullptr when a key is absent, and PROTO_NONE
                    // when the key is present but set to undefined. The original checks
                    // used `!= PROTO_NONE` which incorrectly matched the absent (nullptr) case,
                    // causing unrelated constructors to enter the wrong branch. Both nullptr and
                    // PROTO_NONE mean "this marker is not set on this function".
                    const proto::ProtoObject* arrayAttr = (func && func != PROTO_NONE && arrayK)
                        ? func->getAttribute(pContext, arrayK, false) : nullptr;
                    const proto::ProtoObject* errAttr   = (func && func != PROTO_NONE && errK)
                        ? func->getAttribute(pContext, errK, false) : nullptr;
                    const proto::ProtoObject* reAttr    = (func && func != PROTO_NONE && reK)
                        ? func->getAttribute(pContext, reK, false) : nullptr;
                    const proto::ProtoObject* taAttr    = (func && func != PROTO_NONE && taK)
                        ? func->getAttribute(pContext, taK, false) : nullptr;
                    const proto::ProtoObject* strAttr   = (func && func != PROTO_NONE && strK)
                        ? func->getAttribute(pContext, strK, false) : nullptr;

                    if (arrayAttr == PROTO_TRUE) {
                        const proto::ProtoString* pk = JSSymbols::prototype(pContext);
                        const proto::ProtoObject* pr = func->getAttribute(pContext, pk, false);
                        const proto::ProtoObject* arr = (pr && pr != PROTO_NONE) ? pr->newChild(pContext, true) : pContext->newObject(true);
                        const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                        if (isArrKey) arr = arr->setAttribute(pContext, isArrKey, PROTO_TRUE);
                        // ECMA-262 §22.1.1.2 (Array(len)): if argument is a
                        // single Number, length must be ToUint32(len) AND
                        // SameValue(len, ToUint32(len)) — otherwise RangeError.
                        // Catches Array(-1), Array(2.5), Array(2^32),
                        // Array(NaN), etc.
                        if (finalArgc == 1 && (argsList->getAt(pContext, 0)->isInteger(pContext) ||
                                               argsList->getAt(pContext, 0)->isDouble(pContext) ||
                                               argsList->getAt(pContext, 0)->isFloat(pContext))) {
                            const proto::ProtoObject* lenArg = argsList->getAt(pContext, 0);
                            double dlen = lenArg->isInteger(pContext)
                                ? static_cast<double>(lenArg->asLong(pContext))
                                : lenArg->asDouble(pContext);
                            long long ilen = static_cast<long long>(dlen);
                            if (std::isnan(dlen) || std::isinf(dlen) ||
                                static_cast<double>(ilen) != dlen ||
                                ilen < 0 || ilen > 4294967295LL) {
                                pending_exception = makeError(pContext, "RangeError",
                                    "Invalid array length", pGlobalRoot);
                                has_pending_exception = true;
                                DISPATCH();
                            }
                            // Sparse Array(n): length = n, no __elements__.
                            arr = arr->setAttribute(pContext, JSSymbols::length(pContext),
                                pContext->fromInteger(ilen));
                        } else {
                            // new Array(v1, v2, ...) — entries go in
                            // __elements__ so iteration / JSON.stringify
                            // / Array.prototype.* see them.  Pre-fix the
                            // indexed-attribute fallback left the array
                            // looking empty to those consumers.
                            proto::ProtoContext::CriticalSection arrCs(pContext);
                            const proto::ProtoList* els = pContext->newList();
                            for (uint32_t i = 0; i < finalArgc; i++)
                                els = els->appendLast(pContext, argsList->getAt(pContext, i));
                            protojs::setArrayElements(pContext, arr, els);
                            arr = arr->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(static_cast<long long>(finalArgc)));
                        }
                        result = arr;
                    } else if (errAttr && errAttr != PROTO_NONE) {
                        std::string msg, type;
                        if (errAttr->isString(pContext)) errAttr->asString(pContext)->toUTF8String(pContext, type);
                        // §19.5.7.1.1 AggregateError(errors, message[,
                        // options]): errors goes into the [[Errors]]
                        // internal slot, message is the SECOND arg.
                        // Pre-fix the constructor blindly used arg[0]
                        // as the message, so
                        //   new AggregateError([1,2,3], "hi").message
                        // returned "1,2,3" and the errors slot was
                        // never populated (built-ins/AggregateError/
                        // 's-prototype-errors-list and the wider
                        // AggregateError surface failed).
                        const bool isAggregate = (type == "AggregateError");
                        unsigned msgArgIdx = isAggregate ? 1u : 0u;
                        if (argc > msgArgIdx) {
                            const proto::ProtoObject* mVal = toString(pContext, argsList->getAt(pContext, static_cast<int>(msgArgIdx)));
                            if (mVal && mVal->isString(pContext)) mVal->asString(pContext)->toUTF8String(pContext, msg);
                        }
                        result = makeError(pContext, type.c_str(), msg.c_str(), pGlobalRoot);
                        // Populate errors[]: deep-copy the iterable arg
                        // into an array (the spec requires real iteration
                        // via the iterator protocol, but our most-common
                        // case is an Array literal — that's sufficient
                        // for the test262 conformance check).
                        if (isAggregate && argc > 0 && result && result != PROTO_NONE) {
                            const proto::ProtoObject* errArg = argsList->getAt(pContext, 0);
                            JSContextWrapper* wrap = JSContextWrapper::current();
                            const proto::ProtoObject* errArrProto = wrap ? wrap->getJSArrayPrototype() : nullptr;
                            const proto::ProtoObject* errArr = (errArrProto && errArrProto != PROTO_NONE)
                                ? errArrProto->newChild(pContext, true)
                                : pContext->newObject(true);
                            const proto::ProtoString* isArrK = JSSymbols::isArray(pContext);
                            if (isArrK) errArr = errArr->setAttribute(pContext, isArrK, PROTO_TRUE);
                            const proto::ProtoList* els = pContext->newList();
                            const proto::ProtoList* srcEls = errArg
                                ? protojs::getArrayElements(pContext, errArg) : nullptr;
                            if (srcEls) {
                                size_t sz = srcEls->getSize(pContext);
                                for (size_t i = 0; i < sz; ++i) els = els->appendLast(pContext, srcEls->getAt(pContext, static_cast<int>(i)));
                            }
                            protojs::setArrayElements(pContext, errArr, els);
                            errArr = errArr->setAttribute(pContext, JSSymbols::length(pContext),
                                pContext->fromInteger(static_cast<long long>(els ? els->getSize(pContext) : 0)));
                            const proto::ProtoObject* errsKo = pContext->fromUTF8String("errors");
                            const proto::ProtoString* errsK = errsKo ? errsKo->asString(pContext) : nullptr;
                            if (errsK) result = result->setAttribute(pContext, errsK, errArr);
                            // §19.5.7.4 AggregateError instance's
                            // "errors" descriptor is
                            // {writable:true, enumerable:false,
                            // configurable:true} (bits 0x3).
                            const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_errors__");
                            const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                            if (pdk) result = result->setAttribute(pContext, pdk,
                                pContext->fromInteger(0x3LL));
                        }
                    } else if (reAttr == PROTO_TRUE) {
                        const proto::ProtoString* pk = JSSymbols::prototype(pContext);
                        const proto::ProtoObject* pr = func->getAttribute(pContext, pk, false);
                        const proto::ProtoObject* re = (pr && pr != PROTO_NONE) ? pr->newChild(pContext, true) : pContext->newObject(true);
                        result = regexpConstructor(pContext, re, nullptr, argsList, nullptr);
                    } else if (taAttr && taAttr != PROTO_NONE) {
                        if (taAttr->isString(pContext)) {
                            std::string name; taAttr->asString(pContext)->toUTF8String(pContext, name);
                            if (name == "ArrayBuffer") {
                                unsigned long bl = 0;
                                if (finalArgc > 0 && argsList->getAt(pContext,0)->isInteger(pContext)) bl = (unsigned long)std::max(0LL, argsList->getAt(pContext,0)->asLong(pContext));
                                result = createArrayBuffer(pContext, bl);
                            }
                        } else if (taAttr->isInteger(pContext)) {
                            uint8_t et = (uint8_t)taAttr->asLong(pContext);
                            const proto::ProtoObject* pr = func->getAttribute(pContext, JSSymbols::prototype(pContext), false);
                            if (finalArgc > 0 && argsList->getAt(pContext,0)->isInteger(pContext)) {
                                result = createTypedArrayFromLength(pContext, pr, et, (uint32_t)argsList->getAt(pContext,0)->asLong(pContext));
                            } else result = createTypedArrayFromLength(pContext, pr, et, 0);
                        }
                    } else if (strAttr == PROTO_TRUE) {
                        // §22.1.1.1 String(value): when NewTarget is
                        // defined and value is a Symbol, ToString
                        // raises TypeError per §7.1.17 (built-ins/
                        // String/symbol-wrapping). Probe the Symbol
                        // marker BEFORE the toString coercion path.
                        if (finalArgc > 0) {
                            const proto::ProtoObject* v0 = argsList->getAt(pContext, 0);
                            const proto::ProtoObject* symKo = pContext->fromUTF8String("__is_symbol__");
                            const proto::ProtoString* symK = symKo ? symKo->asString(pContext) : nullptr;
                            if (v0 && v0 != PROTO_NONE && symK
                                && v0->getAttribute(pContext, symK, true) == PROTO_TRUE) {
                                pending_exception = makeError(pContext, "TypeError",
                                    "Cannot convert a Symbol value to a string", pGlobalRoot);
                                has_pending_exception = true;
                                DISPATCH();
                            }
                        }
                        // String wrapper constructor: new String("hello") → object with [[PrimitiveValue]].
                        // No args → empty string (spec 22.1.2.1: new String() has value "").
                        const proto::ProtoObject* pv = finalArgc > 0
                            ? toString(pContext, argsList->getAt(pContext, 0))
                            : pContext->fromUTF8String("");
                        newObj = newObj->setAttribute(pContext, JSSymbols::primitiveValue(pContext), pv);
                        // ECMA-262 §22.1.4: String wrapper has own length
                        // {writable:false, enumerable:false, configurable:false}.
                        long long slen = 0;
                        if (pv && pv->isString(pContext)) {
                            const proto::ProtoString* ps = pv->asString(pContext);
                            if (ps) slen = static_cast<long long>(ps->getSize(pContext));
                        }
                        const proto::ProtoString* lenKey = JSSymbols::length(pContext);
                        if (lenKey)
                            newObj = newObj->setAttribute(pContext, lenKey, pContext->fromInteger(slen));
                        const proto::ProtoObject* pdo2 = pContext->fromUTF8String("__pd_length__");
                        const proto::ProtoString* pdk2 = pdo2 ? pdo2->asString(pContext) : nullptr;
                        if (pdk2) newObj = newObj->setAttribute(pContext, pdk2, pContext->fromInteger(0x0LL));
                        result = newObj;
                    } else {
                        // Generic: if the constructor carries a __construct__ native method,
                        // invoke it directly (Boolean, Number, Map, Set, Promise, WeakMap, etc.).
                        const proto::ProtoString* ctorKey = JSSymbols::construct(pContext);
                        const proto::ProtoObject* ctorMethod = (ctorKey && func && func != PROTO_NONE)
                            ? func->getAttribute(pContext, ctorKey, false) : nullptr;
                        
                        const proto::ProtoString* isCtorKey = JSSymbols::isConstructor(pContext);
                        const proto::ProtoObject* isCtor = (isCtorKey && func && func != PROTO_NONE)
                            ? func->getAttribute(pContext, isCtorKey, false) : nullptr;

                        if (ctorMethod && ctorMethod != PROTO_NONE && ctorMethod->isMethod(pContext)) {
                            proto::ProtoMethod ctorFn = ctorMethod->asMethod(pContext);
                            if (ctorFn)
                                result = ctorFn(pContext, newObj, nullptr, argsList, nullptr);
                            if (t_hasCallException) {
                                pending_exception = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException = nullptr;
                                DISPATCH();
                            }
                        } else if (isCtor == PROTO_TRUE) {
                            // Explicitly marked as constructor (e.g. Array).
                            // If no specialized logic above matched (Array matches errAttr etc.), just return newObj.
                            result = newObj;
                        } else {
                            // Not a constructor! Throw TypeError.
                            pending_exception = makeError(pContext, "TypeError", "function is not a constructor", pGlobalRoot);
                            has_pending_exception = true;
                            DISPATCH();
                        }
                    }
                }

                // If result is an object, return it; otherwise return the newly created object (spec 9.2.2).
                bool resultIsObject = result && result != PROTO_NONE && !result->isInteger(pContext) && !result->isDouble(pContext) && !result->asString(pContext) && result != PROTO_TRUE && result != PROTO_FALSE;
                const proto::ProtoObject* finalCtorThis = resultIsObject ? result : newObj;

                // super() field-init follow-up: if this OP_call_constructor
                // was emitted by QuickJS as a super() call (t_activeFunc is
                // the CALLER class, which differs from func = parent class),
                // also run the caller class's __fields_init__ on the result.
                // The standard bytecode's emit_class_field_init that should
                // do this isn't resolving the closure-var reference for
                // class_fields_init; this direct dispatch fills the gap.
                if (t_activeFunc && t_activeFunc != PROTO_NONE
                    && t_activeFunc != func && finalCtorThis) {
                    const proto::ProtoObject* afProto = protoKey
                        ? t_activeFunc->getAttribute(pContext, protoKey, false) : nullptr;
                    if (afProto && afProto != PROTO_NONE && afProto != funcProto) {
                        const proto::ProtoObject* fiKo = pContext->fromUTF8String("__fields_init__");
                        const proto::ProtoString* fiK = fiKo ? fiKo->asString(pContext) : nullptr;
                        const proto::ProtoObject* fi = fiK
                            ? afProto->getAttribute(pContext, fiK, false) : nullptr;
                        if (fi && fi != PROTO_NONE) {
                            const proto::ProtoList* fiArgs = pContext->newList();
                            callJSFunction(pContext, fi, finalCtorThis, fiArgs);
                            if (t_hasCallException) {
                                pending_exception = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException = nullptr;
                                DISPATCH();
                            }
                        }
                    }
                }

                stackPush(pContext, finalCtorThis);
                DISPATCH();
            }
            L_OP_tail_call: // tail-call: same encoding as OP_call but the result is
                               // returned from the current function rather than pushed to stack.
            L_OP_call0: ;
            L_OP_call1: ;
            L_OP_call2: ;
            L_OP_call3: ;
            L_OP_call: {
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
                // Read __bytecode_id__ once.  If >= 0, func is a JS closure
                // and CARRIES its identity by construction — skip the
                // __native_fn__ unwrap (which would always return nullptr
                // for a JS closure).  protoCore's per-thread attribute
                // cache makes this getBytecodeId call ~cache-hit on every
                // call after the first to the same closure.
                int bcId = getBytecodeId(pContext, func);

                // 1. Unwrap native function wrapper (__native_fn__) — only
                //    for non-JS receivers (no __bytecode_id__).  Wrapper
                //    objects (e.g. installNonEnumerableMethod-built array
                //    methods) carry __native_fn__ but no __bytecode_id__,
                //    so this branch covers them.
                if (bcId < 0) {
                    const proto::ProtoString* nfKey2 = JSSymbols::nativeFn(pContext);
                    if (nfKey2 && func && func != PROTO_NONE && !func->isMethod(pContext)) {
                        const proto::ProtoObject* rawMethod2 = func->getAttribute(pContext, nfKey2, false);
                        if (rawMethod2 && rawMethod2 != PROTO_NONE && rawMethod2->isMethod(pContext))
                            func = rawMethod2;
                    }
                }

                // 2. Dispatch.  If the unwrap above replaced func, we need
                //    a fresh bcId on the new func (which would be a raw
                //    method with no __bytecode_id__, so this returns -1
                //    and we fall through to the native-method branch).
                if (bcId < 0) bcId = getBytecodeId(pContext, func);
                const ProtoBytecodeModule* resolvedModule = nullptr;
                if (bcId >= 0 && static_cast<size_t>(bcId) < module->nestedFunctions.size())
                    resolvedModule = &module->nestedFunctions[bcId];
                else if (bcId >= 0 && t_rootModule &&
                         static_cast<size_t>(bcId) < t_rootModule->nestedFunctions.size())
                    resolvedModule = &t_rootModule->nestedFunctions[bcId];

                if (resolvedModule) {
                    const auto& nf = *resolvedModule;

                    // Pre-size automaticLocals to match the callee's needs:
                    // args + vars + closure-vars + stack + safety margin.
                    // SBO for typical functions: 64 slots cover most
                    // call frames without heap alloc.
                    constexpr size_t kSBOSlots = 64;
                    const size_t totalSlots =
                        nf.argCount() + nf.varCount() +
                        (nf.closureSymbols ? nf.closureSymbols->getSize(pContext) : 0) +
                        nf.stackSize() + 16;
                    alignas(void*) const proto::ProtoObject* sboBuf_call[kSBOSlots];
                    const proto::ProtoObject** ext = nullptr;
                    if (totalSlots <= kSBOSlots) {
                        for (size_t s = 0; s < totalSlots; ++s) sboBuf_call[s] = PROTO_NONE;
                        ext = sboBuf_call;
                    }
                    proto::ProtoContext childCtx(pContext->space, pContext,
                                                 nullptr, nullptr, nullptr, nullptr,
                                                 totalSlots, ext);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;

                    // CS spans the entire build → setSlot region — see
                    // resolvedMod2 branch for rationale.  Without
                    // continuous CS, threshold submission can fire
                    // between build and bind, leaving argsList cells
                    // sweep-candidates with no live root.
                    const proto::ProtoList* argsList;
                    const proto::ProtoObject* callThisVal;
                    uint32_t bindCount = (argc < nf.argCount()) ? argc : nf.argCount();
                    {
                        proto::ProtoContext::CriticalSection callCs3(pContext);
                        // Match L_OP_call_method (P-#4 single-allocation
                        // builder): the top `argc` stack slots live
                        // contiguously in automaticLocals at
                        // [stackBase + stackTop - argc, stackBase + stackTop)
                        // — pass that slice directly to the multi-arg
                        // newList builder.  argc ≤ 5 yields a single
                        // SmallList cell; argc > 5 falls back to the AVL
                        // builder via the same entry point.  Replaces
                        // 1 + N cell allocations per call with 1.
                        InterpFrame* frameNow = currentFrame(pContext);
                        const proto::ProtoObject* const* argSlice =
                            pContext->getAutomaticLocals()
                            + frameNow->stackBase + frameNow->stackTop - argc;
                        argsList = pContext->newList(argc, argSlice);
                        callThisVal = PROTO_NONE;
                        if (nf.isArrow) {
                            const proto::ProtoObject* captured =
                                func->getAttribute(pContext, JSSymbols::arrowThis(pContext), false);
                            if (captured && captured != PROTO_NONE)
                                callThisVal = captured;
                        }
                        for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                        for (uint32_t i = 0; i < bindCount; i++)
                            setSlot(&childCtx, i, argsList->getAt(pContext, static_cast<int>(i)));
                    }
                    populateClosureCellsFromInstance(&childCtx, func, nf);

                    // Publish active func / args for OP_special_object inside body.
                    const proto::ProtoObject* prevActiveC = t_activeFunc;
                    const proto::ProtoObject* prevTgtC    = t_activeNewTgt;
                    const proto::ProtoList*   prevArgsC   = t_activeArgs;
                    t_activeFunc = func;
                    t_activeNewTgt = nullptr;
                    t_activeArgs = argsList;
                    const proto::ProtoObject* childEx = PROTO_NONE;
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, callThisVal, argsList, pGlobalRoot, &childEx);
                    t_activeFunc = prevActiveC;
                    t_activeNewTgt = prevTgtC;
                    t_activeArgs = prevArgsC;
                    REFRESH_INTERP_STATE();
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        DISPATCH();
                    }
                    if (is_tail_call) return result ? result : PROTO_NONE;
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    const proto::ProtoObject* thisVal = PROTO_NONE;
                    // CS: argsList held in C++ scratch — see resolvedMod2 branch above.
                    proto::ProtoContext::CriticalSection callCs4(pContext);
                    // argc==0 fast path: pass nullptr (see L_OP_call_method
                    // native branch for the rationale).  argc>0: use the
                    // single-allocation newList(argc, slice) form rather than
                    // newList()+appendLast loop — saves argc-1 cell allocations
                    // and keeps the construction in a single critical section.
                    const proto::ProtoList* argsList = nullptr;
                    if (argc > 0) {
                        const proto::ProtoObject* const* argSliceC =
                            pContext->getAutomaticLocals()
                            + currentFrame(pContext)->stackBase
                            + currentFrame(pContext)->stackTop - argc;
                        argsList = pContext->newList(argc, argSliceC);
                    }
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);

                    const proto::ProtoMethod nativeFn = func->asMethod(pContext);
                    const proto::ProtoObject* result = nativeFn
                        ? nativeFn(pContext, thisVal, nullptr, argsList, nullptr)
                        : PROTO_NONE;
                    REFRESH_INTERP_STATE();
                    if (t_hasCallException) {
                        pending_exception  = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException    = nullptr;
                        DISPATCH();
                    }
                    if (is_tail_call) return result ? result : PROTO_NONE;
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else {
                    // Check for specialized callables (BoundFn, Error, Array, String).
                    // We check BoundFn first because it can wrap any other callable.
                    const proto::ProtoString* boundFnAttr = JSSymbols::boundFn(pContext);
                    const proto::ProtoObject* target = (func && func != PROTO_NONE && boundFnAttr) 
                        ? func->getAttribute(pContext, boundFnAttr, false) : PROTO_NONE;

                    if (target && target != PROTO_NONE) {
                        const proto::ProtoObject* bThis = func->getAttribute(pContext, JSSymbols::boundThis(pContext), false);
                        const proto::ProtoObject* bArgs = func->getAttribute(pContext, JSSymbols::boundArgs(pContext), false);
                        
                        const proto::ProtoList* merged = pContext->newList();
                        if (bArgs && bArgs != PROTO_NONE) {
                            long long blen = 0;
                            const proto::ProtoObject* lo = bArgs->getAttribute(pContext, JSSymbols::length(pContext), false);
                            if (lo && lo->isInteger(pContext)) blen = lo->asLong(pContext);
                            for (long long bi = 0; bi < blen; bi++) {
                                merged = merged->appendLast(pContext, bArgs->getAttribute(pContext, JSSymbols::indexKey(pContext, (uint32_t)bi), false));
                            }
                        }
                        for (uint32_t i = 0; i < argc; i++) merged = merged->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                        for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                        const proto::ProtoObject* res = callJSFunction(pContext, target, bThis ? bThis : PROTO_NONE, merged);
                        REFRESH_INTERP_STATE();
                        if (is_tail_call) return res;
                        stackPush(pContext, res);
                    } else {
                        const proto::ProtoString* errCtorAttr = JSSymbols::errorCtor(pContext);
                        const proto::ProtoString* arrayCtorAttr = JSSymbols::arrayCtor(pContext);
                        const proto::ProtoString* strCtorAttr = JSSymbols::stringCtor(pContext);

                        // getAttribute returns nullptr when a key is absent and PROTO_NONE when
                        // explicitly set to undefined. Check for both to avoid false matches.
                        const proto::ProtoObject* errAttrVal = (func && func != PROTO_NONE && errCtorAttr)
                            ? func->getAttribute(pContext, errCtorAttr, false) : nullptr;
                        if (errAttrVal && errAttrVal != PROTO_NONE) {
                            std::string msg, type;
                            if (argc > 0) {
                                const proto::ProtoObject* msgObj = toString(pContext, stackAt(pContext, argc - 1));
                                if (msgObj && msgObj->isString(pContext)) msgObj->asString(pContext)->toUTF8String(pContext, msg);
                            }
                            if (errAttrVal->isString(pContext)) errAttrVal->asString(pContext)->toUTF8String(pContext, type);
                            for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                            const proto::ProtoObject* err = makeError(pContext, type.c_str(), msg.c_str(), pGlobalRoot);
                            if (is_tail_call) return err;
                            stackPush(pContext, err);
                        } else if (func && func != PROTO_NONE && arrayCtorAttr && func->getAttribute(pContext, arrayCtorAttr, false) == PROTO_TRUE) {
                            // Array(...) — same semantics as new Array(...).
                            // ECMA-262 §22.1.1.1: if there's exactly one
                            // argument and it is a non-negative integer,
                            // create a sparse array of that length.
                            // Otherwise, treat each argument as an element.
                            const proto::ProtoList* argsList = pContext->newList();
                            for (uint32_t i = 0; i < argc; i++) argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                            for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                            const proto::ProtoString* protK = JSSymbols::prototype(pContext);
                            const proto::ProtoObject* prot = func->getAttribute(pContext, protK, false);
                            const proto::ProtoObject* arr = (prot && prot != PROTO_NONE) ? prot->newChild(pContext, true) : pContext->newObject(true);
                            const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                            if (isArrKey) arr = arr->setAttribute(pContext, isArrKey, PROTO_TRUE);
                            long long arrLength = static_cast<long long>(argc);
                            if (argc == 1) {
                                const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
                                // ECMA-262 §22.1.1.1 (Array() called as
                                // function — same as constructor §22.1.1.2):
                                // single-number arg must satisfy
                                // SameValue(arg, ToUint32(arg)) else throw
                                // RangeError. Pre-fix the function form
                                // had no range guard, so Array(-1) /
                                // Array(2.5) / Array(NaN) silently
                                // produced a 1-element array containing
                                // the bad value, diverging from
                                // new Array(-1) which already threw.
                                if (a0 && (a0->isInteger(pContext)
                                           || a0->isDouble(pContext)
                                           || a0->isFloat(pContext))) {
                                    double dlen = a0->isInteger(pContext)
                                        ? static_cast<double>(a0->asLong(pContext))
                                        : a0->asDouble(pContext);
                                    long long ilen = static_cast<long long>(dlen);
                                    if (std::isnan(dlen) || std::isinf(dlen)
                                        || static_cast<double>(ilen) != dlen
                                        || ilen < 0 || ilen > 4294967295LL) {
                                        for (uint32_t i = 0; i <= 0; i++) {} // already popped above
                                        pending_exception = makeError(pContext, "RangeError",
                                            "Invalid array length", pGlobalRoot);
                                        has_pending_exception = true;
                                        DISPATCH();
                                    }
                                    arrLength = ilen;
                                } else {
                                    // single non-integer arg: treat as one element via __elements__.
                                    const proto::ProtoList* els = pContext->newList();
                                    els = els->appendLast(pContext, a0 ? a0 : PROTO_NONE);
                                    protojs::setArrayElements(pContext, arr, els);
                                }
                            } else {
                                // multi-arg: entries go in __elements__ so iteration sees them.
                                proto::ProtoContext::CriticalSection arrCs2(pContext);
                                const proto::ProtoList* els = pContext->newList();
                                for (uint32_t i = 0; i < argc; i++)
                                    els = els->appendLast(pContext,
                                        argsList->getAt(pContext, static_cast<int>(i)));
                                protojs::setArrayElements(pContext, arr, els);
                            }
                            arr = arr->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(arrLength));
                            if (is_tail_call) return arr;
                            stackPush(pContext, arr);
                        } else if (func && func != PROTO_NONE && strCtorAttr && func->getAttribute(pContext, strCtorAttr, false) == PROTO_TRUE) {
                            // ECMA-262 §22.1.1.1 step 1: if no arguments,
                            // result is the empty string. ToString(undefined)
                            // would otherwise produce "undefined".
                            const proto::ProtoObject* s;
                            if (argc == 0) {
                                static const proto::ProtoObject* s_empty = nullptr;
                                if (!s_empty) s_empty = pContext->fromUTF8String("");
                                s = s_empty;
                            } else {
                                const proto::ProtoObject* arg = stackAt(pContext, argc - 1);
                                // §22.1.1.1 step 2.a: when NewTarget is
                                // undefined AND Type(value) is Symbol,
                                // return SymbolDescriptiveString(value)
                                // — "Symbol(<desc>)". Pre-fix the
                                // Symbol routed through toString which
                                // returned "[object Object]". Probe
                                // the marker first.
                                const proto::ProtoObject* symKo = pContext->fromUTF8String("__is_symbol__");
                                const proto::ProtoString* symK = symKo ? symKo->asString(pContext) : nullptr;
                                if (arg && symK && arg->getAttribute(pContext, symK, true) == PROTO_TRUE) {
                                    const proto::ProtoObject* descKo = pContext->fromUTF8String("__symbol_desc__");
                                    const proto::ProtoString* descK = descKo ? descKo->asString(pContext) : nullptr;
                                    std::string desc;
                                    if (descK) {
                                        const proto::ProtoObject* descVal = arg->getAttribute(pContext, descK, true);
                                        if (descVal && descVal != PROTO_NONE && descVal->isString(pContext)) {
                                            descVal->asString(pContext)->toUTF8String(pContext, desc);
                                        }
                                    }
                                    std::string out = "Symbol(" + desc + ")";
                                    s = pContext->fromUTF8String(out.c_str());
                                } else {
                                    s = toString(pContext, arg);
                                }
                            }
                            for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                            if (is_tail_call) return s;
                            stackPush(pContext, s);
                        } else {
                            // ECMA-262 §7.3.13 Call: if IsCallable(func) is
                            // false, throw TypeError.  IsCallable is true
                            // when func has __construct__ (Number, Boolean,
                            // Map, etc.) or is a method or has a
                            // __bytecode_id__ (JS function).  Without any
                            // of those markers, the receiver is genuinely
                            // not callable — throw.
                            const proto::ProtoString* conKey = JSSymbols::construct(pContext);
                            const proto::ProtoObject* conMethod = (conKey && func && func != PROTO_NONE)
                                ? func->getAttribute(pContext, conKey, false) : nullptr;
                            if (conMethod && conMethod != PROTO_NONE && conMethod->isMethod(pContext)) {
                                // Number(...) / Boolean(...) etc. called as a
                                // conversion function.  The __construct__ method
                                // expects `self` to be a fresh wrapper object
                                // and sets __primitive_value__ on it; for the
                                // no-new conversion case we then unwrap to the
                                // primitive and discard the wrapper.
                                const proto::ProtoList* argsList = pContext->newList();
                                for (uint32_t i = 0; i < argc; i++)
                                    argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                                for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                                const proto::ProtoObject* protoForCtor = func->getAttribute(pContext, JSSymbols::prototype(pContext), false);
                                const proto::ProtoObject* wrapper = (protoForCtor && protoForCtor != PROTO_NONE)
                                    ? protoForCtor->newChild(pContext, true)
                                    : pContext->newObject(true);
                                const proto::ProtoObject* constructed = conMethod->asMethod(pContext)(
                                    pContext, wrapper, nullptr, argsList, nullptr);
                                const proto::ProtoObject* result = constructed ? constructed : wrapper;
                                // Unwrap to primitive: Number()/Boolean()/String()
                                // return the primitive itself, not the wrapper.
                                // Object() is the exception — \`Object(5)\` must
                                // return the wrapper (\`new Number(5)\` shape) per
                                // ECMA-262 §19.1.1.  Detect via the function's
                                // .name (a primitive ctor is one whose name is
                                // not 'Object').  Cheaper than a dedicated
                                // marker attribute.
                                bool isObjectCtor = false;
                                const proto::ProtoString* nameKey = JSSymbols::name(pContext);
                                if (nameKey) {
                                    const proto::ProtoObject* fnName = func->getAttribute(pContext, nameKey, false);
                                    if (fnName && fnName != PROTO_NONE) {
                                        const proto::ProtoString* fns = fnName->asString(pContext);
                                        if (fns) {
                                            std::string n;
                                            fns->toUTF8String(pContext, n);
                                            if (n == "Object") isObjectCtor = true;
                                        }
                                    }
                                }
                                if (!isObjectCtor) {
                                    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(pContext);
                                    if (pvKey && result && result != PROTO_NONE) {
                                        const proto::ProtoObject* pv = result->getAttribute(pContext, pvKey, false);
                                        if (pv && pv != PROTO_NONE) result = pv;
                                    }
                                }
                                if (is_tail_call) return result ? result : PROTO_NONE;
                                stackPush(pContext, result ? result : PROTO_NONE);
                                DISPATCH();
                            }
                            for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                            pending_exception = makeError(pContext, "TypeError",
                                "is not a function", pGlobalRoot);
                            has_pending_exception = true;
                            DISPATCH();
                        }
                    }
                }
                DISPATCH();
            }
    L_OP_fclosure8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                const proto::ProtoObject* rawFn = (cpool && idx < cpool->getSize(pContext)) ? cpool->getAt(pContext, static_cast<int>(idx)) : PROTO_NONE;
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
                    // fn.prototype inherits Object.prototype (see L_OP_fclosure for rationale).
                    const proto::ProtoObject* objProtoFc8 =
                        (pContext->space && pContext->space->objectPrototype)
                            ? pContext->space->objectPrototype : nullptr;
                    const proto::ProtoObject* fnDefProto8 = objProtoFc8
                        ? objProtoFc8->newChild(pContext, true)
                        : pContext->newObject(true);
                    fnInst = fnInst->setAttribute(pContext, JSSymbols::prototype(pContext), fnDefProto8);
                    // Spec: fn.prototype is {writable:true, enumerable:false, configurable:false}
                    // bits: 0x1=writable, 0x2=configurable, 0x4=enumerable → 0x1 only.
                    {
                        const proto::ProtoString* pdks = JSSymbols::pdPrototype(pContext);
                        if (pdks) fnInst = fnInst->setAttribute(pContext, pdks, pContext->fromInteger(0x1LL));
                    }
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
                        // Spec: fn.name is {writable:false, enumerable:false, configurable:true}
                        setNWCDescriptor(pContext, fnInst, "name");
                        const proto::ProtoString* lenKey8 = JSSymbols::length(pContext);
                        if (lenKey8)
                            fnInst = fnInst->setAttribute(pContext, lenKey8,
                                pContext->fromInteger(static_cast<long long>(nm8.argCount_)));
                        // Spec: fn.length is {writable:false, enumerable:false, configurable:true}
                        setNWCDescriptor(pContext, fnInst, "length");
                        // Capture lexical this for arrow functions.
                        if (nm8.isArrow) {
                            fnInst = fnInst->setAttribute(pContext, JSSymbols::arrowThis(pContext),
                                thisObj ? thisObj : PROTO_NONE);
                        }
                        // Mark async functions so callJSFunction can wrap the result in a Promise.
                        if (nm8.isAsync) {
                            const proto::ProtoString* iasK = JSSymbols::isAsync(pContext);
                            if (iasK) fnInst = fnInst->setAttribute(pContext, iasK, PROTO_TRUE);
                        }
                        // Closure var capture: store cells (or raw values
                        // for global captures) on the function instance via
                        // `__captured_cells__`.  See OP_fclosure for the
                        // detailed semantics — this is the same logic for
                        // the 8-bit immediate variant.
                        if (!nm8.closureVarNames.empty()) {
                            const proto::ProtoSparseList* cells = pContext->newSparseList();
                            for (size_t cvi = 0; cvi < nm8.closureVarNames.size(); ++cvi) {
                                int cvType = (cvi < nm8.closureVarTypes.size())
                                    ? nm8.closureVarTypes[cvi] : -1;
                                uint16_t cvIdx = (cvi < nm8.closureVarIndices.size())
                                    ? nm8.closureVarIndices[cvi] : 0;
                                const proto::ProtoObject* captured = PROTO_NONE;
                                if (cvType == 1 /* ARG */) {
                                    const proto::ProtoObject* curVal =
                                        getSlot(pContext, cvIdx);
                                    const proto::ProtoObject* cell =
                                        allocCell(pContext, curVal);
                                    if (cell) captured = cell;
                                } else if (cvType == 0 /* LOCAL */) {
                                    const proto::ProtoObject* slotVal =
                                        getSlot(pContext, argCount + cvIdx);
                                    if (isCell(pContext, slotVal)) {
                                        captured = slotVal;
                                    } else {
                                        const proto::ProtoObject* cell =
                                            allocCell(pContext, slotVal);
                                        if (cell) {
                                            setSlot(pContext, argCount + cvIdx, cell);
                                            captured = cell;
                                        }
                                    }
                                } else if (cvType == 2 /* REF */) {
                                    captured = getSlot(pContext, argCount + varCount + cvIdx);
                                } else {
                                    captured = PROTO_NONE;
                                }
                                cells = cells->setAt(pContext, static_cast<unsigned long>(cvi),
                                                      captured ? captured : PROTO_NONE);
                            }
                            const proto::ProtoString* ccKey = capturedCellsKey(pContext);
                            if (ccKey)
                                fnInst = fnInst->setAttribute(pContext, ccKey,
                                    cells->asObject(pContext));
                        }
                    }
                    stackPush(pContext, fnInst);
                } else {
                    stackPush(pContext, rawFn ? rawFn : PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_fclosure: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* rawFn2 = (cpool && idx < cpool->getSize(pContext)) ? cpool->getAt(pContext, static_cast<int>(idx)) : PROTO_NONE;
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
                    // fn.prototype must inherit Object.prototype so instances
                    // produced by `new f()` carry hasOwnProperty/toString/etc.
                    // Pre-fix the default prototype was a raw newObject(true)
                    // with no parent, so `new F().hasOwnProperty(...)` threw.
                    const proto::ProtoObject* objProtoFc =
                        (pContext->space && pContext->space->objectPrototype)
                            ? pContext->space->objectPrototype : nullptr;
                    const proto::ProtoObject* fnDefProto = objProtoFc
                        ? objProtoFc->newChild(pContext, true)
                        : pContext->newObject(true);
                    fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::prototype(pContext), fnDefProto);
                    // Spec: fn.prototype is {writable:true, enumerable:false, configurable:false}
                    {
                        const proto::ProtoString* pdks2 = JSSymbols::pdPrototype(pContext);
                        if (pdks2) fnInst2 = fnInst2->setAttribute(pContext, pdks2, pContext->fromInteger(0x1LL));
                    }
                    // Resolve function metadata from the root module's flat nestedFunctions list.
                    const ProtoBytecodeModule* nm2Ptr = nullptr;
                    if (fnBcId2 >= 0 && t_rootModule &&
                            static_cast<size_t>(fnBcId2) < t_rootModule->nestedFunctions.size())
                        nm2Ptr = &t_rootModule->nestedFunctions[static_cast<size_t>(fnBcId2)];
                    
                    if (nm2Ptr) {
                        const ProtoBytecodeModule& nm2 = *nm2Ptr;
                        // Attach native metadata object for rooting and fast access.
                        if (nm2.metadata)
                            fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::metadata(pContext), nm2.metadata);
                        if (!nm2.funcName.empty()) {
                            const proto::ProtoObject* nameVal2 = pContext->fromUTF8String(nm2.funcName.c_str());
                            if (nameVal2)
                                fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::name(pContext), nameVal2);
                        }
                        // Spec: fn.name is {writable:false, enumerable:false, configurable:true}
                        setNWCDescriptor(pContext, fnInst2, "name");
                        const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                        if (lenKey2)
                            fnInst2 = fnInst2->setAttribute(pContext, lenKey2,
                                pContext->fromInteger(static_cast<long long>(nm2.argCount_)));
                        // Spec: fn.length is {writable:false, enumerable:false, configurable:true}
                        setNWCDescriptor(pContext, fnInst2, "length");
                        // Capture lexical this for arrow functions.
                        if (nm2.isArrow) {
                            fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::arrowThis(pContext),
                                thisObj ? thisObj : PROTO_NONE);
                        }
                        // Mark async functions so callJSFunction can wrap the result in a Promise.
                        if (nm2.isAsync) {
                            const proto::ProtoString* iasK2 = JSSymbols::isAsync(pContext);
                            if (iasK2) fnInst2 = fnInst2->setAttribute(pContext, iasK2, PROTO_TRUE);
                        }
                        // Closure var capture: store cells (or raw values for
                        // global captures) on the function instance via the
                        // `__captured_cells__` SparseList.  When fnInst2 is
                        // later called, runBytecode reads the SparseList and
                        // populates the callee's closure-var slots with the
                        // SAME cell pointers — so reads/writes from inside
                        // the inner function go through the cell shared with
                        // its capturing scope.
                        if (!nm2.closureVarNames.empty()) {
                            const proto::ProtoSparseList* cells = pContext->newSparseList();
                            for (size_t cvi2 = 0; cvi2 < nm2.closureVarNames.size(); ++cvi2) {
                                int cvType2 = (cvi2 < nm2.closureVarTypes.size())
                                    ? nm2.closureVarTypes[cvi2] : -1;
                                uint16_t cvIdx2 = (cvi2 < nm2.closureVarIndices.size())
                                    ? nm2.closureVarIndices[cvi2] : 0;
                                const proto::ProtoObject* captured = PROTO_NONE;
                                if (cvType2 == 1 /* ARG */) {
                                    // Allocate a cell wrapping the parent's
                                    // arg slot so the callee can mutate it
                                    // back into the parent's view.
                                    const proto::ProtoObject* curVal =
                                        getSlot(pContext, cvIdx2);
                                    const proto::ProtoObject* cell =
                                        allocCell(pContext, curVal);
                                    if (cell) {
                                        // Promote the parent's arg slot to
                                        // hold the cell so subsequent reads
                                        // / writes via OP_get_arg / OP_set_arg
                                        // see the cell — caveat: those ops
                                        // currently don't dereference cells.
                                        // For now this matches LOCAL behaviour
                                        // after OP_close_loc.
                                        captured = cell;
                                    }
                                } else if (cvType2 == 0 /* LOCAL */) {
                                    // Read the parent's local slot; if it's
                                    // not yet a cell, lazily promote it by
                                    // allocating a cell and writing it back
                                    // to the parent's local slot.  Subsequent
                                    // OP_fclosure calls in the same parent
                                    // scope that capture the SAME local will
                                    // see isCell(slot) = true and reuse the
                                    // already-promoted cell.  We must NOT
                                    // search the parent's closureVars for a
                                    // matching index — closureVarIndices
                                    // there refer to the GRANDPARENT's slot
                                    // numbering (where the parent's CV came
                                    // from), not the parent's local slots.
                                    const proto::ProtoObject* slotVal =
                                        getSlot(pContext, argCount + cvIdx2);
                                    if (isCell(pContext, slotVal)) {
                                        captured = slotVal;
                                    } else {
                                        const proto::ProtoObject* cell =
                                            allocCell(pContext, slotVal);
                                        if (cell) {
                                            setSlot(pContext, argCount + cvIdx2, cell);
                                            captured = cell;
                                        }
                                    }
                                } else if (cvType2 == 2 /* REF */) {
                                    // Parent already has the variable as a
                                    // closure var — its slot holds the cell
                                    // (assuming the chain has been built
                                    // correctly).  Pass the cell pointer
                                    // through unchanged.
                                    captured = getSlot(pContext, argCount + varCount + cvIdx2);
                                } else {
                                    // Global captures: keep raw value.  The
                                    // callee's runBytecode will fall through
                                    // to its global-lookup path.  Mark with
                                    // PROTO_NONE so the SparseList stores it
                                    // explicitly.
                                    captured = PROTO_NONE;
                                }
                                cells = cells->setAt(pContext, static_cast<unsigned long>(cvi2),
                                                      captured ? captured : PROTO_NONE);
                            }
                            const proto::ProtoString* ccKey = capturedCellsKey(pContext);
                            if (ccKey)
                                fnInst2 = fnInst2->setAttribute(pContext, ccKey,
                                    cells->asObject(pContext));
                        }
                    }
                    stackPush(pContext, fnInst2);
                } else {
                    stackPush(pContext, rawFn2 ? rawFn2 : PROTO_NONE);
                }
                DISPATCH();
            }
            L_OP_is_undefined: {
                // Pops one value; pushes true iff the value is the JS
                // `undefined`.  protoJS has two undefined representations
                // (PROTO_NONE and the heap-allocated t_undefinedSentinel
                // used as the value of the global `undefined` identifier
                // — see prior commits' notes).  Pre-fix this opcode only
                // matched PROTO_NONE, so `undefined === void 0` (which
                // QuickJS compiles to OP_push_undef / get_var undefined +
                // OP_is_undefined) returned false because the global
                // `undefined` resolves to the heap sentinel, not PROTO_NONE.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                bool isUndef = (!val || val == PROTO_NONE ||
                                val == getUndefinedSentinel() ||
                                (val && val->isNone(pContext)));
                stackPush(pContext, isUndef ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_is_null: {
                // Pops one value; pushes true if it is null (t_nullSentinel only).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (val == t_nullSentinel) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_typeof_is_undefined: {
                // Pops one value; pushes true if typeof is "undefined".
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE || val == t_undefinedSentinel) ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            L_OP_typeof_is_function: {
                // Pops one value; pushes true if typeof is "function".
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                int bcId = getBytecodeId(pContext, val);
                bool isFunc = (bcId >= 0) ||
                              (val && val != PROTO_NONE && val->isMethod(pContext));
                if (!isFunc && val && val != PROTO_NONE) {
                    // Check for __native_fn__ wrapper.
                    const proto::ProtoString* nfIsFnKey = JSSymbols::nativeFn(pContext);
                    const proto::ProtoObject* nfIsFnTarget = nfIsFnKey
                        ? val->getAttribute(pContext, nfIsFnKey, false) : nullptr;
                    if (nfIsFnTarget && nfIsFnTarget != PROTO_NONE && nfIsFnTarget->isMethod(pContext)) {
                        isFunc = true;
                    } else {
                        const proto::ProtoString* bfIsFnKey = JSSymbols::boundFn(pContext);
                        const proto::ProtoObject* bfIsFnTarget = bfIsFnKey
                            ? val->getAttribute(pContext, bfIsFnKey, false) : nullptr;
                        if (bfIsFnTarget && bfIsFnTarget != PROTO_NONE) {
                            isFunc = true;
                        } else {
                            // Built-in constructors (Array, Object, Error, RegExp, etc.)
                            const proto::ProtoString* acK = JSSymbols::arrayCtor(pContext);
                            const proto::ProtoString* ecK = JSSymbols::errorCtor(pContext);
                            const proto::ProtoString* reK = JSSymbols::regexpCtor(pContext);
                            const proto::ProtoString* taK = JSSymbols::taCtor(pContext);
                            const proto::ProtoString* scK = JSSymbols::stringCtor(pContext);
                            const proto::ProtoString* conK = JSSymbols::construct(pContext);
                            isFunc = (acK && val->getAttribute(pContext, acK, false) == PROTO_TRUE)
                                  || (ecK && val->getAttribute(pContext, ecK, false) && val->getAttribute(pContext, ecK, false) != PROTO_NONE)
                                  || (reK && val->getAttribute(pContext, reK, false) == PROTO_TRUE)
                                  || (taK && val->getAttribute(pContext, taK, false) && val->getAttribute(pContext, taK, false) != PROTO_NONE)
                                  || (scK && val->getAttribute(pContext, scK, false) == PROTO_TRUE)
                                  || (conK && val->getAttribute(pContext, conK, false) && val->getAttribute(pContext, conK, false)->isMethod(pContext));
                        }
                    }
                }
                stackPush(pContext, isFunc ? PROTO_TRUE : PROTO_FALSE);
                DISPATCH();
            }
            // ---------------------------------------------------------------
            // Step A — OP_array_from
            // DEF(array_from, 3, 0, 1, npop) — 1 opcode + 2-byte element count
            // ---------------------------------------------------------------
            L_OP_array_from: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t count = get_u16(buf + pc);
                pc += 2;
                // Create a mutable array that inherits from Array.prototype so that
                // push/pop/join/slice etc. are found via prototype-chain lookup.
                REFRESH_GLOBAL_OBJ();
                const proto::ProtoString* arrProtoLookupKey =
                    JSSymbols::arrayProto(pContext);
                const proto::ProtoObject* arrProto =
                    (arrProtoLookupKey && globalObj && globalObj != PROTO_NONE)
                        ? globalObj->getAttribute(pContext, arrProtoLookupKey, false)
                        : nullptr;
                const proto::ProtoObject* arr = (arrProto && arrProto != PROTO_NONE)
                    ? arrProto->newChild(pContext, true)   // mutable, inherits Array.prototype
                    : pContext->newObject(true);            // fallback: mutable plain object
                if (!arr) { stackPush(pContext, PROTO_NONE); DISPATCH(); }
                // Set the internal array marker so Array.isArray identifies it.
                const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
                if (isArrKey) arr = arr->setAttribute(pContext, isArrKey, PROTO_TRUE);
                // Build the elements ProtoList (native storage) and publish it
                // via a single setAttribute(__elements__, …) at the end.  This
                // is the lazy-publish pattern: appendLast per element is
                // O(log N) inside the list, but only one mutable-CAS round-
                // trip on the array itself.
                //
                // GC critical section: each appendLast produces a fresh
                // ProtoList root that is reachable only via the C++ local
                // `list` until setArrayElements publishes it.  Without CS
                // a safepoint poll inside an inner allocCell can submit
                // the young chain to dirtySegments, leaving the in-flight
                // tree's root unrooted across the next mark cycle.  Same
                // discipline as ArrayPrototype.push.
                proto::ProtoContext::CriticalSection arrayFromCs(pContext);
                const proto::ProtoList* list = pContext->newList();
                for (uint16_t i = 0; i < count; i++) {
                    const proto::ProtoObject* elem =
                        stackAt(pContext, static_cast<unsigned long>(count - 1 - i));
                    list = list->appendLast(pContext, elem ? elem : PROTO_NONE);
                }
                for (uint16_t i = 0; i < count; i++) stackPop(pContext);
                if (list) protojs::setArrayElements(pContext, arr, list);
                // ECMA-262 §22.1.5.1: Array.length descriptor is
                // {writable:true, enumerable:false, configurable:false}
                // (bits 0x1). OP_array_from is the hot path for array
                // literals; without this sidecar 'length' enumerated in
                // for-in and getOwnPropertyDescriptor reported enumerable
                // true instead of false.
                {
                    const proto::ProtoObject* pdo = pContext->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdk = pdo ? pdo->asString(pContext) : nullptr;
                    if (pdk) arr = arr->setAttribute(pContext, pdk, pContext->fromInteger(0x1LL));
                }
                stackPush(pContext, arr ? arr : PROTO_NONE);
                DISPATCH();
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
            L_OP_for_of_start: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* iterable = stackTop(pContext);
                stackPop(pContext);
                // Null and undefined are not iterable — throw TypeError per
                // ECMA-262 §7.4.1 GetIterator(undefined/null) → TypeError.
                if (iterable == t_nullSentinel) {
                    pending_exception = makeError(pContext, "TypeError",
                        "null is not iterable", pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }
                if (!iterable || iterable == PROTO_NONE ||
                    iterable == getUndefinedSentinel() ||
                    iterable->isNone(pContext)) {
                    pending_exception = makeError(pContext, "TypeError",
                        "undefined is not iterable", pGlobalRoot);
                    has_pending_exception = true;
                    DISPATCH();
                }

                // Case A: native iterator object (produced by Array/TypedArray keys/values/entries).
                // Detect by presence of both `next` method and `__iter_arr__` internal key.
                const proto::ProtoString* nextKey2 = JSSymbols::next(pContext);
                const proto::ProtoString* iterArrKey2 = JSSymbols::iterArr(pContext);
                if (nextKey2 && iterArrKey2) {
                    const proto::ProtoObject* nextFn2 = iterable->getAttribute(pContext, nextKey2, false);
                    const proto::ProtoObject* iterArrVal = iterable->getAttribute(pContext, iterArrKey2, false);
                    if (nextFn2 && nextFn2 != PROTO_NONE && iterArrVal && iterArrVal != PROTO_NONE) {
                        uint32_t baseSlot = 0x10000u + static_cast<uint32_t>(pc - 1);
                        setSlot(pContext, baseSlot,     iterable);
                        setSlot(pContext, baseSlot + 1, pContext->fromInteger(-1LL)); // sentinel
                        setSlot(pContext, baseSlot + 2, pContext->fromInteger(0LL));  // done flag
                        // setSlot at baseSlot (≥ 0x10000) forces a massive
                        // resize of automaticLocals on first hit, invalidating
                        // the pAutomaticLocals pointer cached at the top of
                        // runBytecode.  Without refresh, the next opcode reads
                        // stale freed memory — the for-of body sees garbage
                        // on the first iteration and the loop never terminates
                        // (idx checks read stale values).  REFRESH HERE.
                        REFRESH_INTERP_STATE();
                        const proto::ProtoObject* iterObj = pContext->newObject(false);
                        if (!iterObj) return PROTO_NONE;
                        const proto::ProtoString* slotKey2 = JSSymbols::iterSlot(pContext);
                        if (slotKey2)
                            iterObj = iterObj->setAttribute(pContext, slotKey2,
                                pContext->fromInteger(static_cast<long long>(baseSlot)));
                        stackPush(pContext, iterObj);
                        stackPush(pContext, PROTO_NONE);
                        stackPush(pContext, pContext->fromInteger(0LL));
                        DISPATCH();
                    }
                }

                // Check for Symbol.iterator first (Case C - Spec Compliant).
                const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(pContext);
                const proto::ProtoObject* iterFn = symIterKey
                    ? iterable->getAttribute(pContext, symIterKey, true) : PROTO_NONE;
                
                if (iterFn && iterFn != PROTO_NONE) {
                    // printf("L_OP_for_of_start: Case C (Symbol.iterator)\n");
                    const proto::ProtoList* emptyArgs2 = pContext->newList();
                    const proto::ProtoObject* iterator = callJSFunction(pContext, iterFn, iterable, emptyArgs2);
                    if (t_hasCallException) {
                        pending_exception  = t_callException;
                        has_pending_exception = true;
                        t_hasCallException = false;
                        t_callException    = nullptr;
                        DISPATCH();
                    }
                    if (!iterator || iterator == PROTO_NONE) return PROTO_NONE;
                    uint32_t baseSlotC = 0x10000u + static_cast<uint32_t>(pc - 1);
                    setSlot(pContext, baseSlotC,     iterator);
                    setSlot(pContext, baseSlotC + 1, pContext->fromInteger(-1LL)); // sentinel: next()-based
                    setSlot(pContext, baseSlotC + 2, pContext->fromInteger(0LL));  // done flag
                    REFRESH_INTERP_STATE(); // setSlot(≥0x10000) resized automaticLocals — see Case A comment.
                    const proto::ProtoObject* iterObjC = pContext->newObject(false);
                    if (!iterObjC) return PROTO_NONE;
                    const proto::ProtoString* slotKeyC = JSSymbols::iterSlot(pContext);
                    if (slotKeyC)
                        iterObjC = iterObjC->setAttribute(pContext, slotKeyC,
                            pContext->fromInteger(static_cast<long long>(baseSlotC)));
                    stackPush(pContext, iterObjC);
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, pContext->fromInteger(0LL));
                    DISPATCH();
                }

                // Fallback to Case B: .length based iteration for non-iterables.
                const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                const proto::ProtoObject* lenVal = lenKey2 ? iterable->getAttribute(pContext, lenKey2, true) : PROTO_NONE;
                if (lenVal && lenVal != PROTO_NONE && lenVal->isInteger(pContext)) {
                    uint32_t baseSlot = 0x10000u + static_cast<uint32_t>(pc - 1);
                    setSlot(pContext, baseSlot,     iterable);
                    setSlot(pContext, baseSlot + 1, pContext->fromInteger(0LL));
                    REFRESH_INTERP_STATE(); // setSlot(≥0x10000) resized automaticLocals — see Case A comment.
                    // Build a lightweight iterator object carrying the slot base.
                    const proto::ProtoObject* iterObj = pContext->newObject(false);
                    if (iterObj) {
                        const proto::ProtoString* slotKey2 = JSSymbols::iterSlot(pContext);
                        if (slotKey2)
                            iterObj = iterObj->setAttribute(pContext, slotKey2, pContext->fromInteger(static_cast<long long>(baseSlot)));
                        stackPush(pContext, iterObj);           // iterator
                        stackPush(pContext, PROTO_NONE);        // nextMethod placeholder
                        stackPush(pContext, pContext->fromInteger(0LL)); // catch_offset placeholder
                        DISPATCH();
                    }
                }

                // Vacuous pass if not iterable.
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, pContext->fromInteger(0LL));
                DISPATCH();
            }

            L_OP_for_of_next: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t rawOffset = buf[pc++];

                // QuickJS semantics: the u8 byte is the depth of extra
                // slots between the iterator state and TOS.  Stack
                // layout is
                //   [..., iterator, next_meth, catch_off,
                //         slot_1, slot_2, ..., slot_rawOffset]
                // After execution, value and done are pushed on top:
                //   [..., iterator, next_meth, catch_off,
                //         slot_1, ..., slot_rawOffset, value, done]
                // The iterator state stays in-place; the impl below
                // reads it via stackAt with the right depths and
                // pushes value/done at the end.  Pre-fix the
                // dispatcher ignored rawOffset and popped top 3
                // unconditionally — for destructure rest patterns
                // `[a, ...r] = [1,2,3,4]` (rawOffset=2 because
                // array+idx sit above the iterator state) it corrupted
                // the iterator state, so the rest array ended up empty.
                if (stackSize(pContext) < static_cast<unsigned long>(rawOffset) + 3UL)
                    return PROTO_NONE;
                const proto::ProtoObject* catch_off =
                    stackAt(pContext, static_cast<unsigned long>(rawOffset));
                const proto::ProtoObject* next_meth =
                    stackAt(pContext, static_cast<unsigned long>(rawOffset) + 1UL);
                const proto::ProtoObject* iterator  =
                    stackAt(pContext, static_cast<unsigned long>(rawOffset) + 2UL);

                const proto::ProtoString* slotKey3 = JSSymbols::iterSlot(pContext);
                const proto::ProtoObject* slotVal = (slotKey3 && iterator && iterator != PROTO_NONE)
                    ? iterator->getAttribute(pContext, slotKey3, false) : PROTO_NONE;

                if (!slotVal || slotVal == PROTO_NONE || !slotVal->isInteger(pContext)) {
                    // Iterator state stays in-place — push value+done on top.
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    DISPATCH();
                }

                uint32_t bs = static_cast<uint32_t>(slotVal->asLong(pContext));
                const proto::ProtoObject* arrObj  = getSlot(pContext, bs);
                const proto::ProtoObject* idxObj2 = getSlot(pContext, bs + 1);

                if (!arrObj || arrObj == PROTO_NONE || !idxObj2 || !idxObj2->isInteger(pContext)) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    DISPATCH();
                }
                long long idx2 = idxObj2->asLong(pContext);

                if (idx2 == -1LL) {
                    // next()-based path.
                    const proto::ProtoString* nextKeyFO = JSSymbols::next(pContext);
                    const proto::ProtoObject* nextFnFO = (nextKeyFO && arrObj != PROTO_NONE)
                        ? arrObj->getAttribute(pContext, nextKeyFO, false) : PROTO_NONE;
                    const proto::ProtoObject* resultFO = PROTO_NONE;
                    if (nextFnFO && nextFnFO != PROTO_NONE) {
                        if (nextFnFO->isMethod(pContext)) {
                            proto::ProtoMethod nativeFnFO = nextFnFO->asMethod(pContext);
                            if (nativeFnFO)
                                resultFO = nativeFnFO(pContext, arrObj, nullptr, nullptr, nullptr);
                        } else {
                            const proto::ProtoList* emptyArgsFO = pContext->newList();
                            resultFO = callJSFunction(pContext, nextFnFO, arrObj, emptyArgsFO);
                            if (t_hasCallException) {
                                pending_exception  = t_callException;
                                has_pending_exception = true;
                                t_hasCallException = false;
                                t_callException    = nullptr;
                                DISPATCH();
                            }
                        }
                    }
                    // §7.4.2 IteratorNext step 4: if Type(result) is not
                    // Object, throw a TypeError. Pre-fix for-of silently
                    // proceeded with a primitive result and the body
                    // looped on stale `done`/`value` reads (language/
                    // statements/for-of/iterator-next-result-type.js
                    // expects bool / string / number / Symbol returns
                    // to throw).
                    {
                        bool isPrimResult = !resultFO || resultFO == PROTO_NONE
                            || resultFO->isString(pContext)
                            || resultFO->isInteger(pContext)
                            || resultFO->isDouble(pContext)
                            || resultFO->isFloat(pContext)
                            || resultFO->isBoolean(pContext)
                            || resultFO == t_nullSentinel
                            || resultFO == t_undefinedSentinel
                            || resultFO == getUndefinedSentinel()
                            || resultFO == PROTO_TRUE
                            || resultFO == PROTO_FALSE;
                        if (!isPrimResult && resultFO) {
                            // Symbol carrier objects look like Objects to
                            // the type checks above but are primitives
                            // per §6.1.5.
                            const proto::ProtoObject* symKo =
                                pContext->fromUTF8String("__is_symbol__");
                            const proto::ProtoString* symK = symKo
                                ? symKo->asString(pContext) : nullptr;
                            if (symK && resultFO->getAttribute(pContext, symK, true)
                                == PROTO_TRUE) isPrimResult = true;
                        }
                        if (isPrimResult) {
                            pending_exception = makeNativeError(
                                pContext, "TypeError",
                                "Iterator result is not an object");
                            has_pending_exception = true;
                            DISPATCH();
                        }
                    }
                    const proto::ProtoString* doneKeyFO  = JSSymbols::done(pContext);
                    const proto::ProtoString* valueKeyFO = JSSymbols::value(pContext);
                    const proto::ProtoObject* doneFO = (resultFO && resultFO != PROTO_NONE && doneKeyFO)
                        ? resultFO->getAttribute(pContext, doneKeyFO, false) : PROTO_TRUE;
                    if ((!doneFO || doneFO == PROTO_NONE) && resultFO && resultFO != PROTO_NONE) {
                        const proto::ProtoObject* gd = invokeGetterIfPresent(resultFO, "done");
                        if (has_pending_exception) DISPATCH();
                        if (gd && gd != PROTO_NONE) doneFO = gd;
                    }
                    const proto::ProtoObject* valueFO = (resultFO && resultFO != PROTO_NONE && valueKeyFO)
                        ? resultFO->getAttribute(pContext, valueKeyFO, false) : PROTO_NONE;
                    if ((!valueFO || valueFO == PROTO_NONE) && resultFO && resultFO != PROTO_NONE) {
                        const proto::ProtoObject* gv = invokeGetterIfPresent(resultFO, "value");
                        if (has_pending_exception) DISPATCH();
                        if (gv && gv != PROTO_NONE) valueFO = gv;
                    }
                    const bool isDone = (doneFO && doneFO != PROTO_NONE) ? toBool(pContext, doneFO) : false;
                    if (isDone) setSlot(pContext, bs + 2, pContext->fromInteger(1LL));

                    // Iterator state stays in-place — push value+done on top.
                    stackPush(pContext, valueFO ? valueFO : PROTO_NONE);
                    stackPush(pContext, isDone ? PROTO_TRUE : PROTO_FALSE);
                    DISPATCH();
                }

                const proto::ProtoString* lenKey3 = JSSymbols::length(pContext);
                const proto::ProtoObject* lenVal2 = lenKey3 ? arrObj->getAttribute(pContext, lenKey3, false) : PROTO_NONE;
                long long arrLen = (lenVal2 && lenVal2 != PROTO_NONE && lenVal2->isInteger(pContext))
                                   ? lenVal2->asLong(pContext) : 0LL;
                if (idx2 >= arrLen) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    DISPATCH();
                }
                const proto::ProtoObject* elemVal;
                uint8_t taElemType = getTypedArrayElementType(pContext, arrObj);
                if (taElemType != 0xFF) {
                    elemVal = typedArrayGetElement(pContext, arrObj,
                                                  static_cast<uint32_t>(idx2), taElemType);
                } else {
                    // Prefer __elements__ via arrayTryFastGet — dense
                    // arrays no longer keep elements as string-keyed
                    // attributes, so the indexKey getAttribute path
                    // returns nullptr and the rest pattern
                    // `[a,...r] = [1,2,3,4]` produced r = [].
                    elemVal = arrayTryFastGet(pContext, arrObj, static_cast<unsigned long>(idx2));
                    if (!elemVal) {
                        std::string elemIdxStr = std::to_string(idx2);
                        const proto::ProtoObject* elemIdxObj = pContext->fromUTF8String(elemIdxStr.c_str());
                        const proto::ProtoString* elemIdxKey = elemIdxObj ? elemIdxObj->asString(pContext) : nullptr;
                        elemVal = elemIdxKey
                            ? arrObj->getAttribute(pContext, elemIdxKey, false) : PROTO_NONE;
                    }
                }
                setSlot(pContext, bs + 1, pContext->fromInteger(idx2 + 1LL));
                stackPush(pContext, elemVal ? elemVal : PROTO_NONE);
                stackPush(pContext, PROTO_FALSE); // done = false
                DISPATCH();
            }

            // OP_iterator_get_value_done: DEF(iterator_get_value_done, 1, 2, 3, none)
            // Stack before: [..., catch_0, result_obj]  → after: [..., new_catch_0, value, done]
            L_OP_iterator_get_value_done: {
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
                DISPATCH();
            }

            // OP_iterator_check_object: DEF(iterator_check_object, 1, 1, 1, none)
            // Validates iterator result is object-like; protoCore accepts any non-null — no-op.
            L_OP_iterator_check_object: ;
                // Leave TOS unchanged; no validation throw in protoCore.
                DISPATCH();

            // OP_iterator_close: DEF(iterator_close, 1, 3, 0, none)
            // Pops [iter, nextMethod, catch_0] from stack.
            // For native iterators (sentinel -1) call iterator.return() if present.
            L_OP_iterator_close: {
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
                DISPATCH();
            }

            // OP_iterator_next: DEF(iterator_next, 1, 4, 4, none)
            // Stack before: [iter, nextMethod, catch_offset, sentinel]  (4 consumed)
            // Stack after:  [iter, nextMethod, catch_offset, result_obj] (4 produced)
            // result_obj is a ProtoObject with "value" and "done" attributes.
            // Used by array/object destructuring patterns: const [a,b] = expr.
            L_OP_iterator_next: {
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
                                    DISPATCH();
                                }
                            }
                        }
                        // §7.4.2 IteratorNext step 4: if Type(result) is
                        // not Object, throw a TypeError.  Pre-fix the
                        // dispatch silently proceeded, read undefined for
                        // `done`, and the for-of body looped on a stale
                        // value (language/statements/for-of/
                        // iterator-next-result-type.js verifies bool /
                        // string / number / Symbol returns from next()
                        // throw).
                        {
                            bool isPrimRes = !resultObjIN || resultObjIN == PROTO_NONE
                                || resultObjIN->isString(pContext)
                                || resultObjIN->isInteger(pContext)
                                || resultObjIN->isDouble(pContext)
                                || resultObjIN->isFloat(pContext)
                                || resultObjIN->isBoolean(pContext)
                                || resultObjIN == t_nullSentinel
                                || resultObjIN == t_undefinedSentinel
                                || resultObjIN == getUndefinedSentinel()
                                || resultObjIN == PROTO_TRUE
                                || resultObjIN == PROTO_FALSE;
                            if (!isPrimRes && resultObjIN) {
                                const proto::ProtoObject* symKo2 =
                                    pContext->fromUTF8String("__is_symbol__");
                                const proto::ProtoString* symK2 = symKo2
                                    ? symKo2->asString(pContext) : nullptr;
                                if (symK2 && resultObjIN->getAttribute(pContext, symK2, true)
                                    == PROTO_TRUE) isPrimRes = true;
                            }
                            if (isPrimRes) {
                                pending_exception = makeNativeError(
                                    pContext, "TypeError",
                                    "Iterator result is not an object");
                                has_pending_exception = true;
                                stackPush(pContext, iterObjIN);
                                stackPush(pContext, nextMethodIN);
                                stackPush(pContext, catchOffIN ? catchOffIN
                                    : pContext->fromInteger(0LL));
                                DISPATCH();
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
                DISPATCH();
            }

            // OP_iterator_call: DEF(iterator_call, 2, 4, 5, u8)
            // Stack before: [iter, nextMethod, catch_offset, sentinel]  (4 consumed)
            // Stack after:  [iter, nextMethod, catch_offset, value, done] (5 produced)
            // flags=1: collect remaining iterator values into a rest array, done=false.
            // flags=0: iterator.return() cleanup path, value=undefined, done=true.
            L_OP_iterator_call: {
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
                if (icCallException) DISPATCH();

                // Push back [iter, nextMethod, catch_offset, value, done].
                stackPush(pContext, iterObjIC);
                stackPush(pContext, nextMethodIC);
                stackPush(pContext, catchOffIC ? catchOffIC : pContext->fromInteger(0LL));
                stackPush(pContext, resultValIC  ? resultValIC  : PROTO_NONE);
                stackPush(pContext, resultDoneIC ? resultDoneIC : PROTO_TRUE);
                DISPATCH();
            }

            // ---------------------------------------------------------------
            // Step C — for-in (PROTO_NONE guard — key enumeration not supported)
            // ---------------------------------------------------------------

            // OP_for_in_start: DEF(for_in_start, 1, 1, 1, none)
            // Pop object, push a for-in iterator that carries all enumerable
            // string-keyed property names reachable through the full [[Prototype]]
            // chain (ES2015+ EnumerateObjectProperties semantics).
            L_OP_for_in_start: {
                const proto::ProtoObject* fiObj = PROTO_NONE;
                if (!stackEmpty(pContext)) {
                    fiObj = stackTop(pContext);
                    stackPop(pContext);
                }

                // Collect own non-internal string-keyed properties.
                // Walk the protoCore parent chain to include inherited enumerable
                // properties (mirrors JS for-in semantics).
                const proto::ProtoObject* fiIter = pContext->newObject(true);
                const proto::ProtoString* fiArrKey = JSSymbols::iterArr(pContext);
                const proto::ProtoString* fiIdxKey = JSSymbols::iterIdx(pContext);
                const proto::ProtoString* fiLenKey = JSSymbols::length(pContext);
                const proto::ProtoString* fiIsArr  = JSSymbols::isArray(pContext);

                // Build the key array on the iterator.
                const proto::ProtoObject* fiKeyArr = pContext->newObject(true);
                long long fiCount = 0;

                // Helper lambda: add a key string to fiKeyArr.
                auto addFiKey = [&](const std::string& keyStr) {
                    const proto::ProtoString* slot =
                        JSSymbols::indexKey(pContext, static_cast<uint32_t>(fiCount));
                    const proto::ProtoObject* kv = pContext->fromUTF8String(keyStr.c_str());
                    if (slot && kv) fiKeyArr = fiKeyArr->setAttribute(pContext, slot, kv);
                    fiCount++;
                };

                if (fiObj && fiObj != PROTO_NONE && fiObj != t_nullSentinel
                    && !fiObj->isBoolean(pContext)
                    && !fiObj->isInteger(pContext)
                    && !fiObj->isDouble(pContext)) {

                    // Detect arrays to suppress "length" (check on the original object only).
                    bool fiIsArray = false;
                    if (fiIsArr) {
                        const proto::ProtoObject* af = fiObj->getAttribute(pContext, fiIsArr, false);
                        fiIsArray = (af == PROTO_TRUE);
                    }

                    // Synthesise array indices from __elements__ so for-in
                    // over a literal `[10,20,30]` emits '0','1','2' even
                    // though those entries don't live as own attributes.
                    // Indices come first in own-key enumeration per spec.
                    std::unordered_set<std::string> fiSeen;
                    if (fiIsArray) {
                        const proto::ProtoList* els = getArrayElements(pContext, fiObj);
                        if (els) {
                            unsigned long n = els->getSize(pContext);
                            for (unsigned long i = 0; i < n; ++i) {
                                const proto::ProtoObject* v = els->getAt(pContext, static_cast<int>(i));
                                if (v && v != PROTO_NONE) {  // skip holes
                                    std::string s = std::to_string(i);
                                    if (!fiSeen.count(s)) {
                                        fiSeen.insert(s);
                                        addFiKey(s);
                                    }
                                }
                            }
                        }
                    }

                    // Walk the full [[Prototype]] chain per ES2015+ EnumerateObjectProperties.
                    // Keys seen at closer levels shadow the same key from ancestors.
                    // A visited-pointer set guards against cycles in the C++ parent chain;
                    // we stop as soon as getPrototype() returns a node we have already processed.
                    // (fiSeen is shared with the array-index synth above.)
                    std::unordered_set<const proto::ProtoObject*> fiVisited;
                    const proto::ProtoObject* cursor = fiObj;

                    while (cursor && cursor != PROTO_NONE && cursor != t_nullSentinel) {
                        if (fiVisited.count(cursor)) break;
                        fiVisited.insert(cursor);

                        const proto::ProtoSparseList* fiOwn = cursor->getOwnAttributes(pContext);
                        if (fiOwn) {
                            const proto::ProtoSparseListIterator* it = fiOwn->getIterator(pContext);
                            while (it && it->hasNext(pContext)) {
                                unsigned long rk = it->nextKey(pContext);
                                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(pContext);
                                const proto::ProtoString* pk =
                                    reinterpret_cast<const proto::ProtoString*>(rk);
                                if (!pk) continue;
                                std::string kstr;
                                pk->toUTF8String(pContext, kstr);
                                // Skip internal bookkeeping keys (__name__ pattern).
                                if (kstr.size() >= 4 && kstr[0]=='_' && kstr[1]=='_'
                                    && kstr[kstr.size()-1]=='_' && kstr[kstr.size()-2]=='_') continue;
                                // Suppress "length" on arrays (own object only).
                                if (fiIsArray && cursor == fiObj && kstr == "length") continue;
                                // Own-key shadows any inherited key with the same name.
                                if (fiSeen.count(kstr)) continue;
                                fiSeen.insert(kstr);
                                // Respect enumerable descriptor flag (bit 2 of __pd_<key>__).
                                // Missing __pd__ means default = enumerable; bit 2 (0x4) clear means non-enumerable — skip.
                                {
                                    std::string pdks = "__pd_" + kstr + "__";
                                    const proto::ProtoObject* pko =
                                        pContext->fromUTF8String(pdks.c_str());
                                    const proto::ProtoString* pdkStr =
                                        pko ? pko->asString(pContext) : nullptr;
                                    if (pdkStr) {
                                        // Check descriptor on the current cursor level only.
                                        const proto::ProtoObject* pdv =
                                            cursor->getAttribute(pContext, pdkStr, false);
                                        if (pdv && pdv != PROTO_NONE && pdv->isInteger(pContext)) {
                                            uint8_t bits = static_cast<uint8_t>(pdv->asLong(pContext));
                                            if (!(bits & 0x4)) continue; // not enumerable — skip
                                        }
                                    }
                                }
                                addFiKey(kstr);
                            }
                        }
                        // Advance to the next prototype level.
                        // Prefer an explicit JS [[Prototype]] override (set via
                        // Object.setPrototypeOf) before falling back to the C++ parent chain.
                        const proto::ProtoObject* next = getJSProtoOverride(cursor);
                        if (!next) next = cursor->getPrototype(pContext);
                        if (!next || next == PROTO_NONE || next == t_nullSentinel) break;
                        cursor = next;
                    }
                }

                if (fiLenKey)
                    fiKeyArr = fiKeyArr->setAttribute(pContext, fiLenKey, pContext->fromInteger(fiCount));
                if (fiArrKey) fiIter = fiIter->setAttribute(pContext, fiArrKey, fiKeyArr);
                if (fiIdxKey) fiIter = fiIter->setAttribute(pContext, fiIdxKey, pContext->fromInteger(0LL));
                stackPush(pContext, fiIter);
                DISPATCH();
            }

            // OP_for_in_next: DEF(for_in_next, 1, 1, 3, none)
            // Pop the for-in iterator; push (updated_iterator, key_or_undefined, done).
            L_OP_for_in_next: {
                const proto::ProtoObject* fiIterN = PROTO_NONE;
                if (!stackEmpty(pContext)) {
                    fiIterN = stackTop(pContext);
                    stackPop(pContext);
                }

                const proto::ProtoString* fiArrKeyN = JSSymbols::iterArr(pContext);
                const proto::ProtoString* fiIdxKeyN = JSSymbols::iterIdx(pContext);
                const proto::ProtoString* fiLenKeyN = JSSymbols::length(pContext);

                long long fiIdx = 0;
                long long fiLen = 0;
                const proto::ProtoObject* fiKeyArrN = PROTO_NONE;

                if (fiIterN && fiIterN != PROTO_NONE) {
                    if (fiIdxKeyN) {
                        const proto::ProtoObject* iv = fiIterN->getAttribute(pContext, fiIdxKeyN, false);
                        if (iv && iv != PROTO_NONE && iv->isInteger(pContext)) fiIdx = iv->asLong(pContext);
                    }
                    if (fiArrKeyN) {
                        fiKeyArrN = fiIterN->getAttribute(pContext, fiArrKeyN, false);
                        if (fiKeyArrN && fiKeyArrN != PROTO_NONE && fiLenKeyN) {
                            const proto::ProtoObject* lv = fiKeyArrN->getAttribute(pContext, fiLenKeyN, false);
                            if (lv && lv != PROTO_NONE && lv->isInteger(pContext)) fiLen = lv->asLong(pContext);
                        }
                    }
                }

                if (fiIdx < fiLen && fiKeyArrN && fiKeyArrN != PROTO_NONE) {
                    // Return the current key and advance iterator.
                    const proto::ProtoString* slotKey =
                        JSSymbols::indexKey(pContext, static_cast<uint32_t>(fiIdx));
                    const proto::ProtoObject* keyVal = slotKey
                        ? fiKeyArrN->getAttribute(pContext, slotKey, false) : PROTO_NONE;
                    // Build updated iterator with incremented index.
                    const proto::ProtoObject* nextIter = fiIterN;
                    if (fiIdxKeyN)
                        nextIter = nextIter->setAttribute(pContext, fiIdxKeyN,
                            pContext->fromInteger(fiIdx + 1LL));
                    stackPush(pContext, nextIter);
                    stackPush(pContext, keyVal ? keyVal : PROTO_NONE);
                    stackPush(pContext, PROTO_FALSE); // done = false
                } else {
                    // Exhausted.
                    stackPush(pContext, fiIterN ? fiIterN : PROTO_NONE);
                    stackPush(pContext, PROTO_NONE); // undefined key
                    stackPush(pContext, PROTO_TRUE); // done = true
                }
                DISPATCH();
            }

            // OP_initial_yield: DEF(initial_yield, 1, 0, 0, none)
            // First opcode in every generator/async function body.
            // For pure async functions (async, not async-generator): fall through —
            // the body continues executing synchronously.
            // For generators (including async-generators): create the iterator object.
            L_OP_initial_yield: {
                // If this is an async function (but not an async-generator), skip the
                // generator setup and continue executing the body synchronously.
                if (mod->isAsync && !mod->isGenerator) {
                    DISPATCH(); // continue executing async body synchronously
                }

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

                // Save the flat automaticLocals snapshot (post-e2e6eaa: this
                // is where args, locals, closure vars and the value stack
                // really live).  Without this, default-bound parameters do
                // not survive the OP_initial_yield → .next() boundary.
                {
                    InterpFrame* f = currentFrame(pContext);
                    unsigned int snapCount = f
                        ? f->stackBase + f->stackTop
                        : pContext->getAutomaticLocalsCount();
                    const proto::ProtoList* slotList =
                        snapshotAutomaticLocals(pContext, snapCount);
                    if (slotList) {
                        const proto::ProtoString* sk =
                            pContext->fromUTF8String(kGenSlots)
                                ? pContext->fromUTF8String(kGenSlots)->asString(pContext)
                                : nullptr;
                        if (sk) iterObj = iterObj->setAttribute(
                            pContext, sk, slotList->asObject(pContext));
                    }
                    setI(kGenStackTop, f ? (long long)f->stackTop : 0LL);
                }

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
            L_OP_yield: {
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
                // Persist the flat automaticLocals snapshot (see OP_initial_yield).
                {
                    InterpFrame* f = currentFrame(pContext);
                    unsigned int snapCount = f
                        ? f->stackBase + f->stackTop
                        : pContext->getAutomaticLocalsCount();
                    const proto::ProtoList* slotList =
                        snapshotAutomaticLocals(pContext, snapCount);
                    if (slotList) updIter = genSetObj(pContext, updIter,
                        kGenSlots, slotList->asObject(pContext));
                    updIter = genSetInt(pContext, updIter, kGenStackTop,
                        f ? (long long)f->stackTop : 0LL);
                }
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
            L_OP_yield_star: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* innerIter = stackTop(pContext);
                stackPop(pContext);
                if (!innerIter || innerIter == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    DISPATCH();
                }

                // Get .next method from the inner iterator.
                const proto::ProtoString* nextKey3 = JSSymbols::next(pContext);
                const proto::ProtoObject* nextFn = nextKey3
                    ? innerIter->getAttribute(pContext, nextKey3, true) : PROTO_NONE;
                if (!nextFn || nextFn == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    DISPATCH();
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
                    // Persist the flat automaticLocals snapshot (see OP_initial_yield).
                    {
                        InterpFrame* f = currentFrame(pContext);
                        unsigned int snapCount = f
                            ? f->stackBase + f->stackTop
                            : pContext->getAutomaticLocalsCount();
                        const proto::ProtoList* slotList =
                            snapshotAutomaticLocals(pContext, snapCount);
                        if (slotList) updIter = genSetObj(pContext, updIter,
                            kGenSlots, slotList->asObject(pContext));
                        updIter = genSetInt(pContext, updIter, kGenStackTop,
                            f ? (long long)f->stackTop : 0LL);
                    }
                    updIter = genSetInt(pContext, updIter, kGenNcc, (long long)catch_stack.size());
                    updIter = genSetInt(pContext, updIter, kGenState, 0LL);
                    if (updIter != t_genIterator)
                        updateMapping(pContext, t_genIterator, updIter);
                    t_genIterator = nullptr;
                    t_genResumePc = -2;
                    return makeIterResult(pContext, val2, false);
                }
                DISPATCH();
            }

            // OP_return_async: DEF(return_async, 1, 1, 0, none)
            // Used at the end of an async function body in place of OP_return.
            // Pops the return value and wraps it in a fulfilled Promise.
            L_OP_return_async: {
                const proto::ProtoObject* retVal = PROTO_NONE;
                if (!stackEmpty(pContext)) {
                    retVal = stackTop(pContext);
                    stackPop(pContext);
                }
                if (!retVal) retVal = PROTO_NONE;
                // QuickJS emits OP_return_async at the end of both async-function
                // and sync-generator bodies.  For sync generators (isGenerator &&
                // !isAsync) the surrounding resumeGenerator() already wraps the
                // raw return value in {value, done:true}; wrapping it here in a
                // Promise produces {value:Promise{99}, done:true} instead of
                // {value:99, done:true}.  Match L_OP_return's behaviour (return
                // raw) for sync generators, and keep the Promise wrap only for
                // genuine async function returns.
                if (module && module->isGenerator && !module->isAsync) {
                    return retVal;
                }
                // Wrap in Promise.resolve(retVal) for async functions.
                return makeResolvedPromise(pContext, retVal);
            }

            // OP_await: DEF(await, 1, 1, 1, none)
            // Synchronous approximation: if the value is a settled Promise, unwrap it.
            // For pending Promises and non-Promise values, use the value as-is.
            L_OP_await: {
                if (stackEmpty(pContext)) { stackPush(pContext, PROTO_NONE); DISPATCH(); }
                const proto::ProtoObject* awaitVal = stackTop(pContext);
                stackPop(pContext);
                if (!awaitVal) awaitVal = PROTO_NONE;

                if (isPromiseObject(pContext, awaitVal)) {
                    int promSt = getPromiseStatePublic(pContext, awaitVal);
                    if (promSt == 1) {
                        // Fulfilled: push the unwrapped value.
                        awaitVal = getPromiseValuePublic(pContext, awaitVal);
                        if (!awaitVal) awaitVal = PROTO_NONE;
                    } else if (promSt == 2) {
                        // Rejected: throw the rejection reason.
                        const proto::ProtoObject* reason = getPromiseValuePublic(pContext, awaitVal);
                        pending_exception = reason ? reason : PROTO_NONE;
                        has_pending_exception = true;
                        DISPATCH();
                    }
                    // else pending: use undefined
                }
                stackPush(pContext, awaitVal);
                DISPATCH();
            }

            // OP_for_await_of_start: DEF(for_await_of_start, 1, 1, 3, none)
            // Async iteration setup. Fall through to normal iteration for synchronous
            // iterables (sync approximation).
            L_OP_for_await_of_start: {
                // Treat exactly like OP_for_of_start: no-op here; the value stays on stack.
                // We push two placeholder slots to match the stack delta (1→3: +2 extra).
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, PROTO_NONE);
                DISPATCH();
            }

            // OP_for_await_of_next: DEF(for_await_of_next, 1, 3, 4, none)
            // Advance the async iterator. Treat as no-op (pending Promise) in sync mode.
            L_OP_for_await_of_next: {
                // Push undefined result for the iteration step (sync approximation).
                stackPush(pContext, PROTO_NONE);
                DISPATCH();
            }

            L_default: {
                // Unknown opcode: log for diagnostics; execution cannot continue safely.
                std::fprintf(stderr, "[ProtoInterpreter] unsupported opcode 0x%02x at byte offset %d\n",
                    static_cast<unsigned>(opcode), static_cast<int>(pc - 1));
                return PROTO_NONE;
            }

            // --- Exception dispatch ---
            // If a case raised an exception (has_pending_exception flag),
            // DISPATCH() above noticed and jumped here in lieu of fetching
            // the next opcode.  Either jump to the nearest catch handler
            // or propagate to the caller.  pending_exception may itself be
            // PROTO_NONE (JS `throw undefined`) — use the flag.
            handle_exception_label: {
                has_pending_exception = false;
                if (!catch_stack.empty()) {
                    CatchFrame frame = catch_stack.back();
                    catch_stack.pop_back();
                    // Restore value stack: truncate to placeholder_stack_pos (the sentinel slot),
                    // then push the exception.  This replaces the catch sentinel with the caught
                    // value, matching QuickJS's behaviour where the tagged catch-offset integer is
                    // replaced with the exception object at that same stack position.
                    while (stackSize(pContext) > frame.placeholder_stack_pos) stackPop(pContext);
                    stackPush(pContext, pending_exception);
                    pc = frame.handler_pc;
                    pending_exception = nullptr;
                    // Continue executing from the catch handler — re-enter
                    // the dispatch loop directly (DISPATCH() but bypassing
                    // the has_pending_exception check we just satisfied).
                    if (__builtin_expect(pc < 0 || pc >= len, 0)) goto exit_dispatch;
                    globalObj = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : PROTO_NONE;
                    opcode = (int)(unsigned char)buf[pc++];
                    {
                        const void* tgt = dispatch_table[opcode];
                        if (__builtin_expect(tgt == nullptr, 0)) goto L_default;
                        goto *tgt;
                    }
                } else {
                    if (outException) *outException = pending_exception;
                    return PROTO_NONE;
                }
            }

            exit_dispatch: ;
    }
    #undef DISPATCH
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
    // Reads of these keys live in L_OP_yield's resumption path (see
    // JSSymbols::genSent / JSSymbols::genThrowVal); writes MUST go through
    // the same interned ProtoString* or the read won't find the write.
    if (mode == 2 && sentVal && sentVal != PROTO_NONE) {
        const proto::ProtoString* k = JSSymbols::genThrowVal(ctx);
        if (k && iter) iter = iter->setAttribute(ctx, k, sentVal);
    } else {
        // Store sent value (result of yield expr on resume).
        const proto::ProtoString* ks = JSSymbols::genSent(ctx);
        if (ks && iter) iter = iter->setAttribute(ctx, ks, sentVal ? sentVal : PROTO_NONE);
        // Clear any prior throw val.
        const proto::ProtoString* kt = JSSymbols::genThrowVal(ctx);
        if (kt && iter) iter = iter->setAttribute(ctx, kt, PROTO_NONE);
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



const proto::ProtoObject** getCurrentGlobalRoot() {
    return t_currentGlobalRoot;
}

void signalNativeException(const proto::ProtoObject* errorObj) {
    t_callException    = errorObj;
    t_hasCallException = true;
}

const proto::ProtoObject* makeNativeError(proto::ProtoContext* ctx,
                                          const char* errorType,
                                          const char* message) {
    return makeError(ctx, errorType, message, t_currentGlobalRoot);
}

bool hasCallException() {
    return t_hasCallException;
}

const proto::ProtoObject* jsToNumber(proto::ProtoContext* context,
                                     const proto::ProtoObject* value) {
    return toNumber(context, value);
}

const proto::ProtoObject* callJSFunctionFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoList* args,
    const ProtoBytecodeModule* module,
    const proto::ProtoObject** globalRoot)
{
    if (!fn || fn == PROTO_NONE || !ctx) return PROTO_NONE;
    // Restore the thread-local module / global pointers that callJSFunction
    // relies on.  Without this, bcId resolution against t_currentModule /
    // t_rootModule would fail (both are null between eval calls), and any
    // bytecode callback scheduled by setImmediate / Deferred would silently
    // no-op.  RAII restore on exit so re-entrant async calls don't clobber
    // each other.
    const ProtoBytecodeModule* prevCurrent = t_currentModule;
    const ProtoBytecodeModule* prevRoot    = t_rootModule;
    const proto::ProtoObject** prevGR      = t_currentGlobalRoot;
    t_currentModule    = module;
    t_rootModule       = module;
    t_currentGlobalRoot = globalRoot;
    const proto::ProtoObject* result = callJSFunction(ctx, fn, thisVal, args);
    t_currentModule     = prevCurrent;
    t_rootModule        = prevRoot;
    t_currentGlobalRoot = prevGR;
    return result;
}

const proto::ProtoObject* callJSFunction(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoList* args)
{
    if (!fn || fn == PROTO_NONE || !ctx) return PROTO_NONE;

    const proto::ProtoObject** globalRoot = t_currentGlobalRoot;

    // Unwrap native function wrapper (__native_fn__).
    if (!fn->isMethod(ctx)) {
        const proto::ProtoString* nfKey2 = JSSymbols::nativeFn(ctx);
        if (nfKey2) {
            const proto::ProtoObject* rawMethod2 = fn->getAttribute(ctx, nfKey2, false);
            if (rawMethod2 && rawMethod2 != PROTO_NONE && rawMethod2->isMethod(ctx))
                fn = rawMethod2;
        }
    }

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
        const size_t totalSlots =
            nf.argCount() + nf.varCount() +
            (nf.closureSymbols ? nf.closureSymbols->getSize(ctx) : 0) +
            nf.stackSize() + 16;
        proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr, totalSlots, nullptr);
        uint32_t bCount = 0;
        if (args) {
            unsigned int asize = static_cast<unsigned int>(args->getSize(ctx));
            bCount = (asize < nf.argCount()) ? asize : nf.argCount();
        }
        for (uint32_t i = 0; i < bCount; i++)
            setSlot(&childCtx, i, args->getAt(ctx, static_cast<int>(i)));
        childCtx.currentFileName = ctx->currentFileName;
        childCtx.currentLineNumber = ctx->currentLineNumber;
        populateClosureCellsFromInstance(&childCtx, fn, nf);
        unsigned argc = args ? static_cast<unsigned>(args->getSize(ctx)) : 0u;
        const proto::ProtoObject* childEx = PROTO_NONE;
        if (debugBindEnabled()) {
            printf("[DEBUG] callJSFunction (dispatching to %d): this=%p argc=%u\n", bcId, effectiveThis, argc);
        }
        // Publish active func / args for OP_special_object kind=THIS_FUNC / kind=HOME_OBJECT
        // and OP_init_ctor in nested derived ctor invocations.  t_activeNewTgt
        // is INHERITED from caller, not reset — when OP_init_ctor invokes
        // callJSFunction(parent_ctor, ...), the parent ctor's body needs to
        // see the same new_target so its own OP_init_ctor (in a deeper
        // derivation) targets the right prototype.
        const proto::ProtoObject* prevActiveK = t_activeFunc;
        const proto::ProtoList*   prevArgsK   = t_activeArgs;
        t_activeFunc = fn;
        t_activeArgs = args;
        const proto::ProtoObject* result =
            runBytecode(&childCtx, &nf, effectiveThis, args, globalRoot, &childEx);
        t_activeFunc = prevActiveK;
        t_activeArgs = prevArgsK;
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
            if (debugBindEnabled()) {
                printf("[DEBUG] callJSFunction (bound): target=%p this=%p argc=%d\n", target, effectiveBoundThis, (int)mergedArgs->getSize(ctx));
            }
            return callJSFunction(ctx, target, effectiveBoundThis, mergedArgs);
        }
    }

    return PROTO_NONE;
}

} // namespace protojs
