// Promise objects and the Promise built-ins, ECMA-262 §27.2.
//
// See PromisePrototype.h for the representation of a promise and
// MicrotaskQueue.h for the job queue the reactions run on.
//
// GC notes.  Everything a pending promise will need -- its reactions, the
// handlers and the promises or continuations they settle -- is reachable from
// the promise's own record, and a job's operands are reachable from the job
// queue's holder until the job has run (MicrotaskQueue.h).  Settling a promise
// queues the reaction jobs BEFORE it publishes the settled record, so the
// reaction list is never reachable only from a C++ local while allocations
// happen.  The records that resolve functions, element functions and the
// finally() closures carry are referenced only by those function objects, and
// none of them references back the function that holds it, so no cycle among
// mutable objects is formed (protoCore docs/MemoryModel.md §7).

#include "PromisePrototype.h"

#include "ArrayElementsStorage.h"
#include "ArrayPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "MicrotaskQueue.h"
#include "ObjectPrototype.h"
#include "ProtoDeferred.h"
#include "ProxyBuiltin.h"
#include "runtime/PinnedBuiltin.h"
#include "runtime/ProtoInterpreter.h"

#include <string>
#include <vector>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Keys.  Interned symbols are perennial, so a process-wide cache is safe.
// ---------------------------------------------------------------------------

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* name) {
    return proto::ProtoString::createSymbol(ctx, name);
}

#define PROMISE_KEY(fn, text) \
    const proto::ProtoString* fn(proto::ProtoContext* ctx) { \
        static const proto::ProtoString* k = sym(ctx, text); \
        return k; \
    }

PROMISE_KEY(keySlot,            "__promise__")
PROMISE_KEY(keyRfPromise,       "__rf_promise__")
PROMISE_KEY(keyRfDone,          "__rf_done__")
PROMISE_KEY(keyCapResolve,      "__cap_resolve__")
PROMISE_KEY(keyCapReject,       "__cap_reject__")
PROMISE_KEY(keyThen,            "then")
PROMISE_KEY(keyThenGetter,      "__get_then__")
PROMISE_KEY(keyConstructorGetter, "__get_constructor__")
PROMISE_KEY(keyResolve,         "resolve")
PROMISE_KEY(keyResolveGetter,   "__get_resolve__")
PROMISE_KEY(keySpeciesGetter,   "__get_Symbol.species__")
PROMISE_KEY(keyIterGetter,      "__get_Symbol.iterator__")
PROMISE_KEY(keyReturn,          "return")
PROMISE_KEY(keyStatus,          "status")
PROMISE_KEY(keyReason,          "reason")
PROMISE_KEY(keyErrors,          "errors")
PROMISE_KEY(keyPdErrors,        "__pd_errors__")
PROMISE_KEY(keyPromise,         "promise")
PROMISE_KEY(keyReject,          "reject")
// Combinator records.
PROMISE_KEY(keyAggValues,       "__agg_values__")
PROMISE_KEY(keyAggRemaining,    "__agg_remaining__")
PROMISE_KEY(keyAggCap,          "__agg_cap__")
PROMISE_KEY(keyElCalled,        "__el_called__")
PROMISE_KEY(keyElIndex,         "__el_index__")
PROMISE_KEY(keyElAgg,           "__el_agg__")

#undef PROMISE_KEY

enum : long long { kPending = 0, kFulfilled = 1, kRejected = 2 };

// ---------------------------------------------------------------------------
// Small helpers.
// ---------------------------------------------------------------------------

const proto::ProtoObject* undef() {
    const proto::ProtoObject* u = getUndefinedSentinel();
    return u ? u : PROTO_NONE;
}

bool isUndefinedValue(const proto::ProtoObject* v) {
    return !v || v == PROTO_NONE || v == getUndefinedSentinel();
}

bool isNullValue(const proto::ProtoObject* v) {
    return v && v == getNullSentinel();
}

// Type(v) is Object: not undefined, null, a boolean, a number, a string, a
// Symbol or a BigInt (the last two are carried as tagged objects).
bool isObjectValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (isUndefinedValue(v) || isNullValue(v)) return false;
    if (v == PROTO_TRUE || v == PROTO_FALSE) return false;
    if (v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx) ||
        v->isString(ctx) || v->isBoolean(ctx)) return false;
    if (v->isMethod(ctx)) return true;
    const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
    if (symK && v->getAttribute(ctx, symK, true) == PROTO_TRUE) return false;
    const proto::ProtoString* bigK = JSSymbols::isBigInt(ctx);
    if (bigK && v->getAttribute(ctx, bigK, true) == PROTO_TRUE) return false;
    return true;
}

bool isCallable(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (isUndefinedValue(v) || isNullValue(v)) return false;
    if (isCallableValue(ctx, v)) return true;
    // A callable Proxy: its target is callable.
    if (isProxy(ctx, v)) {
        const proto::ProtoObject* t = proxyTarget(ctx, v);
        return t && isCallable(ctx, t);
    }
    return false;
}

// IsConstructor (§7.2.4), with the markers protoJS's constructors carry.
bool isConstructorValue(proto::ProtoContext* ctx, const proto::ProtoObject* t) {
    if (!isObjectValue(ctx, t)) return false;
    if (isProxy(ctx, t)) {
        const proto::ProtoObject* target = proxyTarget(ctx, t);
        return target && isConstructorValue(ctx, target);
    }
    const proto::ProtoString* ctorK = JSSymbols::construct(ctx);
    if (ctorK) {
        const proto::ProtoObject* cf = t->getAttribute(ctx, ctorK, false);
        if (cf && cf != PROTO_NONE && cf->isMethod(ctx)) return true;
    }
    const proto::ProtoString* icK = JSSymbols::isConstructor(ctx);
    if (icK && t->getAttribute(ctx, icK, false) == PROTO_TRUE) return true;
    const proto::ProtoString* arrK = JSSymbols::arrayCtor(ctx);
    if (arrK && t->getAttribute(ctx, arrK, false) == PROTO_TRUE) return true;
    const proto::ProtoString* errK = JSSymbols::errorCtor(ctx);
    if (errK) {
        const proto::ProtoObject* v = t->getAttribute(ctx, errK, false);
        if (v && v != PROTO_NONE) return true;
    }
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (bfK) {
        const proto::ProtoObject* target = t->getAttribute(ctx, bfK, false);
        if (target && target != PROTO_NONE && !target->isMethod(ctx))
            return isConstructorValue(ctx, target);
    }
    return isBytecodeConstructor(ctx, t);
}

// ToBoolean (§7.1.2).
bool toBooleanValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (isUndefinedValue(v) || isNullValue(v) || v == PROTO_FALSE) return false;
    if (v == PROTO_TRUE) return true;
    if (v->isBoolean(ctx)) return v->asBoolean(ctx);
    if (v->isInteger(ctx)) return v->asLong(ctx) != 0;
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        const double d = v->asDouble(ctx);
        return d == d && d != 0.0;
    }
    if (v->isString(ctx)) return v->asString(ctx)->getSize(ctx) != 0;
    return true;
}

void throwTypeError(proto::ProtoContext* ctx, const char* message) {
    signalNativeException(makeNativeError(ctx, "TypeError", message));
}

// Consume the exception a call left pending, into `out`.  True when there was one.
bool takeException(const proto::ProtoObject*& out) {
    if (!hasCallException()) return false;
    const proto::ProtoObject* e = consumeCallException();
    out = e ? e : PROTO_NONE;
    return true;
}

const proto::ProtoList* list1(proto::ProtoContext* ctx, const proto::ProtoObject* a) {
    const proto::ProtoObject* items[1] = {a ? a : PROTO_NONE};
    return ctx->newList(1, items);
}

const proto::ProtoList* list2(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                              const proto::ProtoObject* b) {
    const proto::ProtoObject* items[2] = {a ? a : PROTO_NONE, b ? b : PROTO_NONE};
    return ctx->newList(2, items);
}

const proto::ProtoObject* argAt(proto::ProtoContext* ctx, const proto::ProtoList* args, int i) {
    if (!args || static_cast<proto::proto_ulong>(i) >= args->getSize(ctx)) return undef();
    const proto::ProtoObject* v = args->getAt(ctx, i);
    return v ? v : undef();
}

// [[Get]](key) from native code: a Proxy's get trap, an own data property, an
// accessor anywhere on the chain (the `__get_<key>__` sidecar the runtime
// stores accessors under), then inherited data.  May call JavaScript; the
// caller checks hasCallException().
const proto::ProtoObject* jsGet(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                const proto::ProtoString* key,
                                const proto::ProtoString* getterKey) {
    if (!obj || obj == PROTO_NONE || !key) return undef();
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* v = proxyDispatchGet(ctx, obj, key, obj);
        return v ? v : undef();
    }
    if (obj->hasOwnAttribute(ctx, key) == PROTO_TRUE) {
        const proto::ProtoObject* own = obj->getAttribute(ctx, key, false);
        // An accessor's data slot holds undefined; its getter decides.
        if (own && own != PROTO_NONE && own != getUndefinedSentinel()) return own;
    }
    if (getterKey) {
        const proto::ProtoObject* getter = obj->getAttribute(ctx, getterKey, true);
        if (getter && getter != PROTO_NONE && isCallable(ctx, getter)) {
            const proto::ProtoObject* v = callJSFunction(ctx, getter, obj, ctx->newList());
            return v ? v : undef();
        }
    }
    const proto::ProtoObject* v = obj->getAttribute(ctx, key, true);
    return v ? v : undef();
}

// The %Array.prototype% of the running global, for arrays made here.
const proto::ProtoObject* arrayPrototype(proto::ProtoContext* ctx) {
    const proto::ProtoObject** gr = getCurrentGlobalRoot();
    const proto::ProtoString* k = JSSymbols::arrayProto(ctx);
    if (!gr || !*gr || !k) return nullptr;
    const proto::ProtoObject* p = (*gr)->getAttribute(ctx, k, false);
    return (p && p != PROTO_NONE) ? p : nullptr;
}

const proto::ProtoObject* arrayFromList(proto::ProtoContext* ctx, const proto::ProtoList* list) {
    const proto::ProtoObject* arr = createNewArray(ctx, arrayPrototype(ctx));
    if (!list) list = ctx->newList();
    if (arr) setArrayElements(ctx, arr, list);
    return arr ? arr : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Intrinsics: %Promise% and %Promise.prototype% of each space (shared by
// every thread of the space; runtime/PinnedBuiltin.h).
// ---------------------------------------------------------------------------

PinnedBuiltin g_promiseCtor;
PinnedBuiltin g_promiseProto;
PinnedBuiltin g_promiseThen;

const proto::ProtoObject* intrinsicPromiseProto(proto::ProtoContext* ctx) {
    if (const proto::ProtoObject* p = g_promiseProto.get(ctx)) return p;
    // Before ensurePromiseConstructor ran in this space: read the global.
    const proto::ProtoObject** gr = getCurrentGlobalRoot();
    if (!gr || !*gr) return nullptr;
    const proto::ProtoObject* ctor = (*gr)->getAttribute(ctx, sym(ctx, "Promise"), false);
    if (!ctor || ctor == PROTO_NONE) return nullptr;
    const proto::ProtoObject* proto = ctor->getAttribute(ctx, JSSymbols::prototype(ctx), false);
    return (proto && proto != PROTO_NONE) ? proto : nullptr;
}

const proto::ProtoObject* intrinsicPromiseCtor(proto::ProtoContext* ctx) {
    return g_promiseCtor.get(ctx);
}

// ---------------------------------------------------------------------------
// The promise record.
// ---------------------------------------------------------------------------

struct Rec {
    long long state = kPending;
    const proto::ProtoObject* result = PROTO_NONE;
    const proto::ProtoList* reactions = nullptr;
    bool handled = false;
};

bool readRec(proto::ProtoContext* ctx, const proto::ProtoObject* p, Rec& out) {
    if (!p || p == PROTO_NONE) return false;
    if (p->isInteger(ctx) || p->isDouble(ctx) || p->isString(ctx) || p->isBoolean(ctx))
        return false;
    const proto::ProtoObject* v = p->getOwnAttributeDirect(ctx, keySlot(ctx));
    if (!v || v == PROTO_NONE) return false;
    const proto::ProtoList* l = v->asList(ctx);
    if (!l || l->getSize(ctx) < 4) return false;
    const proto::ProtoObject* st = l->getAt(ctx, 0);
    out.state = (st && st->isInteger(ctx)) ? st->asLong(ctx) : kPending;
    out.result = l->getAt(ctx, 1);
    if (!out.result) out.result = PROTO_NONE;
    const proto::ProtoObject* r = l->getAt(ctx, 2);
    out.reactions = (r && r != PROTO_NONE) ? r->asList(ctx) : nullptr;
    out.handled = l->getAt(ctx, 3) == PROTO_TRUE;
    return true;
}

void writeRec(proto::ProtoContext* ctx, const proto::ProtoObject* p, const Rec& r) {
    const proto::ProtoObject* items[4] = {
        ctx->fromInteger(r.state),
        r.result ? r.result : PROTO_NONE,
        r.reactions ? r.reactions->asObject(ctx) : PROTO_NONE,
        r.handled ? PROTO_TRUE : PROTO_FALSE,
    };
    p->setAttribute(ctx, keySlot(ctx), ctx->newList(4, items)->asObject(ctx));
}

bool isPromise(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    Rec r;
    return readRec(ctx, obj, r);
}

// Initialise `p` as a pending promise.
const proto::ProtoObject* initPromise(proto::ProtoContext* ctx, const proto::ProtoObject* p) {
    Rec r;
    writeRec(ctx, p, r);
    return p;
}

void enqueueJob(proto::ProtoContext* ctx, const proto::ProtoObject* const* items, unsigned n) {
    MicrotaskQueue* q = MicrotaskQueue::current();
    if (!q) return;  // No agent on this thread: nothing will ever run the job.
    q->enqueue(ctx, ctx->newList(n, items));
}

void enqueueReactionJob(proto::ProtoContext* ctx, const proto::ProtoObject* reaction,
                        const proto::ProtoObject* argument, long long state) {
    const proto::ProtoObject* items[4] = {
        ctx->fromInteger(MicrotaskQueue::kReactionJob), reaction,
        argument ? argument : PROTO_NONE, ctx->fromInteger(state)};
    enqueueJob(ctx, items, 4);
}

// FulfillPromise / RejectPromise + TriggerPromiseReactions (§27.2.1.4, .7, .8).
void settle(proto::ProtoContext* ctx, const proto::ProtoObject* p, long long state,
            const proto::ProtoObject* value) {
    Rec r;
    if (!readRec(ctx, p, r) || r.state != kPending) return;
    if (!value) value = PROTO_NONE;
    // Queue the reactions while they are still reachable from the promise.
    if (r.reactions) {
        const proto::proto_ulong n = r.reactions->getSize(ctx);
        for (proto::proto_ulong i = 0; i < n; ++i)
            enqueueReactionJob(ctx, r.reactions->getAt(ctx, static_cast<int>(i)), value, state);
    }
    Rec settled;
    settled.state = state;
    settled.result = value;
    settled.handled = r.handled;
    writeRec(ctx, p, settled);
    // HostPromiseRejectionTracker(promise, "reject").
    if (state == kRejected && !r.handled) {
        if (MicrotaskQueue* q = MicrotaskQueue::current()) q->noteRejection(ctx, p);
    }
}

// ---------------------------------------------------------------------------
// Resolving functions (§27.2.1.3).
// ---------------------------------------------------------------------------

const proto::ProtoObject* resolvingFunctionTarget(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* record) {
    if (!record || record == PROTO_NONE) return nullptr;
    // [[AlreadyResolved]]
    if (record->getOwnAttributeDirect(ctx, keyRfDone(ctx)) == PROTO_TRUE) return nullptr;
    const proto::ProtoObject* p = record->getOwnAttributeDirect(ctx, keyRfPromise(ctx));
    if (!p || p == PROTO_NONE) return nullptr;
    record->setAttribute(ctx, keyRfDone(ctx), PROTO_TRUE);
    return p;
}

const proto::ProtoObject* resolvingResolve(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* self,
                                           const proto::ParentLink*,
                                           const proto::ProtoList* args,
                                           const proto::ProtoSparseList*) {
    if (const proto::ProtoObject* p = resolvingFunctionTarget(ctx, self))
        resolvePromise(ctx, p, argAt(ctx, args, 0));
    return undef();
}

const proto::ProtoObject* resolvingReject(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* self,
                                          const proto::ParentLink*,
                                          const proto::ProtoList* args,
                                          const proto::ProtoSparseList*) {
    if (const proto::ProtoObject* p = resolvingFunctionTarget(ctx, self))
        rejectPromise(ctx, p, argAt(ctx, args, 0));
    return undef();
}

void createResolvingFunctions(proto::ProtoContext* ctx, const proto::ProtoObject* p,
                              const proto::ProtoObject*& resolveFn,
                              const proto::ProtoObject*& rejectFn) {
    const proto::ProtoObject* record = ctx->newObject(true);
    record->setAttribute(ctx, keyRfPromise(ctx), p);
    resolveFn = makeNativeClosure(ctx, resolvingResolve, record, 1, "");
    rejectFn  = makeNativeClosure(ctx, resolvingReject, record, 1, "");
}

// ---------------------------------------------------------------------------
// PromiseCapability records (§27.2.1.1).
// ---------------------------------------------------------------------------

// A capability of %Promise% is just a promise this file settles directly
// (`native`); its resolve / reject functions are made only when a caller needs
// them as values.
struct Capability {
    const proto::ProtoObject* promise = PROTO_NONE;
    const proto::ProtoObject* resolve = PROTO_NONE;
    const proto::ProtoObject* reject  = PROTO_NONE;
    bool native = false;
};

void materializeFunctions(proto::ProtoContext* ctx, Capability& cap) {
    if (!cap.native) return;
    createResolvingFunctions(ctx, cap.promise, cap.resolve, cap.reject);
    cap.native = false;
}

// GetCapabilitiesExecutor (§27.2.1.5.1): the executor passed to C.
const proto::ProtoObject* capabilityExecutor(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    if (!self || self == PROTO_NONE) return undef();
    const proto::ProtoObject* curRes = self->getOwnAttributeDirect(ctx, keyCapResolve(ctx));
    const proto::ProtoObject* curRej = self->getOwnAttributeDirect(ctx, keyCapReject(ctx));
    if (!isUndefinedValue(curRes) || !isUndefinedValue(curRej)) {
        throwTypeError(ctx, "Promise executor has already been invoked with non-undefined arguments");
        return PROTO_NONE;
    }
    self->setAttribute(ctx, keyCapResolve(ctx), argAt(ctx, args, 0));
    self->setAttribute(ctx, keyCapReject(ctx), argAt(ctx, args, 1));
    return undef();
}

// NewPromiseCapability(C) (§27.2.1.5).  False with the exception signalled.
bool newPromiseCapability(proto::ProtoContext* ctx, const proto::ProtoObject* C,
                          Capability& out) {
    if (C && C == intrinsicPromiseCtor(ctx)) {
        out.promise = newPromise(ctx);
        out.native = true;
        return true;
    }
    if (!isConstructorValue(ctx, C)) {
        throwTypeError(ctx, "Promise capability: the constructor is not a constructor");
        return false;
    }
    const proto::ProtoObject* record = ctx->newObject(true);
    const proto::ProtoObject* executor = makeNativeClosure(ctx, capabilityExecutor, record, 2, "");
    const proto::ProtoObject* promise = jsConstruct(ctx, C, list1(ctx, executor));
    if (hasCallException()) return false;
    const proto::ProtoObject* res = record->getOwnAttributeDirect(ctx, keyCapResolve(ctx));
    const proto::ProtoObject* rej = record->getOwnAttributeDirect(ctx, keyCapReject(ctx));
    if (!isCallable(ctx, res) || !isCallable(ctx, rej)) {
        throwTypeError(ctx, "Promise resolve or reject function is not callable");
        return false;
    }
    out.promise = promise ? promise : PROTO_NONE;
    out.resolve = res;
    out.reject = rej;
    out.native = false;
    return true;
}

// Call(capability.[[Resolve]] / [[Reject]], undefined, «value»).  An
// exception from a user function is left pending for the caller.
void capabilitySettle(proto::ProtoContext* ctx, const Capability& cap, bool reject,
                      const proto::ProtoObject* value) {
    if (cap.native) {
        if (reject) rejectPromise(ctx, cap.promise, value);
        else resolvePromise(ctx, cap.promise, value);
        return;
    }
    callJSFunction(ctx, reject ? cap.reject : cap.resolve, undef(), list1(ctx, value));
}

// IfAbruptRejectPromise: reject the capability with the pending exception and
// return its promise.
const proto::ProtoObject* rejectWithPending(proto::ProtoContext* ctx, const Capability& cap) {
    const proto::ProtoObject* e = PROTO_NONE;
    takeException(e);
    capabilitySettle(ctx, cap, true, e);
    if (hasCallException()) return PROTO_NONE;
    return cap.promise;
}

// ---------------------------------------------------------------------------
// PerformPromiseThen (§27.2.5.4.1).
// ---------------------------------------------------------------------------

void performThen(proto::ProtoContext* ctx, const proto::ProtoObject* p,
                 long long kind, const proto::ProtoObject* target,
                 const proto::ProtoObject* onFulfilled, const proto::ProtoObject* onRejected) {
    Rec r;
    if (!readRec(ctx, p, r)) return;
    const proto::ProtoObject* items[4] = {
        ctx->fromInteger(kind), target ? target : PROTO_NONE,
        onFulfilled ? onFulfilled : PROTO_NONE, onRejected ? onRejected : PROTO_NONE};
    const proto::ProtoObject* reaction = ctx->newList(4, items)->asObject(ctx);
    if (r.state == kPending) {
        const proto::ProtoList* reactions = r.reactions ? r.reactions : ctx->newList();
        r.reactions = reactions->appendLast(ctx, reaction);
        r.handled = true;
        writeRec(ctx, p, r);
        return;
    }
    enqueueReactionJob(ctx, reaction, r.result, r.state);
    if (!r.handled) {
        // HostPromiseRejectionTracker(promise, "handle"): setting the flag is
        // what removes it from the turn's unhandled list (MicrotaskQueue.h).
        r.handled = true;
        writeRec(ctx, p, r);
    }
}

void performThenWithCapability(proto::ProtoContext* ctx, const proto::ProtoObject* p,
                               const proto::ProtoObject* onFulfilled,
                               const proto::ProtoObject* onRejected, const Capability* cap) {
    const proto::ProtoObject* onF = isCallable(ctx, onFulfilled) ? onFulfilled : PROTO_NONE;
    const proto::ProtoObject* onR = isCallable(ctx, onRejected) ? onRejected : PROTO_NONE;
    if (!cap) {
        performThen(ctx, p, kReactNone, PROTO_NONE, onF, onR);
    } else if (cap->native) {
        performThen(ctx, p, kReactPromise, cap->promise, onF, onR);
    } else {
        const proto::ProtoObject* items[3] = {cap->promise, cap->resolve, cap->reject};
        performThen(ctx, p, kReactCapability, ctx->newList(3, items)->asObject(ctx), onF, onR);
    }
}

// ---------------------------------------------------------------------------
// SpeciesConstructor (§7.3.22) and PromiseResolve (§27.2.4.7.1).
// ---------------------------------------------------------------------------

// nullptr with the exception signalled on an abrupt completion.
const proto::ProtoObject* speciesConstructor(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* O,
                                             const proto::ProtoObject* defaultCtor) {
    const proto::ProtoObject* C = jsGet(ctx, O, JSSymbols::constructor(ctx),
                                        keyConstructorGetter(ctx));
    if (hasCallException()) return nullptr;
    if (isUndefinedValue(C)) return defaultCtor;
    if (!isObjectValue(ctx, C)) {
        throwTypeError(ctx, "SpeciesConstructor: constructor is not an Object");
        return nullptr;
    }
    const proto::ProtoObject* S = jsGet(ctx, C, JSSymbols::symbolSpecies(ctx),
                                        keySpeciesGetter(ctx));
    if (hasCallException()) return nullptr;
    if (isUndefinedValue(S) || isNullValue(S)) return defaultCtor;
    if (isConstructorValue(ctx, S)) return S;
    throwTypeError(ctx, "SpeciesConstructor: [Symbol.species] is not a constructor");
    return nullptr;
}

const proto::ProtoObject* promiseResolveWith(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* C,
                                             const proto::ProtoObject* x) {
    if (isPromise(ctx, x)) {
        const proto::ProtoObject* xC = jsGet(ctx, x, JSSymbols::constructor(ctx),
                                             keyConstructorGetter(ctx));
        if (hasCallException()) return nullptr;
        if (xC == C) return x;
    }
    Capability cap;
    if (!newPromiseCapability(ctx, C, cap)) return nullptr;
    capabilitySettle(ctx, cap, false, x);
    if (hasCallException()) return nullptr;
    return cap.promise;
}

// ---------------------------------------------------------------------------
// Promise.prototype.then / catch / finally.
// ---------------------------------------------------------------------------

const proto::ProtoObject* promiseThen(proto::ProtoContext* ctx,
                                      const proto::ProtoObject* self,
                                      const proto::ParentLink*,
                                      const proto::ProtoList* args,
                                      const proto::ProtoSparseList*) {
    if (!isPromise(ctx, self)) {
        throwTypeError(ctx, "Promise.prototype.then called on an incompatible receiver");
        return PROTO_NONE;
    }
    const proto::ProtoObject* promiseCtor = intrinsicPromiseCtor(ctx);
    const proto::ProtoObject* C = speciesConstructor(ctx, self, promiseCtor);
    if (!C) return PROTO_NONE;
    Capability cap;
    if (!newPromiseCapability(ctx, C, cap)) return PROTO_NONE;
    performThenWithCapability(ctx, self, argAt(ctx, args, 0), argAt(ctx, args, 1), &cap);
    return cap.promise;
}

// Invoke(target, "then", args).
const proto::ProtoObject* invokeThen(proto::ProtoContext* ctx, const proto::ProtoObject* target,
                                     const proto::ProtoList* thenArgs) {
    const proto::ProtoObject* thenFn = jsGet(ctx, target, keyThen(ctx), keyThenGetter(ctx));
    if (hasCallException()) return PROTO_NONE;
    if (!isCallable(ctx, thenFn)) {
        throwTypeError(ctx, "then is not a function");
        return PROTO_NONE;
    }
    return callJSFunction(ctx, thenFn, target, thenArgs);
}

const proto::ProtoObject* promiseCatch(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* self,
                                       const proto::ParentLink*,
                                       const proto::ProtoList* args,
                                       const proto::ProtoSparseList*) {
    if (isUndefinedValue(self) || isNullValue(self)) {
        throwTypeError(ctx, "Promise.prototype.catch called on null or undefined");
        return PROTO_NONE;
    }
    return invokeThen(ctx, self, list2(ctx, undef(), argAt(ctx, args, 0)));
}

// finally()'s closures carry [onFinally, C] or the settled value.
const proto::ProtoObject* finallyValueThunk(proto::ProtoContext*, const proto::ProtoObject* self,
                                            const proto::ParentLink*, const proto::ProtoList*,
                                            const proto::ProtoSparseList*) {
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* finallyThrower(proto::ProtoContext*, const proto::ProtoObject* self,
                                         const proto::ParentLink*, const proto::ProtoList*,
                                         const proto::ProtoSparseList*) {
    signalNativeException(self ? self : PROTO_NONE);
    return PROTO_NONE;
}

const proto::ProtoObject* finallyReaction(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                          const proto::ProtoList* args, bool rejected) {
    const proto::ProtoList* data = self ? self->asList(ctx) : nullptr;
    if (!data || data->getSize(ctx) < 2) return undef();
    const proto::ProtoObject* onFinally = data->getAt(ctx, 0);
    const proto::ProtoObject* C = data->getAt(ctx, 1);
    const proto::ProtoObject* outcome = argAt(ctx, args, 0);
    const proto::ProtoObject* result = callJSFunction(ctx, onFinally, undef(), ctx->newList());
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* p = promiseResolveWith(ctx, C, result ? result : undef());
    if (!p) return PROTO_NONE;
    const proto::ProtoObject* thunk = rejected
        ? makeNativeClosure(ctx, finallyThrower, outcome, 0, "")
        : makeNativeClosure(ctx, finallyValueThunk, outcome, 0, "");
    return invokeThen(ctx, p, list1(ctx, thunk));
}

const proto::ProtoObject* finallyThen(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                      const proto::ParentLink*, const proto::ProtoList* args,
                                      const proto::ProtoSparseList*) {
    return finallyReaction(ctx, self, args, false);
}

const proto::ProtoObject* finallyCatch(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                       const proto::ParentLink*, const proto::ProtoList* args,
                                       const proto::ProtoSparseList*) {
    return finallyReaction(ctx, self, args, true);
}

const proto::ProtoObject* promiseFinally(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* self,
                                         const proto::ParentLink*,
                                         const proto::ProtoList* args,
                                         const proto::ProtoSparseList*) {
    if (!isObjectValue(ctx, self)) {
        throwTypeError(ctx, "Promise.prototype.finally called on a non-object");
        return PROTO_NONE;
    }
    const proto::ProtoObject* C = speciesConstructor(ctx, self, intrinsicPromiseCtor(ctx));
    if (!C) return PROTO_NONE;
    const proto::ProtoObject* onFinally = argAt(ctx, args, 0);
    const proto::ProtoObject* thenFinally = onFinally;
    const proto::ProtoObject* catchFinally = onFinally;
    if (isCallable(ctx, onFinally)) {
        const proto::ProtoObject* data = list2(ctx, onFinally, C)->asObject(ctx);
        thenFinally = makeNativeClosure(ctx, finallyThen, data, 1, "");
        catchFinally = makeNativeClosure(ctx, finallyCatch, data, 1, "");
    }
    return invokeThen(ctx, self, list2(ctx, thenFinally, catchFinally));
}

// ---------------------------------------------------------------------------
// The Promise constructor (§27.2.3.1).
// ---------------------------------------------------------------------------

const proto::ProtoObject* promiseConstructor(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    const proto::ProtoObject* executor = argAt(ctx, args, 0);
    if (!isCallable(ctx, executor)) {
        throwTypeError(ctx, "Promise resolver is not a function");
        return PROTO_NONE;
    }
    // OP_call_constructor (and Reflect.construct) hand over the receiver,
    // already parented at newTarget.prototype, so subclasses keep their
    // prototype.  A direct native call may not.
    const proto::ProtoObject* p = isObjectValue(ctx, self) ? self : nullptr;
    if (!p) {
        const proto::ProtoObject* proto = intrinsicPromiseProto(ctx);
        p = proto ? proto->newChild(ctx, true) : ctx->newObject(true);
    }
    initPromise(ctx, p);
    const proto::ProtoObject* resolveFn = PROTO_NONE;
    const proto::ProtoObject* rejectFn = PROTO_NONE;
    createResolvingFunctions(ctx, p, resolveFn, rejectFn);
    callJSFunction(ctx, executor, undef(), list2(ctx, resolveFn, rejectFn));
    const proto::ProtoObject* e = PROTO_NONE;
    if (takeException(e)) {
        callJSFunction(ctx, rejectFn, undef(), list1(ctx, e));
        if (hasCallException()) return PROTO_NONE;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Statics: resolve, reject, withResolvers, try.
// ---------------------------------------------------------------------------

bool requireObjectReceiver(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                           const char* method) {
    if (isObjectValue(ctx, self)) return true;
    std::string msg = std::string(method) + " called on a non-object";
    throwTypeError(ctx, msg.c_str());
    return false;
}

const proto::ProtoObject* promiseStaticResolve(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const proto::ParentLink*,
                                               const proto::ProtoList* args,
                                               const proto::ProtoSparseList*) {
    if (!requireObjectReceiver(ctx, self, "Promise.resolve")) return PROTO_NONE;
    const proto::ProtoObject* p = promiseResolveWith(ctx, self, argAt(ctx, args, 0));
    return p ? p : PROTO_NONE;
}

const proto::ProtoObject* promiseStaticReject(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    Capability cap;
    if (!newPromiseCapability(ctx, self, cap)) return PROTO_NONE;
    capabilitySettle(ctx, cap, true, argAt(ctx, args, 0));
    if (hasCallException()) return PROTO_NONE;
    return cap.promise;
}

const proto::ProtoObject* promiseStaticWithResolvers(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* self,
                                                     const proto::ParentLink*,
                                                     const proto::ProtoList*,
                                                     const proto::ProtoSparseList*) {
    Capability cap;
    if (!newPromiseCapability(ctx, self, cap)) return PROTO_NONE;
    materializeFunctions(ctx, cap);
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* obj = objProto ? objProto->newChild(ctx, true) : ctx->newObject(true);
    obj->setAttribute(ctx, keyPromise(ctx), cap.promise);
    obj->setAttribute(ctx, keyResolve(ctx), cap.resolve);
    obj->setAttribute(ctx, keyReject(ctx), cap.reject);
    return obj;
}

const proto::ProtoObject* promiseStaticTry(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* self,
                                           const proto::ParentLink*,
                                           const proto::ProtoList* args,
                                           const proto::ProtoSparseList*) {
    // Promise.try (§27.2.4.8, as revised in 2026): call the callback; a
    // throw rejects a new capability of C, and a value goes through
    // PromiseResolve(C, value), so a C promise the callback returns is
    // returned as it is, not wrapped.
    if (!requireObjectReceiver(ctx, self, "Promise.try")) return PROTO_NONE;
    const int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoList* fwd = (argc > 1) ? args->getSlice(ctx, 1, argc) : ctx->newList();
    const proto::ProtoObject* result =
        callJSFunction(ctx, argAt(ctx, args, 0), undef(), fwd ? fwd : ctx->newList());
    const proto::ProtoObject* e = PROTO_NONE;
    if (takeException(e)) {
        Capability cap;
        if (!newPromiseCapability(ctx, self, cap)) return PROTO_NONE;
        capabilitySettle(ctx, cap, true, e);
        if (hasCallException()) return PROTO_NONE;
        return cap.promise;
    }
    const proto::ProtoObject* p = promiseResolveWith(ctx, self, result ? result : undef());
    return p ? p : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// The iterator protocol, for the combinators.
// ---------------------------------------------------------------------------

struct IteratorRecord {
    const proto::ProtoObject* iterator = PROTO_NONE;
    const proto::ProtoObject* next = PROTO_NONE;
    bool done = false;
    // Strings iterate their code points without the protocol.
    const proto::ProtoList* stringItems = nullptr;
    proto::proto_ulong stringIndex = 0;
};

// GetIterator(obj, sync).  False with the exception signalled.
bool getIterator(proto::ProtoContext* ctx, const proto::ProtoObject* obj, IteratorRecord& rec) {
    if (obj && obj != PROTO_NONE && obj->isString(ctx)) {
        // String.prototype[Symbol.iterator] yields code points.
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        const proto::ProtoList* items = ctx->newList();
        for (size_t i = 0; i < s.size();) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
            if (i + len > s.size()) len = s.size() - i;
            items = items->appendLast(ctx, ctx->fromUTF8String(s.substr(i, len).c_str()));
            i += len;
        }
        rec.stringItems = items;
        return true;
    }
    if (!isObjectValue(ctx, obj)) {
        throwTypeError(ctx, "object is not iterable (cannot read property Symbol(Symbol.iterator))");
        return false;
    }
    const proto::ProtoObject* method = jsGet(ctx, obj, JSSymbols::symbolIterator(ctx),
                                             keyIterGetter(ctx));
    if (hasCallException()) return false;
    if (!isCallable(ctx, method)) {
        throwTypeError(ctx, "object is not iterable (cannot read property Symbol(Symbol.iterator))");
        return false;
    }
    const proto::ProtoObject* it = callJSFunction(ctx, method, obj, ctx->newList());
    if (hasCallException()) return false;
    if (!isObjectValue(ctx, it)) {
        throwTypeError(ctx, "Result of the Symbol.iterator method is not an object");
        return false;
    }
    rec.iterator = it;
    rec.next = jsGet(ctx, it, JSSymbols::next(ctx), nullptr);
    if (hasCallException()) return false;
    return true;
}

// IteratorStepValue: 1 a value, 0 done, -1 an exception (signalled; the
// record is then done, so the caller does not close the iterator).
int iteratorStepValue(proto::ProtoContext* ctx, IteratorRecord& rec,
                      const proto::ProtoObject*& value) {
    if (rec.stringItems) {
        if (rec.stringIndex >= rec.stringItems->getSize(ctx)) { rec.done = true; return 0; }
        value = rec.stringItems->getAt(ctx, static_cast<int>(rec.stringIndex++));
        return 1;
    }
    const proto::ProtoObject* result = callJSFunction(ctx, rec.next, rec.iterator, ctx->newList());
    if (hasCallException()) { rec.done = true; return -1; }
    if (!isObjectValue(ctx, result)) {
        rec.done = true;
        throwTypeError(ctx, "Iterator result is not an object");
        return -1;
    }
    const proto::ProtoObject* done = jsGet(ctx, result, JSSymbols::done(ctx), sym(ctx, "__get_done__"));
    if (hasCallException()) { rec.done = true; return -1; }
    if (toBooleanValue(ctx, done)) { rec.done = true; return 0; }
    value = jsGet(ctx, result, JSSymbols::value(ctx), sym(ctx, "__get_value__"));
    if (hasCallException()) { rec.done = true; return -1; }
    return 1;
}

// IteratorClose with a throw completion: call return() and keep the original
// exception whatever return() does.
void iteratorCloseOnThrow(proto::ProtoContext* ctx, IteratorRecord& rec) {
    if (rec.done || rec.stringItems) return;
    const proto::ProtoObject* original = PROTO_NONE;
    const bool had = takeException(original);
    const proto::ProtoObject* ret = jsGet(ctx, rec.iterator, keyReturn(ctx), sym(ctx, "__get_return__"));
    if (!hasCallException() && isCallable(ctx, ret))
        callJSFunction(ctx, ret, rec.iterator, ctx->newList());
    const proto::ProtoObject* ignored = PROTO_NONE;
    takeException(ignored);
    if (had) signalNativeException(original);
}

// GetPromiseResolve(C): Get(C, "resolve"), which must be callable.
const proto::ProtoObject* getPromiseResolve(proto::ProtoContext* ctx, const proto::ProtoObject* C) {
    const proto::ProtoObject* r = jsGet(ctx, C, keyResolve(ctx), keyResolveGetter(ctx));
    if (hasCallException()) return nullptr;
    if (!isCallable(ctx, r)) {
        throwTypeError(ctx, "Promise resolve is not a function");
        return nullptr;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Promise.all / allSettled / any: element functions over a shared aggregate.
//
// The aggregate (mutable) holds the values list, the remaining-elements count
// and the result capability as [promise, resolve, reject].  Each element
// function carries a mutable record {called, index, aggregate}; allSettled's
// two functions for one element share theirs, which is its [[AlreadyCalled]].
// ---------------------------------------------------------------------------

enum CombinatorKind { kAll, kAllSettled, kAny };

const proto::ProtoObject* makeAggregate(proto::ProtoContext* ctx, const Capability& cap) {
    const proto::ProtoObject* agg = ctx->newObject(true);
    const proto::ProtoObject* capItems[3] = {cap.promise, cap.resolve, cap.reject};
    agg->setAttribute(ctx, keyAggCap(ctx), ctx->newList(3, capItems)->asObject(ctx));
    agg->setAttribute(ctx, keyAggValues(ctx), ctx->newList()->asObject(ctx));
    agg->setAttribute(ctx, keyAggRemaining(ctx), ctx->fromInteger(1));
    return agg;
}

void aggregateAppendUndefined(proto::ProtoContext* ctx, const proto::ProtoObject* agg) {
    const proto::ProtoObject* v = agg->getOwnAttributeDirect(ctx, keyAggValues(ctx));
    const proto::ProtoList* l = (v && v != PROTO_NONE) ? v->asList(ctx) : ctx->newList();
    agg->setAttribute(ctx, keyAggValues(ctx), l->appendLast(ctx, undef())->asObject(ctx));
}

void aggregateSet(proto::ProtoContext* ctx, const proto::ProtoObject* agg, long long index,
                  const proto::ProtoObject* value) {
    const proto::ProtoObject* v = agg->getOwnAttributeDirect(ctx, keyAggValues(ctx));
    const proto::ProtoList* l = (v && v != PROTO_NONE) ? v->asList(ctx) : nullptr;
    if (!l || index < 0 || static_cast<proto::proto_ulong>(index) >= l->getSize(ctx)) return;
    agg->setAttribute(ctx, keyAggValues(ctx),
                      l->setAt(ctx, static_cast<int>(index), value)->asObject(ctx));
}

// remainingElementsCount -= 1; true when it reached zero.
bool aggregateDecrement(proto::ProtoContext* ctx, const proto::ProtoObject* agg) {
    const proto::ProtoObject* r = agg->getOwnAttributeDirect(ctx, keyAggRemaining(ctx));
    long long n = (r && r->isInteger(ctx)) ? r->asLong(ctx) : 0;
    --n;
    agg->setAttribute(ctx, keyAggRemaining(ctx), ctx->fromInteger(n));
    return n == 0;
}

void aggregateIncrement(proto::ProtoContext* ctx, const proto::ProtoObject* agg) {
    const proto::ProtoObject* r = agg->getOwnAttributeDirect(ctx, keyAggRemaining(ctx));
    long long n = (r && r->isInteger(ctx)) ? r->asLong(ctx) : 0;
    agg->setAttribute(ctx, keyAggRemaining(ctx), ctx->fromInteger(n + 1));
}

const proto::ProtoObject* aggregateArray(proto::ProtoContext* ctx, const proto::ProtoObject* agg) {
    const proto::ProtoObject* v = agg->getOwnAttributeDirect(ctx, keyAggValues(ctx));
    return arrayFromList(ctx, (v && v != PROTO_NONE) ? v->asList(ctx) : nullptr);
}

// Finish: call the capability's resolve (all, allSettled) or reject with an
// AggregateError (any).
void aggregateFinish(proto::ProtoContext* ctx, const proto::ProtoObject* agg, bool reject,
                     const proto::ProtoObject* value) {
    const proto::ProtoObject* capObj = agg->getOwnAttributeDirect(ctx, keyAggCap(ctx));
    const proto::ProtoList* cap = capObj ? capObj->asList(ctx) : nullptr;
    if (!cap || cap->getSize(ctx) < 3) return;
    callJSFunction(ctx, cap->getAt(ctx, reject ? 2 : 1), undef(), list1(ctx, value));
}

const proto::ProtoObject* makeAggregateError(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* errorsArray) {
    const proto::ProtoObject* err = makeNativeError(ctx, "AggregateError", "All promises were rejected");
    if (err && err != PROTO_NONE) {
        err->setAttribute(ctx, keyErrors(ctx), errorsArray);
        // {writable: true, enumerable: false, configurable: true}
        err->setAttribute(ctx, keyPdErrors(ctx), ctx->fromInteger(0x3LL));
    }
    return err;
}

const proto::ProtoObject* makeElementRecord(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* agg, long long index) {
    const proto::ProtoObject* el = ctx->newObject(true);
    el->setAttribute(ctx, keyElAgg(ctx), agg);
    el->setAttribute(ctx, keyElIndex(ctx), ctx->fromInteger(index));
    return el;
}

// [[AlreadyCalled]] check-and-set; returns the aggregate, or nullptr when the
// element was already settled.
const proto::ProtoObject* claimElement(proto::ProtoContext* ctx, const proto::ProtoObject* el,
                                       long long& index) {
    if (!el || el == PROTO_NONE) return nullptr;
    if (el->getOwnAttributeDirect(ctx, keyElCalled(ctx)) == PROTO_TRUE) return nullptr;
    el->setAttribute(ctx, keyElCalled(ctx), PROTO_TRUE);
    const proto::ProtoObject* idx = el->getOwnAttributeDirect(ctx, keyElIndex(ctx));
    index = (idx && idx->isInteger(ctx)) ? idx->asLong(ctx) : 0;
    const proto::ProtoObject* agg = el->getOwnAttributeDirect(ctx, keyElAgg(ctx));
    return (agg && agg != PROTO_NONE) ? agg : nullptr;
}

const proto::ProtoObject* settledRecord(proto::ProtoContext* ctx, bool fulfilled,
                                        const proto::ProtoObject* x) {
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* obj = objProto ? objProto->newChild(ctx, true) : ctx->newObject(true);
    obj->setAttribute(ctx, keyStatus(ctx), ctx->fromUTF8String(fulfilled ? "fulfilled" : "rejected"));
    obj->setAttribute(ctx, fulfilled ? JSSymbols::value(ctx) : keyReason(ctx), x);
    return obj;
}

// Promise.all resolve element / allSettled resolve element / reject element.
const proto::ProtoObject* elementSettle(proto::ProtoContext* ctx, const proto::ProtoObject* el,
                                        const proto::ProtoList* args, CombinatorKind kind,
                                        bool fulfilled) {
    long long index = 0;
    const proto::ProtoObject* agg = claimElement(ctx, el, index);
    if (!agg) return undef();
    const proto::ProtoObject* x = argAt(ctx, args, 0);
    aggregateSet(ctx, agg, index, kind == kAllSettled ? settledRecord(ctx, fulfilled, x) : x);
    if (aggregateDecrement(ctx, agg)) {
        const proto::ProtoObject* arr = aggregateArray(ctx, agg);
        if (kind == kAny) aggregateFinish(ctx, agg, true, makeAggregateError(ctx, arr));
        else aggregateFinish(ctx, agg, false, arr);
    }
    return undef();
}

const proto::ProtoObject* allResolveElement(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                            const proto::ParentLink*, const proto::ProtoList* args,
                                            const proto::ProtoSparseList*) {
    return elementSettle(ctx, self, args, kAll, true);
}
const proto::ProtoObject* allSettledResolveElement(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList* args,
                                                   const proto::ProtoSparseList*) {
    return elementSettle(ctx, self, args, kAllSettled, true);
}
const proto::ProtoObject* allSettledRejectElement(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* self,
                                                  const proto::ParentLink*,
                                                  const proto::ProtoList* args,
                                                  const proto::ProtoSparseList*) {
    return elementSettle(ctx, self, args, kAllSettled, false);
}
const proto::ProtoObject* anyRejectElement(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                           const proto::ParentLink*, const proto::ProtoList* args,
                                           const proto::ProtoSparseList*) {
    return elementSettle(ctx, self, args, kAny, false);
}

// The body shared by Promise.all, allSettled, any and race.
const proto::ProtoObject* promiseCombinator(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* C,
                                            const proto::ProtoList* args,
                                            int kind /* CombinatorKind, or -1 for race */) {
    Capability cap;
    if (!newPromiseCapability(ctx, C, cap)) return PROTO_NONE;
    materializeFunctions(ctx, cap);
    const proto::ProtoObject* promiseResolve = getPromiseResolve(ctx, C);
    if (!promiseResolve) return rejectWithPending(ctx, cap);
    IteratorRecord iter;
    if (!getIterator(ctx, argAt(ctx, args, 0), iter)) return rejectWithPending(ctx, cap);

    const proto::ProtoObject* agg = (kind >= 0) ? makeAggregate(ctx, cap) : nullptr;
    long long index = 0;
    for (;;) {
        const proto::ProtoObject* value = PROTO_NONE;
        const int step = iteratorStepValue(ctx, iter, value);
        if (step < 0) return rejectWithPending(ctx, cap);
        if (step == 0) {
            if (agg && aggregateDecrement(ctx, agg)) {
                const proto::ProtoObject* arr = aggregateArray(ctx, agg);
                if (kind == kAny) capabilitySettle(ctx, cap, true, makeAggregateError(ctx, arr));
                else capabilitySettle(ctx, cap, false, arr);
                // A throwing resolve (or reject) is an abrupt completion of
                // the combinator: the iterator is done, so it is not closed,
                // and IfAbruptRejectPromise rejects the capability.
                if (hasCallException()) return rejectWithPending(ctx, cap);
            }
            return cap.promise;
        }
        if (agg) aggregateAppendUndefined(ctx, agg);
        const proto::ProtoObject* nextPromise =
            callJSFunction(ctx, promiseResolve, C, list1(ctx, value));
        if (hasCallException()) {
            iteratorCloseOnThrow(ctx, iter);
            return rejectWithPending(ctx, cap);
        }
        const proto::ProtoObject* onFulfilled = cap.resolve;
        const proto::ProtoObject* onRejected = cap.reject;
        if (kind == kAll) {
            onFulfilled = makeNativeClosure(ctx, allResolveElement,
                                            makeElementRecord(ctx, agg, index), 1, "");
        } else if (kind == kAllSettled) {
            const proto::ProtoObject* el = makeElementRecord(ctx, agg, index);
            onFulfilled = makeNativeClosure(ctx, allSettledResolveElement, el, 1, "");
            onRejected = makeNativeClosure(ctx, allSettledRejectElement, el, 1, "");
        } else if (kind == kAny) {
            onRejected = makeNativeClosure(ctx, anyRejectElement,
                                           makeElementRecord(ctx, agg, index), 1, "");
        }
        if (agg) aggregateIncrement(ctx, agg);
        invokeThen(ctx, nextPromise, list2(ctx, onFulfilled, onRejected));
        if (hasCallException()) {
            iteratorCloseOnThrow(ctx, iter);
            return rejectWithPending(ctx, cap);
        }
        ++index;
    }
}

const proto::ProtoObject* promiseAll(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                     const proto::ParentLink*, const proto::ProtoList* args,
                                     const proto::ProtoSparseList*) {
    return promiseCombinator(ctx, self, args, kAll);
}
const proto::ProtoObject* promiseAllSettled(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                            const proto::ParentLink*, const proto::ProtoList* args,
                                            const proto::ProtoSparseList*) {
    return promiseCombinator(ctx, self, args, kAllSettled);
}
const proto::ProtoObject* promiseAny(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                     const proto::ParentLink*, const proto::ProtoList* args,
                                     const proto::ProtoSparseList*) {
    return promiseCombinator(ctx, self, args, kAny);
}
const proto::ProtoObject* promiseRace(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                      const proto::ParentLink*, const proto::ProtoList* args,
                                      const proto::ProtoSparseList*) {
    return promiseCombinator(ctx, self, args, -1);
}

const proto::ProtoObject* queueMicrotaskNative(proto::ProtoContext* ctx,
                                               const proto::ProtoObject*,
                                               const proto::ParentLink*,
                                               const proto::ProtoList* args,
                                               const proto::ProtoSparseList*) {
    const proto::ProtoObject* cb = argAt(ctx, args, 0);
    if (!isCallable(ctx, cb)) {
        throwTypeError(ctx, "The \"callback\" argument must be of type function");
        return PROTO_NONE;
    }
    if (MicrotaskQueue* q = MicrotaskQueue::current()) q->enqueueCallback(ctx, cb);
    return undef();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public operations.
// ---------------------------------------------------------------------------

const proto::ProtoObject* makeNativeClosure(proto::ProtoContext* ctx, proto::ProtoMethod fn,
                                            const proto::ProtoObject* data, long long length,
                                            const char* name) {
    const proto::ProtoObject* fp = ctx->space ? ctx->space->methodPrototype : nullptr;
    // Built immutable, then made mutable in one step (one publish).
    const proto::ProtoObject* f = fp ? fp->newChild(ctx, false) : ctx->newObject(false);
    f = f->setAttribute(ctx, JSSymbols::boundFn(ctx), ctx->fromMethod(nullptr, fn));
    f = f->setAttribute(ctx, JSSymbols::boundThis(ctx), data ? data : PROTO_NONE);
    f = f->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(length));
    f = f->setAttribute(ctx, JSSymbols::pdLength(ctx), ctx->fromInteger(0x2LL));
    f = f->setAttribute(ctx, JSSymbols::name(ctx), ctx->fromUTF8String(name ? name : ""));
    f = f->setAttribute(ctx, JSSymbols::pdName(ctx), ctx->fromInteger(0x2LL));
    f = f->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);
    return f->clone(ctx, true);
}

const proto::ProtoObject* jsGetProperty(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* obj,
                                        const char* name) {
    std::string getter = std::string("__get_") + name + "__";
    return jsGet(ctx, obj, sym(ctx, name), sym(ctx, getter.c_str()));
}

bool jsIsObject(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return isObjectValue(ctx, v);
}

bool jsIsCallable(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return isCallable(ctx, v);
}

const proto::ProtoObject* newPromise(proto::ProtoContext* ctx) {
    const proto::ProtoObject* proto = intrinsicPromiseProto(ctx);
    const proto::ProtoObject* p = proto ? proto->newChild(ctx, true) : ctx->newObject(true);
    return initPromise(ctx, p);
}

const proto::ProtoObject* newPromiseWithPrototype(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* proto) {
    if (!proto) return newPromise(ctx);
    return initPromise(ctx, proto->newChild(ctx, true));
}

const proto::ProtoObject* intrinsicPromisePrototype(proto::ProtoContext* ctx) {
    return intrinsicPromiseProto(ctx);
}

void resolvePromise(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                    const proto::ProtoObject* resolution) {
    Rec r;
    if (!readRec(ctx, promise, r) || r.state != kPending) return;
    if (!resolution) resolution = undef();
    if (resolution == promise) {
        settle(ctx, promise, kRejected,
               makeNativeError(ctx, "TypeError", "Chaining cycle detected for promise #<Promise>"));
        return;
    }
    if (!isObjectValue(ctx, resolution)) {
        settle(ctx, promise, kFulfilled, resolution);
        return;
    }
    const proto::ProtoObject* thenAction = jsGet(ctx, resolution, keyThen(ctx), keyThenGetter(ctx));
    const proto::ProtoObject* e = PROTO_NONE;
    if (takeException(e)) {
        settle(ctx, promise, kRejected, e);
        return;
    }
    if (!isCallable(ctx, thenAction)) {
        settle(ctx, promise, kFulfilled, resolution);
        return;
    }
    const proto::ProtoObject* items[4] = {
        ctx->fromInteger(MicrotaskQueue::kResolveThenableJob), promise, resolution, thenAction};
    enqueueJob(ctx, items, 4);
}

void rejectPromise(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                   const proto::ProtoObject* reason) {
    settle(ctx, promise, kRejected, reason ? reason : undef());
}

const proto::ProtoObject* promiseResolveIntrinsic(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* value) {
    const proto::ProtoObject* C = intrinsicPromiseCtor(ctx);
    if (!C) {
        const proto::ProtoObject* p = newPromise(ctx);
        resolvePromise(ctx, p, value);
        return p;
    }
    return promiseResolveWith(ctx, C, value ? value : undef());
}

bool performAwait(proto::ProtoContext* ctx, const proto::ProtoObject* value,
                  const proto::ProtoObject* continuation) {
    if (!value) value = undef();
    auto reaction = [&]() {
        const proto::ProtoObject* items[4] = {
            ctx->fromInteger(kReactAwait), continuation, PROTO_NONE, PROTO_NONE};
        return ctx->newList(4, items)->asObject(ctx);
    };
    // A primitive: PromiseResolve makes a promise fulfilled with it, and
    // PerformPromiseThen on a fulfilled promise queues the reaction job.
    if (!isObjectValue(ctx, value)) {
        enqueueReactionJob(ctx, reaction(), value, kFulfilled);
        return true;
    }
    if (isPromise(ctx, value)) {
        const proto::ProtoObject* xC = jsGet(ctx, value, JSSymbols::constructor(ctx),
                                             keyConstructorGetter(ctx));
        if (hasCallException()) return false;
        const proto::ProtoObject* C = intrinsicPromiseCtor(ctx);
        if (C && xC == C) {
            // PromiseResolve returns the promise itself.
            performThen(ctx, value, kReactAwait, continuation, PROTO_NONE, PROTO_NONE);
            return true;
        }
    }
    // Any other object: PromiseResolve resolves a new promise with it, which
    // reads `then` now.  A throwing getter rejects that promise; a callable
    // `then` makes it adopt the thenable through a job; otherwise the promise
    // is fulfilled with the object.
    const proto::ProtoObject* thenAction = jsGet(ctx, value, keyThen(ctx), keyThenGetter(ctx));
    const proto::ProtoObject* e = PROTO_NONE;
    if (takeException(e)) {
        enqueueReactionJob(ctx, reaction(), e, kRejected);
        return true;
    }
    if (!isCallable(ctx, thenAction)) {
        enqueueReactionJob(ctx, reaction(), value, kFulfilled);
        return true;
    }
    const proto::ProtoObject* p = newPromise(ctx);
    const proto::ProtoObject* items[4] = {
        ctx->fromInteger(MicrotaskQueue::kResolveThenableJob), p, value, thenAction};
    enqueueJob(ctx, items, 4);
    performThen(ctx, p, kReactAwait, continuation, PROTO_NONE, PROTO_NONE);
    return true;
}

void performPromiseThenInternal(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                                PromiseReactionKind kind, const proto::ProtoObject* target,
                                const proto::ProtoObject* onFulfilled,
                                const proto::ProtoObject* onRejected) {
    performThen(ctx, promise, kind, target, onFulfilled, onRejected);
}

const proto::ProtoObject* makeResolvedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* value) {
    const proto::ProtoObject* p = newPromise(ctx);
    resolvePromise(ctx, p, value);
    return p;
}

const proto::ProtoObject* makeRejectedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* reason) {
    const proto::ProtoObject* p = newPromise(ctx);
    rejectPromise(ctx, p, reason);
    return p;
}

bool isPromiseObject(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    return isPromise(ctx, obj);
}

int getPromiseStatePublic(proto::ProtoContext* ctx, const proto::ProtoObject* p) {
    Rec r;
    return readRec(ctx, p, r) ? static_cast<int>(r.state) : 0;
}

const proto::ProtoObject* getPromiseValuePublic(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* p) {
    Rec r;
    return (readRec(ctx, p, r) && r.state != kPending) ? r.result : undef();
}

bool isPromiseHandled(proto::ProtoContext* ctx, const proto::ProtoObject* p) {
    Rec r;
    return readRec(ctx, p, r) && r.handled;
}

void markPromiseHandled(proto::ProtoContext* ctx, const proto::ProtoObject* p) {
    Rec r;
    if (!readRec(ctx, p, r) || r.handled) return;
    r.handled = true;
    writeRec(ctx, p, r);
}

const proto::ProtoObject* makeIteratorResult(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* value, bool done) {
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* r = objProto ? objProto->newChild(ctx, false) : ctx->newObject(false);
    r = r->setAttribute(ctx, JSSymbols::value(ctx), value ? value : undef());
    r = r->setAttribute(ctx, JSSymbols::done(ctx), done ? PROTO_TRUE : PROTO_FALSE);
    return r->clone(ctx, true);
}

void promiseReactionJob(proto::ProtoContext* ctx, const proto::ProtoObject* reactionObj,
                        const proto::ProtoObject* argument, int state) {
    const proto::ProtoList* reaction =
        (reactionObj && reactionObj != PROTO_NONE) ? reactionObj->asList(ctx) : nullptr;
    if (!reaction || reaction->getSize(ctx) < 4) return;
    const proto::ProtoObject* kindObj = reaction->getAt(ctx, 0);
    const long long kind = (kindObj && kindObj->isInteger(ctx)) ? kindObj->asLong(ctx) : kReactNone;
    const proto::ProtoObject* target = reaction->getAt(ctx, 1);
    const proto::ProtoObject* onFulfilled = reaction->getAt(ctx, 2);
    const proto::ProtoObject* onRejected = reaction->getAt(ctx, 3);
    const bool rejected = state == kRejected;
    if (!argument) argument = undef();

    switch (kind) {
    case kReactAwait:
        resumeAwait(ctx, target, argument, rejected);
        return;
    case kReactAsyncGenReturn:
        asyncGeneratorReturnSettled(ctx, target, argument, rejected);
        return;
    case kReactIterResult:
        // AsyncFromSyncIteratorContinuation: onFulfilled carries `done`.
        if (rejected) rejectPromise(ctx, target, argument);
        else resolvePromise(ctx, target, makeIteratorResult(ctx, argument, onFulfilled == PROTO_TRUE));
        return;
    default:
        break;
    }

    const proto::ProtoObject* handler = rejected ? onRejected : onFulfilled;
    const proto::ProtoObject* value = argument;
    bool isThrow = rejected;
    if (handler && handler != PROTO_NONE) {
        const proto::ProtoObject* result = callJSFunction(ctx, handler, undef(), list1(ctx, argument));
        const proto::ProtoObject* e = PROTO_NONE;
        if (takeException(e)) { value = e; isThrow = true; }
        else { value = result ? result : undef(); isThrow = false; }
    }
    switch (kind) {
    case kReactPromise:
        if (isThrow) rejectPromise(ctx, target, value);
        else resolvePromise(ctx, target, value);
        break;
    case kReactCapability: {
        const proto::ProtoList* cap = (target && target != PROTO_NONE) ? target->asList(ctx) : nullptr;
        if (cap && cap->getSize(ctx) >= 3) {
            callJSFunction(ctx, cap->getAt(ctx, isThrow ? 2 : 1), undef(), list1(ctx, value));
            const proto::ProtoObject* ignored = PROTO_NONE;
            takeException(ignored);
        }
        break;
    }
    default:
        break;
    }
}

void promiseResolveThenableJob(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                               const proto::ProtoObject* thenable,
                               const proto::ProtoObject* then) {
    const proto::ProtoObject* resolveFn = PROTO_NONE;
    const proto::ProtoObject* rejectFn = PROTO_NONE;
    createResolvingFunctions(ctx, promise, resolveFn, rejectFn);
    callJSFunction(ctx, then, thenable, list2(ctx, resolveFn, rejectFn));
    const proto::ProtoObject* e = PROTO_NONE;
    if (takeException(e)) {
        callJSFunction(ctx, rejectFn, undef(), list1(ctx, e));
        const proto::ProtoObject* ignored = PROTO_NONE;
        takeException(ignored);
    }
}

const proto::ProtoObject* installQueueMicrotask(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* global) {
    if (!ctx || !global) return global;
    const proto::ProtoObject* fp = ctx->space ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* f = fp ? fp->newChild(ctx, true) : ctx->newObject(true);
    f->setAttribute(ctx, JSSymbols::nativeFn(ctx), ctx->fromMethod(nullptr, queueMicrotaskNative));
    f->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(1));
    f->setAttribute(ctx, JSSymbols::pdLength(ctx), ctx->fromInteger(0x2LL));
    f->setAttribute(ctx, JSSymbols::name(ctx), ctx->fromUTF8String("queueMicrotask"));
    f->setAttribute(ctx, JSSymbols::pdName(ctx), ctx->fromInteger(0x2LL));
    return global->setAttribute(ctx, sym(ctx, "queueMicrotask"), f);
}

// ---------------------------------------------------------------------------
// ensurePromiseConstructor — register Promise in globalRoot.
// ---------------------------------------------------------------------------
void ensurePromiseConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    // §27.2.3 / §20.2.3: Promise's [[Prototype]] is %Function.prototype%.
    const proto::ProtoObject* methodProto =
        (ctx->space && ctx->space->methodPrototype)
            ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = methodProto
        ? methodProto->newChild(ctx, true)
        : ctx->newObject(true);
    if (methodProto) setJSProtoOverride(ctx, ctor, methodProto);

    // Raw __construct__ stays a bare ProtoMethod — OP_call_constructor
    // dispatches it directly, never reading name / length.
    ctor = ctor->setAttribute(ctx, JSSymbols::construct(ctx),
                              ctx->fromMethod(nullptr, promiseConstructor));

    // A built-in function object: a mutable child of Function.prototype with
    // __native_fn__ and the §17 name / length descriptors (0x2).
    auto makeBuiltin = [&](const char* name, proto::ProtoMethod fn, long long arity) {
        const proto::ProtoObject* wrapper = methodProto
            ? methodProto->newChild(ctx, true) : ctx->newObject(true);
        wrapper = wrapper->setAttribute(ctx, JSSymbols::nativeFn(ctx), ctx->fromMethod(nullptr, fn));
        wrapper = wrapper->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(arity));
        wrapper = wrapper->setAttribute(ctx, JSSymbols::pdLength(ctx), ctx->fromInteger(0x2LL));
        wrapper = wrapper->setAttribute(ctx, JSSymbols::name(ctx), ctx->fromUTF8String(name));
        wrapper = wrapper->setAttribute(ctx, JSSymbols::pdName(ctx), ctx->fromInteger(0x2LL));
        return wrapper;
    };
    auto setWithDescriptor = [&](const proto::ProtoObject* target, const char* name,
                                 const proto::ProtoObject* value, long long bits) {
        target = target->setAttribute(ctx, sym(ctx, name), value);
        std::string pd = std::string("__pd_") + name + "__";
        target = target->setAttribute(ctx, sym(ctx, pd.c_str()), ctx->fromInteger(bits));
        return target;
    };

    // Statics: {writable, configurable, not enumerable}.
    struct Static { const char* name; proto::ProtoMethod fn; long long arity; };
    static const Static statics[] = {
        {"resolve",       promiseStaticResolve,       1},
        {"reject",        promiseStaticReject,        1},
        {"all",           promiseAll,                 1},
        {"allSettled",    promiseAllSettled,          1},
        {"race",          promiseRace,                1},
        {"any",           promiseAny,                 1},
        {"try",           promiseStaticTry,           1},
        {"withResolvers", promiseStaticWithResolvers, 0},
    };
    for (const Static& s : statics)
        ctor = setWithDescriptor(ctor, s.name, makeBuiltin(s.name, s.fn, s.arity), 0x3LL);

    // §27.2.4.8 get Promise[@@species]: an accessor returning `this`,
    // {enumerable: false, configurable: true}.
    {
        static const proto::ProtoMethod speciesGetter = [](
            proto::ProtoContext*, const proto::ProtoObject* self,
            const proto::ParentLink*, const proto::ProtoList*,
            const proto::ProtoSparseList*) -> const proto::ProtoObject* { return self; };
        const proto::ProtoObject* getter = makeBuiltin("get [Symbol.species]", speciesGetter, 0);
        getter = getter->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);
        ctor = ctor->setAttribute(ctx, keySpeciesGetter(ctx), getter);
        ctor = ctor->setAttribute(ctx, sym(ctx, "__pd_Symbol.species__"), ctx->fromInteger(0x2LL));
    }

    // Promise.prototype: then, catch, finally {writable, configurable}.
    const proto::ProtoObject* proto = ctx->newObject(true);
    const proto::ProtoObject* thenFn = makeBuiltin("then", promiseThen, 2);
    proto = setWithDescriptor(proto, "then", thenFn, 0x3LL);
    proto = setWithDescriptor(proto, "catch", makeBuiltin("catch", promiseCatch, 1), 0x3LL);
    proto = setWithDescriptor(proto, "finally", makeBuiltin("finally", promiseFinally, 1), 0x3LL);

    // Promise.prototype[@@toStringTag] === "Promise" (§27.2.5.5), under the
    // internal sidecar and the user-visible key, descriptor 0x2.
    {
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey) proto = proto->setAttribute(ctx, tagKey, ctx->fromUTF8String("Promise"));
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            proto = proto->setAttribute(ctx, userKey, ctx->fromUTF8String("Promise"));
            proto = proto->setAttribute(ctx, sym(ctx, "__pd_Symbol.toStringTag__"),
                                        ctx->fromInteger(0x2LL));
            proto = proto->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);
        }
    }

    // Promise.prototype.constructor === Promise (§27.2.5.2), descriptor 0x3.
    proto = proto->setAttribute(ctx, JSSymbols::constructor(ctx), ctor);
    proto = proto->setAttribute(ctx, JSSymbols::pdConstructor(ctx), ctx->fromInteger(0x3LL));

    // Promise.prototype (0x0), Promise.name (0x2), Promise.length === 1 (0x2).
    ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), proto);
    ctor = ctor->setAttribute(ctx, sym(ctx, "__pd_prototype__"), ctx->fromInteger(0x0LL));
    ctor = ctor->setAttribute(ctx, JSSymbols::name(ctx), ctx->fromUTF8String("Promise"));
    ctor = ctor->setAttribute(ctx, JSSymbols::pdName(ctx), ctx->fromInteger(0x2LL));
    ctor = ctor->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(1LL));
    ctor = ctor->setAttribute(ctx, JSSymbols::pdLength(ctx), ctx->fromInteger(0x2LL));
    ctor = ctor->setAttribute(ctx, JSSymbols::hasNonWritableProps(ctx), PROTO_TRUE);

    // This space's intrinsics, pinned for the life of the wrapper.
    g_promiseCtor.replace(ctx, ctor);
    g_promiseProto.replace(ctx, proto);
    g_promiseThen.replace(ctx, thenFn);

    *globalRoot = (*globalRoot)->setAttribute(ctx, sym(ctx, "Promise"), ctor);
    *globalRoot = (*globalRoot)->setAttribute(ctx, sym(ctx, "__pd_Promise__"), ctx->fromInteger(0x3LL));
    *globalRoot = installQueueMicrotask(ctx, *globalRoot);
    // %Deferred.prototype% is a child of %Promise.prototype%, which exists from
    // here on: publish it as Deferred.prototype.
    ProtoDeferred::ensurePrototype(ctx, globalRoot);
}

} // namespace protojs
