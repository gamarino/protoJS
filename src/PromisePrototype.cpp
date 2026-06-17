#include "PromisePrototype.h"
#include "ArrayElementsStorage.h"
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "headers/protoCore.h"
#include "runtime/ProtoInterpreter.h"
#include <cstdio>
#include <string>
#include <vector>

namespace protojs {

// ---------------------------------------------------------------------------
// Internal state keys for Promise objects:
//   __promise_state__  : integer  0=pending, 1=fulfilled, 2=rejected
//   __promise_value__  : the fulfillment value or rejection reason
// ---------------------------------------------------------------------------

namespace {

// Forward declarations.
static const proto::ProtoObject* makeSettledPromise(
    proto::ProtoContext* ctx,
    int state,
    const proto::ProtoObject* value);

static const proto::ProtoObject* promiseThen(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);

static const proto::ProtoObject* promiseCatch(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);

static const proto::ProtoObject* promiseFinally(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);

// ---------------------------------------------------------------------------
// isPromise — returns true when obj carries __promise_state__.
// ---------------------------------------------------------------------------
static bool isPromise(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    const proto::ProtoObject* stateKey = ctx->fromUTF8String("__promise_state__");
    const proto::ProtoString* sk = stateKey ? stateKey->asString(ctx) : nullptr;
    if (!sk) return false;
    const proto::ProtoObject* sv = obj->getAttribute(ctx, sk, false);
    return sv && sv != PROTO_NONE;
}

// ---------------------------------------------------------------------------
// getPromiseState / getPromiseValue — read internal state.
// ---------------------------------------------------------------------------
static int getPromiseState(proto::ProtoContext* ctx, const proto::ProtoObject* p) {
    const proto::ProtoObject* k = ctx->fromUTF8String("__promise_state__");
    const proto::ProtoString* ks = k ? k->asString(ctx) : nullptr;
    if (!ks) return 0;
    const proto::ProtoObject* sv = p->getAttribute(ctx, ks, false);
    if (!sv || sv == PROTO_NONE) return 0;
    if (sv->isInteger(ctx)) return static_cast<int>(sv->asLong(ctx));
    return 0;
}

static const proto::ProtoObject* getPromiseValue(proto::ProtoContext* ctx, const proto::ProtoObject* p) {
    const proto::ProtoObject* k = ctx->fromUTF8String("__promise_value__");
    const proto::ProtoString* ks = k ? k->asString(ctx) : nullptr;
    if (!ks) return PROTO_NONE;
    const proto::ProtoObject* v = p->getAttribute(ctx, ks, false);
    return v ? v : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// setAttr — write a named attribute on an object, returning the updated object.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setAttr(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    const char* name,
    const proto::ProtoObject* val)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !obj) return obj;
    return obj->setAttribute(ctx, ks, val ? val : PROTO_NONE);
}

// ---------------------------------------------------------------------------
// attachPromiseMethods — attach then/catch/finally to a promise object.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* attachPromiseMethods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* p)
{
    // Build a function-object wrapper per ECMA-262 §17 (own 'name' and
    // 'length' with descriptor {writable:false, enumerable:false,
    // configurable:true} → 0x2). Plain ctx->fromMethod returns a bare
    // method handle with no attribute surface, so test262 instance-shape
    // assertions on Promise.prototype.then / .catch / .finally fail.
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoObject* wrapper = ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
        if (nfk) wrapper = wrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
        const proto::ProtoString* lenk = JSSymbols::length(ctx);
        if (lenk) {
            wrapper = wrapper->setAttribute(ctx, lenk, ctx->fromInteger(length));
            const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmk = JSSymbols::name(ctx);
        if (nmk) {
            wrapper = wrapper->setAttribute(ctx, nmk, ctx->fromUTF8String(name));
            const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (ks) p = p->setAttribute(ctx, ks, wrapper);
    };
    // ECMA-262 spec lengths: then=2, catch=1, finally=1.
    reg("then",    promiseThen,    2);
    reg("catch",   promiseCatch,   1);
    reg("finally", promiseFinally, 1);
    return p;
}

// ---------------------------------------------------------------------------
// makeSettledPromise — create an already-fulfilled (state=1) or already-rejected
// (state=2) promise, or a pending promise (state=0).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* makeSettledPromise(
    proto::ProtoContext* ctx,
    int state,
    const proto::ProtoObject* value)
{
    // ECMA-262 §27.2.4.1/2/3 — Promise.resolve/reject/all/allSettled return
    // a Promise whose [[Prototype]] is %Promise.prototype% so that
    // `p instanceof Promise` is true. Without parenting on Promise.prototype
    // the returned object only carries direct method attributes and fails
    // the instanceof check.
    const proto::ProtoObject* promiseProto = nullptr;
    if (const proto::ProtoObject** gr = getCurrentGlobalRoot()) {
        if (gr && *gr) {
            const proto::ProtoObject* pkObj = ctx->fromUTF8String("Promise");
            const proto::ProtoString* pks = pkObj ? pkObj->asString(ctx) : nullptr;
            if (pks) {
                const proto::ProtoObject* ctor = (*gr)->getAttribute(ctx, pks, false);
                if (ctor && ctor != PROTO_NONE) {
                    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
                    if (protoKey) {
                        const proto::ProtoObject* pp = ctor->getAttribute(ctx, protoKey, false);
                        if (pp && pp != PROTO_NONE) promiseProto = pp;
                    }
                }
            }
        }
    }
    const proto::ProtoObject* p = promiseProto
        ? promiseProto->newChild(ctx, true)
        : ctx->newObject(true);
    p = setAttr(ctx, p, "__promise_state__", ctx->fromInteger(static_cast<long long>(state)));
    p = setAttr(ctx, p, "__promise_value__", value ? value : PROTO_NONE);
    p = attachPromiseMethods(ctx, p);
    return p;
}

// ---------------------------------------------------------------------------
// promiseThen — Promise.prototype.then(onFulfilled, onRejected)
//
// Synchronous model: settled promises invoke callbacks immediately.
// Pending promises return a new pending promise (no microtask queue needed for
// the test262 synchronous scenarios).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseThen(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // §27.2.5.4 step 2 + §27.2.1.6 IsPromise: receivers without
    // [[PromiseState]] must throw TypeError, not silently produce a
    // rejected promise.  Pre-fix Promise.prototype.then.call
    // (Promise.prototype, ...) returned a rejected promise instead
    // of throwing (built-ins/Promise/prototype/no-promise-state).
    if (!self || self == PROTO_NONE || !isPromise(ctx, self)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Promise.prototype.then called on non-promise"));
        return PROTO_NONE;
    }

    // §27.2.5.4 step 3 + §27.2.4.7 SpeciesConstructor: when
    // self.constructor is non-undefined non-Object, TypeError.  Pre-fix
    // the species check never ran on .then so
    // `p.constructor = null; p.then()` silently succeeded (test262
    // then/ctor-null, ctor-throws).
    {
        const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx);
        if (ctorKey) {
            const proto::ProtoObject* c = self->getAttribute(ctx, ctorKey, true);
            if (c && c != PROTO_NONE && c != getUndefinedSentinel()) {
                bool isPrim = c == getNullSentinel()
                    || c->isInteger(ctx) || c->isDouble(ctx)
                    || c->isFloat(ctx)   || c->isBoolean(ctx)
                    || c->isString(ctx);
                if (isPrim) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "SpeciesConstructor: constructor is not an Object"));
                    return PROTO_NONE;
                }
            }
        }
    }

    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* onFulfilled = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* onRejected  = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!onFulfilled) onFulfilled = PROTO_NONE;
    if (!onRejected)  onRejected  = PROTO_NONE;

    int state = getPromiseState(ctx, self);
    const proto::ProtoObject* value = getPromiseValue(ctx, self);

    // §27.2.1.4 / §27.2.1.7 PerformPromiseThen: if onFulfilled /
    // onRejected aren't callable, fall back to identity / thrower so
    // the chain still propagates the value or reason.  Pre-fix the
    // check `onFulfilled != PROTO_NONE` was satisfied by the
    // undefined sentinel too, so `p.then(undefined, undefined)` ran
    // callJSFunction with undefined as the callback and the chain
    // collapsed to undefined (test262 then/S25.4.5.3_A4.1_T1 / T2 /
    // A4.2_T1 / T2).
    auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE
            || fn == getUndefinedSentinel() || fn == getNullSentinel()) return false;
        if (fn->isMethod(ctx)) return true;
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) return true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) return true;
        const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
        if (bfK && fn->hasAttribute(ctx, bfK) == PROTO_TRUE) return true;
        return false;
    };

    if (state == 1) {
        // Fulfilled: invoke onFulfilled if callable, else propagate value.
        if (isCallable(onFulfilled)) {
            const proto::ProtoList* cbArgs = ctx->newList();
            cbArgs = cbArgs->appendLast(ctx, value ? value : PROTO_NONE);
            const proto::ProtoObject* result = callJSFunction(ctx, onFulfilled, PROTO_NONE, cbArgs);
            if (!result) result = PROTO_NONE;
            if (isPromise(ctx, result)) return result;
            return makeSettledPromise(ctx, 1, result);
        }
        return makeSettledPromise(ctx, 1, value);
    }

    if (state == 2) {
        // Rejected: invoke onRejected if callable, else propagate rejection.
        if (isCallable(onRejected)) {
            const proto::ProtoList* cbArgs = ctx->newList();
            cbArgs = cbArgs->appendLast(ctx, value ? value : PROTO_NONE);
            const proto::ProtoObject* result = callJSFunction(ctx, onRejected, PROTO_NONE, cbArgs);
            if (!result) result = PROTO_NONE;
            if (isPromise(ctx, result)) return result;
            return makeSettledPromise(ctx, 1, result);
        }
        return makeSettledPromise(ctx, 2, value);
    }

    // Pending: return a new pending promise.
    return makeSettledPromise(ctx, 0, PROTO_NONE);
}

// ---------------------------------------------------------------------------
// promiseCatch — Promise.prototype.catch(onRejected)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseCatch(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* psl)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* onRej = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!onRej) onRej = PROTO_NONE;

    // §27.2.5.1 step 2: Return Invoke(self, "then", [undefined, onRejected]).
    // That dispatch routes through GetV(self, "then") which sees user
    // overrides on Boolean.prototype / Number.prototype / String.prototype
    // / Symbol.prototype.then (test262 this-value-obj-coercible) — pre-fix
    // catch always called our native promiseThen, ignoring user overrides
    // and primitive receivers entirely.
    const proto::ProtoList* thenArgs = ctx->newList();
    thenArgs = thenArgs->appendLast(ctx, PROTO_NONE);
    thenArgs = thenArgs->appendLast(ctx, onRej);
    if (self && self != PROTO_NONE) {
        const proto::ProtoString* thenK = ctx->fromUTF8String("then")->asString(ctx);
        if (thenK) {
            const proto::ProtoObject* thenFn = self->getAttribute(ctx, thenK, true);
            // Prefer the user-visible "then" when it isn't our native one,
            // or when the receiver isn't a Promise at all (primitive case).
            bool useUserThen = thenFn && thenFn != PROTO_NONE
                && thenFn != getUndefinedSentinel()
                && !isPromise(ctx, self);
            if (useUserThen) {
                bool callable = thenFn->isMethod(ctx);
                const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                if (!callable && bcK && thenFn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
                const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                if (!callable && nfK && thenFn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
                if (callable) return callJSFunction(ctx, thenFn, self, thenArgs);
            }
        }
    }
    return promiseThen(ctx, self, pl, thenArgs, psl);
}

// ---------------------------------------------------------------------------
// promiseFinally — Promise.prototype.finally(onFinally)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseFinally(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // §27.2.5.3: 2. If Type(this value) is not Object, throw TypeError.
    if (!self || self == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Promise.prototype.finally called on non-object"));
        return PROTO_NONE;
    }
    // §27.2.5.3 step 3: SpeciesConstructor(promise, %Promise%).
    // SpeciesConstructor reads obj.constructor; if undefined return
    // %Promise%, else if not an Object throw TypeError.  Pre-fix
    // .finally accepted ANY constructor value, so the species check
    // never fired (built-ins/Promise/prototype/finally/species-
    // constructor-throws.js: SpeciesConstructor TypeError missing).
    {
        const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx);
        if (ctorKey) {
            const proto::ProtoObject* c = self->getAttribute(ctx, ctorKey, true);
            if (c && c != PROTO_NONE) {
                bool isPrim = c->isInteger(ctx) || c->isDouble(ctx) ||
                              c->isString(ctx) || c->isBoolean(ctx);
                if (isPrim) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "SpeciesConstructor: constructor is not an Object"));
                    return PROTO_NONE;
                }
            }
        }
    }
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* onFinally = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!onFinally) onFinally = PROTO_NONE;

    // §27.2.5.3 step 7: Return ? Invoke(promise, "then", «thenFinally,
    // catchFinally»).  Dispatch through Get(self, "then") so a user-
    // overridden `then` on the receiver / its prototype chain fires
    // (test262 finally/invokes-then-with-function, finally/
    // resolved-observable-then-calls).  Pre-fix the native fast path
    // ran the onFinally callback inline and skipped self.then entirely.
    // §27.2.5.3 step 7: ALWAYS go through Get(self, "then") so a
    // user-installed override on the receiver (even when the receiver
    // is a native Promise) fires per spec.
    const proto::ProtoString* thenK = ctx->fromUTF8String("then")->asString(ctx);
    const proto::ProtoObject* thenFn = thenK
        ? self->getAttribute(ctx, thenK, true) : PROTO_NONE;
    bool useUserThen = thenFn && thenFn != PROTO_NONE
        && thenFn != getUndefinedSentinel();
    if (useUserThen) {
        bool callable = thenFn->isMethod(ctx);
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (!callable && bcK && thenFn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && thenFn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
        if (callable) {
            const proto::ProtoList* cbArgs = ctx->newList();
            cbArgs = cbArgs->appendLast(ctx, onFinally);
            cbArgs = cbArgs->appendLast(ctx, onFinally);
            return callJSFunction(ctx, thenFn, self, cbArgs);
        }
    }

    int state = getPromiseState(ctx, self);
    const proto::ProtoObject* value = getPromiseValue(ctx, self);

    if (onFinally && onFinally != PROTO_NONE) {
        const proto::ProtoList* cbArgs = ctx->newList();
        callJSFunction(ctx, onFinally, PROTO_NONE, cbArgs);
    }

    return makeSettledPromise(ctx, state, value);
}

// §27.2 NewPromiseCapability(C): IsConstructor(C) → TypeError when false.
// Used by Promise.resolve / reject / all / allSettled / race / any
// before any further work runs.
//
// Presence probes go through hasAttribute, not getAttribute(...) !=
// PROTO_NONE — PROTO_NONE doubles as the "no value" return AND as a
// legitimate stored value, so the latter pattern false-negatives on
// attributes whose stored value is actually PROTO_NONE.
static bool throwIfNotPromiseConstructor(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* self) {
    // Reject the obvious non-constructors first: null / undefined /
    // primitives leave no [[Construct]] internal method.  PROTO_NONE
    // here means the static method was invoked without an explicit
    // receiver (e.g. via `.call(undefined)` or against a global stub
    // that resolved to PROTO_NONE such as `eval`).
    if (!self || self == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Promise.* invoked on non-constructor"));
        return true;
    }
    if (self->isInteger(ctx) || self->isDouble(ctx)
        || self->isBoolean(ctx) || self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Promise.* invoked on non-constructor"));
        return true;
    }
    // Callable / constructable objects accepted here.  The protoJS
    // Promise constructor itself is a plain object holding a
    // __construct__ method plus static helpers — it has no
    // __native_fn__ on the ctor object directly, so the probe must
    // cover that shape too.  JS user code that subclasses Promise
    // via `class X extends Promise` produces a bytecode closure
    // (__bytecode_id__) tagged __is_constructor__.
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    const proto::ProtoString* csK = JSSymbols::construct(ctx);
    bool callable = self->isMethod(ctx)
        || (nfK && self->hasAttribute(ctx, nfK) == PROTO_TRUE)
        || (bcK && self->hasAttribute(ctx, bcK) == PROTO_TRUE)
        || (csK && self->hasAttribute(ctx, csK) == PROTO_TRUE);
    if (!callable) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Promise.* invoked on non-constructor"));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Promise.resolve(value) — static method.
// If value is already a promise, return it as-is.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseStaticResolve(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (throwIfNotPromiseConstructor(ctx, self)) return PROTO_NONE;
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* val = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    if (isPromise(ctx, val)) return val;
    return makeSettledPromise(ctx, 1, val);
}

// ---------------------------------------------------------------------------
// Promise.try(callback, ...args) — ES2025 §27.2.4.7.
// Synchronously invoke callback with args; resolve with its return value or
// reject with any thrown exception.  Equivalent to
// `new Promise((res) => res(callback(...args)))` but skips the extra
// microtask hop and exposes thrown exceptions as a rejected Promise without
// requiring a try/catch in user code.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseStaticTry(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (throwIfNotPromiseConstructor(ctx, self)) return PROTO_NONE;
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* cb = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoList* fwd = ctx->newList();
    for (int i = 1; i < argc; i++) fwd = fwd->appendLast(ctx, args->getAt(ctx, i));
    const proto::ProtoObject* result = callJSFunction(ctx, cb, getUndefinedSentinel(), fwd);
    if (hasCallException()) {
        const proto::ProtoObject* err = consumeCallException();
        return makeSettledPromise(ctx, 2, err ? err : PROTO_NONE);
    }
    if (isPromise(ctx, result)) return result;
    return makeSettledPromise(ctx, 1, result ? result : PROTO_NONE);
}

// ---------------------------------------------------------------------------
// Promise.reject(reason) — static method.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseStaticReject(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (throwIfNotPromiseConstructor(ctx, self)) return PROTO_NONE;
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* reason = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!reason) reason = PROTO_NONE;
    return makeSettledPromise(ctx, 2, reason);
}

// ---------------------------------------------------------------------------
// collectIterable — extract array elements into a vector.
// ---------------------------------------------------------------------------
static std::vector<const proto::ProtoObject*> collectIterable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* iterable)
{
    std::vector<const proto::ProtoObject*> items;
    if (!iterable || iterable == PROTO_NONE) return items;

    // Fast path: arrays store elements in __elements__ (a ProtoList).
    // Pre-fix the loop probed for indexed string attributes only and
    // returned PROTO_NONE for every slot of a real array, which is
    // why Promise.allSettled / .race / .any silently turned their
    // input into an array of undefined values.
    if (const proto::ProtoList* els = getArrayElements(ctx, iterable)) {
        size_t sz = els->getSize(ctx);
        items.reserve(sz);
        for (size_t i = 0; i < sz; ++i) {
            const proto::ProtoObject* v = els->getAt(ctx, static_cast<int>(i));
            items.push_back(v ? v : PROTO_NONE);
        }
        return items;
    }

    // Legacy / array-like path: read `length` and walk indexed keys.
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return items;
    const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKey, false);
    long long len = 0;
    if (lenObj && lenObj != PROTO_NONE) {
        if (lenObj->isInteger(ctx))     len = lenObj->asLong(ctx);
        else if (lenObj->isDouble(ctx)) len = static_cast<long long>(lenObj->asDouble(ctx));
    }
    for (long long i = 0; i < len; i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        const proto::ProtoObject* v = ik ? iterable->getAttribute(ctx, ik, false) : PROTO_NONE;
        items.push_back(v ? v : PROTO_NONE);
    }
    return items;
}

// ---------------------------------------------------------------------------
// Promise.all(iterable)
// ---------------------------------------------------------------------------
// §27.2.4.1/4/5 GetIterator(iterable): iterable must be Object or string.
// Boolean / number / symbol / null / undefined → TypeError, which
// IfAbruptRejectPromise converts to a rejected promise.
static const proto::ProtoObject* rejectIfNotIterable(
    proto::ProtoContext* ctx, const proto::ProtoObject* iterable,
    const char* method)
{
    if (iterable == getNullSentinel() || iterable == getUndefinedSentinel()
        || iterable == PROTO_NONE
        || iterable->isBoolean(ctx) || iterable->isInteger(ctx)
        || iterable->isDouble(ctx)  || iterable->isFloat(ctx)) {
        std::string msg = std::string(method) + ": argument is not iterable";
        const proto::ProtoObject* err =
            makeNativeError(ctx, "TypeError", msg.c_str());
        return makeSettledPromise(ctx, 2, err);
    }
    const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
    if (symK && iterable->getAttribute(ctx, symK, true) == PROTO_TRUE) {
        const proto::ProtoObject* err =
            makeNativeError(ctx, "TypeError", "Symbol is not iterable");
        return makeSettledPromise(ctx, 2, err);
    }
    return nullptr;
}

static const proto::ProtoObject* promiseAll(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* iterable = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!iterable) iterable = PROTO_NONE;

    if (const proto::ProtoObject* rejected = rejectIfNotIterable(ctx, iterable, "Promise.all"))
        return rejected;

    auto items = collectIterable(ctx, iterable);

    const proto::ProtoObject* resultArr = ctx->newObject(true);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) resultArr = resultArr->setAttribute(ctx, isArrKey, PROTO_TRUE);

    for (size_t i = 0; i < items.size(); ++i) {
        const proto::ProtoObject* item = items[i];
        if (isPromise(ctx, item)) {
            int st = getPromiseState(ctx, item);
            if (st == 2) return makeSettledPromise(ctx, 2, getPromiseValue(ctx, item));
            if (st == 1) item = getPromiseValue(ctx, item);
            else         item = PROTO_NONE; // pending → undefined
        }
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (ik) resultArr = resultArr->setAttribute(ctx, ik, item ? item : PROTO_NONE);
    }
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) resultArr = resultArr->setAttribute(ctx, lenKey,
        ctx->fromInteger(static_cast<long long>(items.size())));
    return makeSettledPromise(ctx, 1, resultArr);
}

// ---------------------------------------------------------------------------
// Promise.allSettled(iterable)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseAllSettled(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* iterable = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!iterable) iterable = PROTO_NONE;

    if (const proto::ProtoObject* rejected = rejectIfNotIterable(ctx, iterable, "Promise.allSettled"))
        return rejected;

    auto items = collectIterable(ctx, iterable);

    const proto::ProtoObject* resultArr = ctx->newObject(true);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) resultArr = resultArr->setAttribute(ctx, isArrKey, PROTO_TRUE);

    for (size_t i = 0; i < items.size(); ++i) {
        const proto::ProtoObject* item = items[i];
        int st = 1;
        const proto::ProtoObject* val = item;
        if (isPromise(ctx, item)) {
            st  = getPromiseState(ctx, item);
            val = getPromiseValue(ctx, item);
            if (st == 0) { st = 1; val = PROTO_NONE; }
        }

        const proto::ProtoObject* desc = ctx->newObject(true);
        const proto::ProtoObject* statusKeyObj = ctx->fromUTF8String("status");
        const proto::ProtoString* sks = statusKeyObj ? statusKeyObj->asString(ctx) : nullptr;
        if (sks) desc = desc->setAttribute(ctx, sks,
            ctx->fromUTF8String(st == 1 ? "fulfilled" : "rejected"));

        if (st == 1) {
            desc = setAttr(ctx, desc, "value", val);
        } else {
            desc = setAttr(ctx, desc, "reason", val);
        }

        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (ik) resultArr = resultArr->setAttribute(ctx, ik, desc);
    }
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) resultArr = resultArr->setAttribute(ctx, lenKey,
        ctx->fromInteger(static_cast<long long>(items.size())));
    return makeSettledPromise(ctx, 1, resultArr);
}

// ---------------------------------------------------------------------------
// Promise.race(iterable)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseRace(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* iterable = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!iterable) iterable = PROTO_NONE;

    if (const proto::ProtoObject* rejected = rejectIfNotIterable(ctx, iterable, "Promise.race"))
        return rejected;

    auto items = collectIterable(ctx, iterable);
    for (const auto& item : items) {
        if (isPromise(ctx, item)) {
            int st = getPromiseState(ctx, item);
            if (st != 0) return item;
        } else {
            return makeSettledPromise(ctx, 1, item);
        }
    }
    // All pending or empty: return pending promise.
    return makeSettledPromise(ctx, 0, PROTO_NONE);
}

// ---------------------------------------------------------------------------
// Promise.any(iterable)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseAny(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* iterable = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!iterable) iterable = PROTO_NONE;

    if (const proto::ProtoObject* rejected = rejectIfNotIterable(ctx, iterable, "Promise.any"))
        return rejected;

    auto items = collectIterable(ctx, iterable);

    const proto::ProtoObject* errorsArr = ctx->newObject(true);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) errorsArr = errorsArr->setAttribute(ctx, isArrKey, PROTO_TRUE);

    bool allSettledAndRejected = true;
    for (size_t i = 0; i < items.size(); ++i) {
        const proto::ProtoObject* item = items[i];
        if (isPromise(ctx, item)) {
            int st = getPromiseState(ctx, item);
            if (st == 1) return makeSettledPromise(ctx, 1, getPromiseValue(ctx, item));
            if (st == 2) {
                const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                if (ik) errorsArr = errorsArr->setAttribute(ctx, ik, getPromiseValue(ctx, item));
            } else {
                allSettledAndRejected = false;
            }
        } else {
            // Non-promise values fulfill immediately.
            return makeSettledPromise(ctx, 1, item);
        }
    }

    if (items.empty() || allSettledAndRejected) {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) errorsArr = errorsArr->setAttribute(ctx, lenKey,
            ctx->fromInteger(static_cast<long long>(items.size())));

        const proto::ProtoObject* aggErr = ctx->newObject(true);
        aggErr = setAttr(ctx, aggErr, "message", ctx->fromUTF8String("All promises were rejected"));
        aggErr = setAttr(ctx, aggErr, "errors",  errorsArr);
        aggErr = setAttr(ctx, aggErr, "name",    ctx->fromUTF8String("AggregateError"));
        return makeSettledPromise(ctx, 2, aggErr);
    }

    return makeSettledPromise(ctx, 0, PROTO_NONE);
}

// ---------------------------------------------------------------------------
// Promise constructor — Promise(executor)
//
// Uses a thread-local stack to pass the "cell key" to the synchronous
// resolve/reject callbacks. This correctly handles nested Promise construction.
//
// The cell (pending promise) is registered in the global scope under a unique
// key so resolve/reject can mutate it via the global root pointer.
// ---------------------------------------------------------------------------

static thread_local unsigned long long t_promiseCellNext{1};
static thread_local std::vector<std::string> t_activeCellKeyStack;

static const proto::ProtoObject* promiseResolveNative(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (t_activeCellKeyStack.empty()) return PROTO_NONE;
    const std::string& cellKey = t_activeCellKeyStack.back();
    const proto::ProtoObject* ckStr = ctx->fromUTF8String(cellKey.c_str());
    const proto::ProtoString* cks = ckStr ? ckStr->asString(ctx) : nullptr;
    if (!cks) return PROTO_NONE;

    const proto::ProtoObject** gr = getCurrentGlobalRoot();
    if (!gr || !*gr) return PROTO_NONE;

    const proto::ProtoObject* cell = (*gr)->getAttribute(ctx, cks, false);
    if (!cell || cell == PROTO_NONE) return PROTO_NONE;
    if (getPromiseState(ctx, cell) != 0) return PROTO_NONE; // already settled

    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* val = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;

    cell = setAttr(ctx, cell, "__promise_state__", ctx->fromInteger(1LL));
    cell = setAttr(ctx, cell, "__promise_value__", val);
    *gr = (*gr)->setAttribute(ctx, cks, cell);
    return PROTO_NONE;
}

static const proto::ProtoObject* promiseRejectNative(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (t_activeCellKeyStack.empty()) return PROTO_NONE;
    const std::string& cellKey = t_activeCellKeyStack.back();
    const proto::ProtoObject* ckStr = ctx->fromUTF8String(cellKey.c_str());
    const proto::ProtoString* cks = ckStr ? ckStr->asString(ctx) : nullptr;
    if (!cks) return PROTO_NONE;

    const proto::ProtoObject** gr = getCurrentGlobalRoot();
    if (!gr || !*gr) return PROTO_NONE;

    const proto::ProtoObject* cell = (*gr)->getAttribute(ctx, cks, false);
    if (!cell || cell == PROTO_NONE) return PROTO_NONE;
    if (getPromiseState(ctx, cell) != 0) return PROTO_NONE;

    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* reason = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!reason) reason = PROTO_NONE;

    cell = setAttr(ctx, cell, "__promise_state__", ctx->fromInteger(2LL));
    cell = setAttr(ctx, cell, "__promise_value__", reason);
    *gr = (*gr)->setAttribute(ctx, cks, cell);
    return PROTO_NONE;
}

static const proto::ProtoObject* promiseConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* executor = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!executor) executor = PROTO_NONE;

    // ECMA-262 §27.2.3.1: Promise(executor) must throw TypeError when executor
    // is not callable (including the no-arg case where executor is undefined).
    auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE) return false;
        if (fn->isMethod(ctx)) return true;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return true;
        return false;
    };
    if (!isCallable(executor)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Promise resolver is not a function"));
        return PROTO_NONE;
    }

    // Generate a unique cell key.
    unsigned long long cellId = t_promiseCellNext++;
    char keyBuf[64];
    std::snprintf(keyBuf, sizeof(keyBuf), "__promise_cell_%llu__", cellId);

    // Use the pre-allocated `self` when OP_call_constructor prepared it
    // (so `new SubPromise(...)` honours [[Prototype]] = SubPromise.prototype
    // and `sub instanceof SubPromise` is true). Fall back to a fresh
    // object for direct `new Promise(...)` calls that arrive with self=PROTO_NONE.
    const proto::ProtoObject* cell = (self && self != PROTO_NONE)
        ? self : ctx->newObject(true);
    cell = setAttr(ctx, cell, "__promise_state__", ctx->fromInteger(0LL));
    cell = setAttr(ctx, cell, "__promise_value__", PROTO_NONE);
    cell = attachPromiseMethods(ctx, cell);

    // Register cell in global scope so resolve/reject can update it.
    const proto::ProtoObject** gr = getCurrentGlobalRoot();
    const proto::ProtoObject* ckStr = ctx->fromUTF8String(keyBuf);
    const proto::ProtoString* cks = ckStr ? ckStr->asString(ctx) : nullptr;
    if (gr && *gr && cks) *gr = (*gr)->setAttribute(ctx, cks, cell);

    // Push cell key onto stack so resolve/reject find it.
    t_activeCellKeyStack.push_back(keyBuf);

    // §27.2.4.1 CreateResolvingFunctions: spec mandates the resolve
    // and reject functions delivered to the executor have length=1
    // and name="".  Pre-fix the raw ProtoMethods carried no .length
    // attribute, so the spec-shape check
    // (built-ins/Promise/exec-args.js asserts resolve.length === 1)
    // failed.  Wrap each raw method in a Function.prototype-rooted
    // shell that carries the spec descriptor sidecars.
    auto wrapResolverFn = [&](proto::ProtoMethod fn) -> const proto::ProtoObject* {
        const proto::ProtoObject* fp = ctx->space
            ? ctx->space->methodPrototype : nullptr;
        const proto::ProtoObject* wrap = (fp && fp != PROTO_NONE)
            ? fp->newChild(ctx, true) : ctx->newObject(true);
        if (!wrap) return ctx->fromMethod(nullptr, fn);
        const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
        if (nfk) wrap = wrap->setAttribute(ctx, nfk,
            ctx->fromMethod(nullptr, fn));
        const proto::ProtoString* lenK = JSSymbols::length(ctx);
        if (lenK) {
            wrap = wrap->setAttribute(ctx, lenK, ctx->fromInteger(1LL));
            const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
            if (pdlk) wrap = wrap->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmK = JSSymbols::name(ctx);
        if (nmK) {
            wrap = wrap->setAttribute(ctx, nmK, ctx->fromUTF8String(""));
            const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
            if (pdnk) wrap = wrap->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
        if (hnwK) wrap = wrap->setAttribute(ctx, hnwK, PROTO_TRUE);
        return wrap;
    };
    const proto::ProtoObject* resolveFn = wrapResolverFn(promiseResolveNative);
    const proto::ProtoObject* rejectFn  = wrapResolverFn(promiseRejectNative);

    // Call executor(resolve, reject) synchronously.
    if (executor && executor != PROTO_NONE) {
        const proto::ProtoList* execArgs = ctx->newList();
        execArgs = execArgs->appendLast(ctx, resolveFn ? resolveFn : PROTO_NONE);
        execArgs = execArgs->appendLast(ctx, rejectFn  ? rejectFn  : PROTO_NONE);
        callJSFunction(ctx, executor, PROTO_NONE, execArgs);
    }

    // Pop cell key.
    t_activeCellKeyStack.pop_back();

    // Read the settled state from the global cell.
    if (gr && *gr && cks) {
        const proto::ProtoObject* updated = (*gr)->getAttribute(ctx, cks, false);
        if (updated && updated != PROTO_NONE) cell = updated;
    }

    return cell;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ensurePromiseConstructor — register Promise in globalRoot.
// ---------------------------------------------------------------------------
void ensurePromiseConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    // \xc2\xa727.2.3 / \xc2\xa720.2.3: Promise's [[Prototype]] is
    // %Function.prototype% so Object.getPrototypeOf(Promise) ===
    // Function.prototype and Promise.call / apply / bind resolve
    // through the inherited methods.  Pre-fix ctx->newObject(true)
    // produced a parentless ctor and the JS-visible chain reported
    // the fallback objectPrototype (Date / RegExp had the same shape
    // before the R49 fixes; same pattern applies here).
    const proto::ProtoObject* methodProto =
        (ctx->space && ctx->space->methodPrototype)
            ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = methodProto
        ? methodProto->newChild(ctx, true)
        : ctx->newObject(true);
    if (methodProto) setJSProtoOverride(ctx, ctor, methodProto);

    // Raw __construct__ stays a bare ProtoMethod — OP_call_constructor
    // dispatches it directly, never reading name / length.
    auto regRaw = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (ks) ctor = ctor->setAttribute(ctx, ks, ctx->fromMethod(nullptr, fn));
    };
    // Static methods (Promise.resolve / .reject / .all / .race / .any /
    // .allSettled) are visible JS-side via verifyProperty(Promise.<m>,
    // "name", ...).  §17 mandates name (writable:false, enumerable:false,
    // configurable:true, descriptor 0x2) and length matching the spec
    // arity.  Pre-fix regStatic installed the bare ProtoMethod whose
    // length=0 with no descriptor sidecar (built-ins/Promise/all/name).
    auto regStatic = [&](const char* name, proto::ProtoMethod fn, long long arity) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) return;
        // Build the same mutable-wrapper shape that
        // installNonEnumerableMethod uses for prototype methods.  A bare
        // ProtoMethod cell can't carry attributes (setAttribute on it
        // does not persist), so the name / length descriptors live on
        // a child of methodPrototype with __native_fn__ pointing to the
        // raw method.
        const proto::ProtoObject* wrapper = ctx->space && ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        if (!wrapper) return;
        const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
        if (nfk) wrapper = wrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
        const proto::ProtoString* lk = JSSymbols::length(ctx);
        if (lk) {
            wrapper = wrapper->setAttribute(ctx, lk, ctx->fromInteger(arity));
            const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nk = JSSymbols::name(ctx);
        if (nk) {
            wrapper = wrapper->setAttribute(ctx, nk, ctx->fromUTF8String(name));
            const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        ctor = ctor->setAttribute(ctx, ks, wrapper);
        std::string pdStr = std::string("__pd_") + name + "__";
        const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    };

    regRaw   ("__construct__", promiseConstructor);
    regStatic("resolve",       promiseStaticResolve, 1);
    regStatic("reject",        promiseStaticReject,  1);
    regStatic("all",           promiseAll,           1);
    regStatic("allSettled",    promiseAllSettled,    1);
    regStatic("race",          promiseRace,          1);
    regStatic("any",           promiseAny,           1);
    regStatic("try",           promiseStaticTry,     1);

    // §27.2.4.5 get Promise[@@species]: a getter returning `this`,
    // with descriptor {enumerable:false, configurable:true} → 0x2.
    // The getter itself has length 0 and name "get [Symbol.species]"
    // ({writable:false, enumerable:false, configurable:true} → 0x2).
    // Pre-fix Promise had no species property at all, so the test262
    // probe Promise[Symbol.species] surfaced undefined.  Mirrors the
    // Set / Map / Array install pattern.
    {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* getter = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (getter) {
                static const auto promiseSpeciesGetter = [](
                    proto::ProtoContext* /*ctx*/, const proto::ProtoObject* self,
                    const proto::ParentLink*,
                    const proto::ProtoList*, const proto::ProtoSparseList*)
                    -> const proto::ProtoObject* { return self; };
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, promiseSpeciesGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get [Symbol.species]"));
                    const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                // Stamp the gating flag so name + length on the getter
                // are actually read-only (Round 12/13 sweep pattern).
                const proto::ProtoString* hnwSp = JSSymbols::hasNonWritableProps(ctx);
                if (hnwSp) getter = getter->setAttribute(ctx, hnwSp, PROTO_TRUE);
                const proto::ProtoString* gksSym =
                    ctx->fromUTF8String("__get_Symbol.species__")->asString(ctx);
                if (gksSym) ctor = ctor->setAttribute(ctx, gksSym, getter);
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.species__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            }
        }
    }

    // Promise.prototype.
    const proto::ProtoObject* proto = ctx->newObject(true);
    auto regProto = [&](const char* name, proto::ProtoMethod fn, long long arity) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) return;
        // §17 + §27.2.5.{4,5,6}: the prototype methods are
        // {writable:true, enumerable:false, configurable:true} and
        // each method's length matches the spec arity.  Bare
        // ProtoMethod cells cannot carry name / length attribute
        // sidecars (setAttribute on a raw method does not persist),
        // so wrap the method in a mutable child of methodPrototype
        // and install the descriptors on the wrapper — matching the
        // installNonEnumerableMethod shape used by every other
        // built-in prototype method.  Pre-fix Promise.prototype.
        // {then,catch,finally}.name surfaced as undefined.
        const proto::ProtoObject* wrapper = ctx->space && ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        if (wrapper) {
            const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
            if (nfk) wrapper = wrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            if (lk) {
                wrapper = wrapper->setAttribute(ctx, lk, ctx->fromInteger(arity));
                const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
                if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
            }
            const proto::ProtoString* nk = JSSymbols::name(ctx);
            if (nk) {
                wrapper = wrapper->setAttribute(ctx, nk, ctx->fromUTF8String(name));
                const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
                if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
            }
        }
        proto = proto->setAttribute(ctx, ks, wrapper);
        std::string pdStr = std::string("__pd_") + name + "__";
        const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    };
    regProto("then",    promiseThen,    2);
    regProto("catch",   promiseCatch,   1);
    regProto("finally", promiseFinally, 1);

    // Promise.prototype[@@toStringTag] === "Promise" per §27.2.5.5.
    // Install under both the internal sidecar (used by
    // Object.prototype.toString's tag probe) and the user-visible
    // 'Symbol.toStringTag' key (what `Promise.prototype[Symbol.toStringTag]`
    // resolves to), each with §17 descriptor 0x2.
    {
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey) proto = proto->setAttribute(ctx, tagKey,
            ctx->fromUTF8String("Promise"));
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            proto = proto->setAttribute(ctx, userKey, ctx->fromUTF8String("Promise"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) proto = proto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
            const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
            if (hnwK) proto = proto->setAttribute(ctx, hnwK, PROTO_TRUE);
        }
    }

    // Promise.prototype.constructor === Promise per §27.2.5.2.
    // Non-enumerable: __pd_constructor__ = 0x3 (writable+configurable).
    {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            proto = proto->setAttribute(ctx, ctorWordKey, ctor);
            const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
            if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
        }
    }

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) {
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        // §27.2.3.1 / §17: Promise.prototype descriptor bits 0x0.
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Promise"));
        // Per §17 every built-in's .name carries §17 descriptor 0x2
        // (writable:false, enumerable:false, configurable:true). Pre-fix
        // the absent descriptor sidecar defaulted to all-true, which
        // breaks Object.keys(Promise) checks and verifyProperty.
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }
    // §27.2.3: Promise.length === 1 (executor formal-parameter count),
    // descriptor non-enumerable + non-writable + configurable (0x2).
    {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
            const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
            if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
        }
    }
    // Hot-path hint mirroring the constructor sweep this round.
    {
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) ctor = ctor->setAttribute(ctx, hnw, PROTO_TRUE);
    }

    const proto::ProtoObject* promiseKeyObj = ctx->fromUTF8String("Promise");
    const proto::ProtoString* pk = promiseKeyObj ? promiseKeyObj->asString(ctx) : nullptr;
    if (pk) {
        *globalRoot = (*globalRoot)->setAttribute(ctx, pk, ctor);
        // §17 globalThis.Promise descriptor 0x3.
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Promise__");
        const proto::ProtoString* pdkk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdkk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdkk,
            ctx->fromInteger(0x3LL));
    }
}

// ---------------------------------------------------------------------------
// Public helpers used by the interpreter for OP_return_async / OP_await.
// ---------------------------------------------------------------------------
const proto::ProtoObject* makeResolvedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* value)
{
    return makeSettledPromise(ctx, 1, value);
}

const proto::ProtoObject* makeRejectedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* reason)
{
    return makeSettledPromise(ctx, 2, reason);
}

bool isPromiseObject(proto::ProtoContext* ctx, const proto::ProtoObject* obj)
{
    return isPromise(ctx, obj);
}

int getPromiseStatePublic(proto::ProtoContext* ctx, const proto::ProtoObject* p)
{
    return getPromiseState(ctx, p);
}

const proto::ProtoObject* getPromiseValuePublic(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* p)
{
    return getPromiseValue(ctx, p);
}

} // namespace protojs
