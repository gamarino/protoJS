#include "PromisePrototype.h"
#include "JSSymbols.h"
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
            const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmk = JSSymbols::name(ctx);
        if (nmk) {
            wrapper = wrapper->setAttribute(ctx, nmk, ctx->fromUTF8String(name));
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
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
    if (!self || self == PROTO_NONE || !isPromise(ctx, self))
        return makeSettledPromise(ctx, 2, PROTO_NONE);

    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* onFulfilled = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* onRejected  = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!onFulfilled) onFulfilled = PROTO_NONE;
    if (!onRejected)  onRejected  = PROTO_NONE;

    int state = getPromiseState(ctx, self);
    const proto::ProtoObject* value = getPromiseValue(ctx, self);

    if (state == 1) {
        // Fulfilled: invoke onFulfilled.
        if (onFulfilled && onFulfilled != PROTO_NONE) {
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
        // Rejected: invoke onRejected.
        if (onRejected && onRejected != PROTO_NONE) {
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

    const proto::ProtoList* thenArgs = ctx->newList();
    thenArgs = thenArgs->appendLast(ctx, PROTO_NONE);
    thenArgs = thenArgs->appendLast(ctx, onRej);
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
    if (!self || self == PROTO_NONE) return makeSettledPromise(ctx, 2, PROTO_NONE);
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* onFinally = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!onFinally) onFinally = PROTO_NONE;

    int state = getPromiseState(ctx, self);
    const proto::ProtoObject* value = getPromiseValue(ctx, self);

    if (onFinally && onFinally != PROTO_NONE) {
        const proto::ProtoList* cbArgs = ctx->newList();
        callJSFunction(ctx, onFinally, PROTO_NONE, cbArgs);
    }

    return makeSettledPromise(ctx, state, value);
}

// ---------------------------------------------------------------------------
// Promise.resolve(value) — static method.
// If value is already a promise, return it as-is.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseStaticResolve(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* val = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    if (isPromise(ctx, val)) return val;
    return makeSettledPromise(ctx, 1, val);
}

// ---------------------------------------------------------------------------
// Promise.reject(reason) — static method.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* promiseStaticReject(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
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
        if (bcKey && fn->getAttribute(ctx, bcKey, false) != PROTO_NONE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->getAttribute(ctx, nfKey, false) != PROTO_NONE) return true;
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

    // Build resolve and reject functions.
    const proto::ProtoObject* resolveFn = ctx->fromMethod(nullptr, promiseResolveNative);
    const proto::ProtoObject* rejectFn  = ctx->fromMethod(nullptr, promiseRejectNative);

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

    // Build constructor object with static methods.
    const proto::ProtoObject* ctor = ctx->newObject(true);

    auto reg = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (ks) ctor = ctor->setAttribute(ctx, ks, ctx->fromMethod(nullptr, fn));
    };

    // The constructor itself is stored as __construct__ so OP_call_constructor
    // can invoke it via the normal constructor-dispatch mechanism.
    reg("__construct__", promiseConstructor);
    reg("resolve",       promiseStaticResolve);
    reg("reject",        promiseStaticReject);
    reg("all",           promiseAll);
    reg("allSettled",    promiseAllSettled);
    reg("race",          promiseRace);
    reg("any",           promiseAny);

    // Promise.prototype.
    const proto::ProtoObject* proto = ctx->newObject(true);
    auto regProto = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (ks) proto = proto->setAttribute(ctx, ks, ctx->fromMethod(nullptr, fn));
    };
    regProto("then",    promiseThen);
    regProto("catch",   promiseCatch);
    regProto("finally", promiseFinally);

    // Promise.prototype[@@toStringTag] === "Promise" per §27.2.5.5
    // so Object.prototype.toString.call(promise) returns
    // "[object Promise]".
    {
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey) proto = proto->setAttribute(ctx, tagKey,
            ctx->fromUTF8String("Promise"));
    }

    // Promise.prototype.constructor === Promise per §27.2.5.2.
    // Non-enumerable: __pd_constructor__ = 0x3 (writable+configurable).
    {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            proto = proto->setAttribute(ctx, ctorWordKey, ctor);
            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_constructor__");
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
        }
    }

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Promise"));

    const proto::ProtoObject* promiseKeyObj = ctx->fromUTF8String("Promise");
    const proto::ProtoString* pk = promiseKeyObj ? promiseKeyObj->asString(ctx) : nullptr;
    if (pk) *globalRoot = (*globalRoot)->setAttribute(ctx, pk, ctor);
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
