#include "IteratorPrototype.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
#include <limits>

namespace protojs {

static const proto::ProtoObject* iteratorReturnSelf(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return self;
}

// ---------------------------------------------------------------------------
// Helpers shared by every Iterator helper method
// ---------------------------------------------------------------------------

static bool iterRequireObject(proto::ProtoContext* ctx,
                               const proto::ProtoObject* self,
                               const char* name) {
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()
        || self->isInteger(ctx) || self->isDouble(ctx)
        || self->isFloat(ctx) || self->isString(ctx)
        || self->isBoolean(ctx)) {
        std::string msg = "Iterator.prototype.";
        msg += name;
        msg += " called on non-object";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return false;
    }
    return true;
}

// Look up an iterator's `next` and verify callable.
static const proto::ProtoObject* iterGetNext(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* iter,
                                              const char* name) {
    const proto::ProtoObject* nextKo = ctx->fromUTF8String("next");
    const proto::ProtoString* nextK = nextKo ? nextKo->asString(ctx) : nullptr;
    if (!nextK) return nullptr;
    const proto::ProtoObject* fn = iter->getAttribute(ctx, nextK, true);
    // Accessor descriptor probe — the data slot is the undefined
    // sentinel while __get_next__ holds the getter.  GetMethod fires
    // the getter and uses its return value (test262 this-plain-
    // iterator.js installs `get next() { return function(){...}; }`).
    if (!fn || fn == PROTO_NONE || fn == getUndefinedSentinel() || fn == getNullSentinel()) {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_next__");
        const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
        if (gk) {
            const proto::ProtoObject* getter = iter->getAttribute(ctx, gk, true);
            if (getter && getter != PROTO_NONE && getter != getUndefinedSentinel()) {
                fn = callJSFunction(ctx, getter, iter, ctx->newList());
                if (hasCallException()) return nullptr;
            }
        }
    }
    if (!fn || fn == PROTO_NONE || fn == getUndefinedSentinel() || fn == getNullSentinel()) {
        std::string msg = "Iterator.prototype.";
        msg += name;
        msg += ": this.next is not callable";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return nullptr;
    }
    bool callable = fn->isMethod(ctx);
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (!callable && bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (!callable && bfK && fn->hasAttribute(ctx, bfK) == PROTO_TRUE) callable = true;
    if (!callable) {
        std::string msg = "Iterator.prototype.";
        msg += name;
        msg += ": this.next is not callable";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return nullptr;
    }
    return fn;
}

// Step once: returns the {value, done} object, or nullptr on abrupt.
// Sets *outDone = true when the iterator is exhausted.
static const proto::ProtoObject* iterStep(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* iter,
                                           const proto::ProtoObject* nextFn,
                                           bool* outDone,
                                           const proto::ProtoObject** outValue) {
    *outDone = true;
    if (outValue) *outValue = PROTO_NONE;
    const proto::ProtoObject* step = callJSFunction(ctx, nextFn, iter, ctx->newList());
    if (hasCallException()) return nullptr;
    if (!step || step == PROTO_NONE) return nullptr;
    // Result must be an Object.
    if (step == getNullSentinel() || step == getUndefinedSentinel()
        || step->isInteger(ctx) || step->isDouble(ctx)
        || step->isFloat(ctx) || step->isString(ctx)
        || step->isBoolean(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator result is not an Object"));
        return nullptr;
    }
    const proto::ProtoString* doneK = JSSymbols::done(ctx);
    const proto::ProtoObject* dv = doneK ? step->getAttribute(ctx, doneK, true) : nullptr;
    if (hasCallException()) return nullptr;
    // Accessor descriptor probe — if `done` is defined as a getter the
    // data slot reads PROTO_NONE/undefined; __get_done__ holds the getter
    // and we must call it so an abrupt completion in the getter
    // propagates (test262 next-method-returns-throwing-done.js).
    if (!dv || dv == PROTO_NONE || dv == getUndefinedSentinel()) {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_done__");
        const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
        if (gk) {
            const proto::ProtoObject* getter = step->getAttribute(ctx, gk, true);
            if (getter && getter != PROTO_NONE && getter != getUndefinedSentinel()
                && getter != getNullSentinel()) {
                dv = callJSFunction(ctx, getter, step, ctx->newList());
                if (hasCallException()) return nullptr;
            }
        }
    }
    bool isDone = (dv == PROTO_TRUE);
    if (!isDone && dv && dv != PROTO_NONE && dv != PROTO_FALSE) {
        // ToBoolean of any truthy non-False value.
        if (dv->isBoolean(ctx)) isDone = dv->asBoolean(ctx);
        else if (dv->isInteger(ctx)) isDone = dv->asLong(ctx) != 0;
        else if (dv->isDouble(ctx)) { double d = dv->asDouble(ctx); isDone = d != 0.0 && d == d; }
        else if (dv == getNullSentinel() || dv == getUndefinedSentinel()) isDone = false;
        else isDone = true;  // any other object is truthy
    }
    *outDone = isDone;
    if (!isDone) {
        const proto::ProtoString* valueK = JSSymbols::value(ctx);
        const proto::ProtoObject* v = valueK ? step->getAttribute(ctx, valueK, true) : PROTO_NONE;
        if (hasCallException()) return nullptr;
        // Accessor descriptor probe for `value` — same rationale as for
        // `done` above (test262 next-method-returns-throwing-value.js).
        if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) {
            const proto::ProtoObject* gko = ctx->fromUTF8String("__get_value__");
            const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
            if (gk) {
                const proto::ProtoObject* getter = step->getAttribute(ctx, gk, true);
                if (getter && getter != PROTO_NONE && getter != getUndefinedSentinel()
                    && getter != getNullSentinel()) {
                    v = callJSFunction(ctx, getter, step, ctx->newList());
                    if (hasCallException()) return nullptr;
                }
            }
        }
        if (outValue) *outValue = v ? v : PROTO_NONE;
    }
    return step;
}

// Call `return` on the iterator if present (for abrupt-completion close).
// Errors thrown by .return are swallowed per spec when called for cleanup.
static void iterClose(proto::ProtoContext* ctx,
                       const proto::ProtoObject* iter) {
    if (!iter || iter == PROTO_NONE) return;
    const proto::ProtoObject* retKo = ctx->fromUTF8String("return");
    const proto::ProtoString* retK = retKo ? retKo->asString(ctx) : nullptr;
    if (!retK) return;
    const proto::ProtoObject* fn = iter->getAttribute(ctx, retK, true);
    if (!fn || fn == PROTO_NONE || fn == getUndefinedSentinel() || fn == getNullSentinel()) return;
    bool callable = fn->isMethod(ctx);
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (!callable && bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    if (!callable) return;
    bool hadEx = hasCallException();
    const proto::ProtoObject* savedEx = hadEx ? consumeCallException() : nullptr;
    (void)callJSFunction(ctx, fn, iter, ctx->newList());
    // Discard the abrupt — spec swallows it during cleanup.
    if (hasCallException()) consumeCallException();
    if (hadEx) {
        signalNativeException(savedEx);
    }
}

// ---------------------------------------------------------------------------
// Iterator.prototype.toArray()
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorToArray(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "toArray")) return PROTO_NONE;
    const proto::ProtoObject* nextFn = iterGetNext(ctx, self, "toArray");
    if (!nextFn) return PROTO_NONE;
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
    if (isArrK) arr = arr->setAttribute(ctx, isArrK, PROTO_TRUE);
    const proto::ProtoList* els = ctx->newList();
    long long count = 0;
    for (;;) {
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, self, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) break;
        els = els->appendLast(ctx, val ? val : PROTO_NONE);
        count++;
    }
    setArrayElements(ctx, arr, els);
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    if (lenK) arr = arr->setAttribute(ctx, lenK, ctx->fromInteger(count));
    return arr;
}

// ---------------------------------------------------------------------------
// Iterator.prototype.forEach(fn)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorForEach(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "forEach")) return PROTO_NONE;
    const proto::ProtoObject* fn = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    bool callable = fn && fn != PROTO_NONE && fn->isMethod(ctx);
    if (!callable && fn && fn != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    }
    if (!callable) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.forEach: callback is not callable"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* nextFn = iterGetNext(ctx, self, "forEach");
    if (!nextFn) return PROTO_NONE;
    long long counter = 0;
    for (;;) {
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, self, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) break;
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, val ? val : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(counter++));
        (void)callJSFunction(ctx, fn, getUndefinedSentinel(), cbArgs);
        if (hasCallException()) { iterClose(ctx, self); return PROTO_NONE; }
    }
    return getUndefinedSentinel();
}

// ---------------------------------------------------------------------------
// Iterator.prototype.reduce(fn[, initialValue])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorReduce(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "reduce")) return PROTO_NONE;
    const proto::ProtoObject* fn = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    bool callable = fn && fn != PROTO_NONE && fn->isMethod(ctx);
    if (!callable && fn && fn != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    }
    if (!callable) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.reduce: callback is not callable"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* nextFn = iterGetNext(ctx, self, "reduce");
    if (!nextFn) return PROTO_NONE;
    const proto::ProtoObject* acc = nullptr;
    bool hasAcc = (args && args->getSize(ctx) > 1);
    if (hasAcc) acc = args->getAt(ctx, 1);
    long long counter = 0;
    for (;;) {
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, self, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) break;
        if (!hasAcc) {
            acc = val;
            hasAcc = true;
            counter++;
            continue;
        }
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, acc ? acc : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, val ? val : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(counter++));
        acc = callJSFunction(ctx, fn, getUndefinedSentinel(), cbArgs);
        if (hasCallException()) { iterClose(ctx, self); return PROTO_NONE; }
    }
    if (!hasAcc) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.reduce: empty iterator with no initial value"));
        return PROTO_NONE;
    }
    return acc ? acc : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Iterator.prototype.every(fn) / some(fn) / find(fn)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorBoolPredicate(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ProtoList* args, const char* name, int mode)
{
    // mode: 0=every, 1=some, 2=find
    if (!iterRequireObject(ctx, self, name)) return PROTO_NONE;
    const proto::ProtoObject* fn = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    bool callable = fn && fn != PROTO_NONE && fn->isMethod(ctx);
    if (!callable && fn && fn != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    }
    if (!callable) {
        std::string msg = "Iterator.prototype.";
        msg += name;
        msg += ": callback is not callable";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return PROTO_NONE;
    }
    const proto::ProtoObject* nextFn = iterGetNext(ctx, self, name);
    if (!nextFn) return PROTO_NONE;
    long long counter = 0;
    for (;;) {
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, self, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) {
            if (mode == 0) return PROTO_TRUE;      // every → true on empty
            if (mode == 1) return PROTO_FALSE;     // some → false on empty
            return getUndefinedSentinel();          // find → undefined on no match
        }
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, val ? val : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(counter++));
        const proto::ProtoObject* r = callJSFunction(ctx, fn, getUndefinedSentinel(), cbArgs);
        if (hasCallException()) { iterClose(ctx, self); return PROTO_NONE; }
        // ToBoolean(r)
        bool truthy = !(r == nullptr || r == PROTO_NONE || r == PROTO_FALSE
            || r == getUndefinedSentinel() || r == getNullSentinel()
            || (r->isInteger(ctx) && r->asLong(ctx) == 0)
            || (r->isDouble(ctx) && (r->asDouble(ctx) == 0.0 || r->asDouble(ctx) != r->asDouble(ctx))));
        if (mode == 0 && !truthy) { iterClose(ctx, self); return PROTO_FALSE; }
        if (mode == 1 && truthy)  { iterClose(ctx, self); return PROTO_TRUE; }
        if (mode == 2 && truthy)  { iterClose(ctx, self); return val ? val : PROTO_NONE; }
    }
}

static const proto::ProtoObject* iteratorEvery(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{ return iteratorBoolPredicate(ctx, self, args, "every", 0); }

static const proto::ProtoObject* iteratorSome(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{ return iteratorBoolPredicate(ctx, self, args, "some", 1); }

static const proto::ProtoObject* iteratorFind(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{ return iteratorBoolPredicate(ctx, self, args, "find", 2); }

// ---------------------------------------------------------------------------
// Helper: build a wrapper-style "result iterator" carrying a stashed
// underlying iterator + the per-helper callback / count / state.
// Stored under unique attribute keys so .next() can recover them.
// ---------------------------------------------------------------------------
// Forward declarations — used by makeHelperIterator below.
static const proto::ProtoObject* iteratorHelperReturn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*);

static const proto::ProtoObject* makeHelperIterator(
    proto::ProtoContext* ctx,
    proto::ProtoMethod nextMethod,
    const proto::ProtoObject* underlying,
    const proto::ProtoObject* callback,
    long long initialCounter)
{
    const proto::ProtoObject* iterProto = getIteratorPrototype(ctx);
    const proto::ProtoObject* it = iterProto
        ? iterProto->newChild(ctx, true) : ctx->newObject(true);
    if (!it) return PROTO_NONE;
    // Stash internal state.
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    if (uK) it = it->setAttribute(ctx, uK, underlying ? underlying : PROTO_NONE);
    const proto::ProtoObject* cKo = ctx->fromUTF8String("__helper_cb__");
    const proto::ProtoString* cK = cKo ? cKo->asString(ctx) : nullptr;
    if (cK) it = it->setAttribute(ctx, cK, callback ? callback : PROTO_NONE);
    const proto::ProtoObject* nKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* nK = nKo ? nKo->asString(ctx) : nullptr;
    if (nK) it = it->setAttribute(ctx, nK, ctx->fromInteger(initialCounter));
    // Install .next pointing at our method.
    const proto::ProtoObject* nxKo = ctx->fromUTF8String("next");
    const proto::ProtoString* nxK = nxKo ? nxKo->asString(ctx) : nullptr;
    if (nxK) it = it->setAttribute(ctx, nxK, ctx->fromMethod(nullptr, nextMethod));
    // Install .return() that forwards to the underlying iterator once.
    const proto::ProtoObject* rtKo = ctx->fromUTF8String("return");
    const proto::ProtoString* rtK = rtKo ? rtKo->asString(ctx) : nullptr;
    if (rtK) it = it->setAttribute(ctx, rtK, ctx->fromMethod(nullptr, iteratorHelperReturn));
    return it;
}

static const proto::ProtoObject* makeIterResult(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* value,
                                                  bool done) {
    const proto::ProtoObject* r = ctx->newObject(true);
    const proto::ProtoString* vK = JSSymbols::value(ctx);
    const proto::ProtoString* dK = JSSymbols::done(ctx);
    if (vK) r = r->setAttribute(ctx, vK, value ? value : getUndefinedSentinel());
    if (dK) r = r->setAttribute(ctx, dK, done ? PROTO_TRUE : PROTO_FALSE);
    return r;
}

// §27.1.4.x helper-iterator .return() — forwards once to the
// underlying iterator's .return.  Idempotent: a __helper_returned__
// flag prevents re-forwarding on subsequent calls.  Always yields the
// canonical {value: undefined, done: true} record.
static const proto::ProtoObject* iteratorHelperReturn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* flagKo = ctx->fromUTF8String("__helper_returned__");
    const proto::ProtoString* flagK = flagKo ? flagKo->asString(ctx) : nullptr;
    bool alreadyReturned = false;
    if (flagK) {
        const proto::ProtoObject* fv = self->getAttribute(ctx, flagK, false);
        if (fv == PROTO_TRUE) alreadyReturned = true;
    }
    if (!alreadyReturned) {
        if (flagK) self->setAttribute(ctx, flagK, PROTO_TRUE);
        const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
        const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
        const proto::ProtoObject* underlying = uK ? self->getAttribute(ctx, uK, false) : nullptr;
        if (underlying && underlying != PROTO_NONE) {
            iterClose(ctx, underlying);
            if (hasCallException()) return PROTO_NONE;
        }
        const proto::ProtoObject* innerKo = ctx->fromUTF8String("__helper_inner__");
        const proto::ProtoString* innerK = innerKo ? innerKo->asString(ctx) : nullptr;
        if (innerK) {
            const proto::ProtoObject* inner = self->getAttribute(ctx, innerK, false);
            if (inner && inner != PROTO_NONE && inner != getUndefinedSentinel()) {
                iterClose(ctx, inner);
                if (hasCallException()) return PROTO_NONE;
            }
        }
    }
    return makeIterResult(ctx, getUndefinedSentinel(), true);
}

// .next for the map() helper: pulls value from underlying, runs cb(v, c).
static const proto::ProtoObject* iteratorMapNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* underlying = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    const proto::ProtoObject* cKo = ctx->fromUTF8String("__helper_cb__");
    const proto::ProtoString* cK = cKo ? cKo->asString(ctx) : nullptr;
    const proto::ProtoObject* cb = cK ? self->getAttribute(ctx, cK, false) : nullptr;
    const proto::ProtoObject* nKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* nK = nKo ? nKo->asString(ctx) : nullptr;
    long long counter = 0;
    if (nK) {
        const proto::ProtoObject* cv = self->getAttribute(ctx, nK, false);
        if (cv && cv->isInteger(ctx)) counter = cv->asLong(ctx);
    }
    if (!underlying || underlying == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* nextFn = iterGetNext(ctx, underlying, "map");
    if (!nextFn) return PROTO_NONE;
    bool done = false;
    const proto::ProtoObject* val = nullptr;
    (void)iterStep(ctx, underlying, nextFn, &done, &val);
    if (hasCallException()) return PROTO_NONE;
    if (done) return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoList* cbArgs = ctx->newList();
    cbArgs = cbArgs->appendLast(ctx, val ? val : PROTO_NONE);
    cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(counter));
    const proto::ProtoObject* mapped = callJSFunction(ctx, cb, getUndefinedSentinel(), cbArgs);
    if (hasCallException()) { iterClose(ctx, underlying); return PROTO_NONE; }
    // Bump counter on the helper.
    if (nK) {
        const proto::ProtoObject* newSelf =
            self->setAttribute(ctx, nK, ctx->fromInteger(counter + 1));
        (void)newSelf;
    }
    return makeIterResult(ctx, mapped, false);
}

static const proto::ProtoObject* iteratorMap(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "map")) return PROTO_NONE;
    const proto::ProtoObject* fn = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    bool callable = fn && fn != PROTO_NONE && fn->isMethod(ctx);
    if (!callable && fn && fn != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    }
    if (!callable) {
        // §27.1.4.x step 2 IfAbruptCloseIterator: argument-validation
        // failure must close the underlying iterator first.
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.map: callback is not callable"));
        return PROTO_NONE;
    }
    return makeHelperIterator(ctx, iteratorMapNext, self, fn, 0);
}

// .next for filter().
static const proto::ProtoObject* iteratorFilterNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* underlying = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    const proto::ProtoObject* cKo = ctx->fromUTF8String("__helper_cb__");
    const proto::ProtoString* cK = cKo ? cKo->asString(ctx) : nullptr;
    const proto::ProtoObject* cb = cK ? self->getAttribute(ctx, cK, false) : nullptr;
    const proto::ProtoObject* nKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* nK = nKo ? nKo->asString(ctx) : nullptr;
    long long counter = 0;
    if (nK) {
        const proto::ProtoObject* cv = self->getAttribute(ctx, nK, false);
        if (cv && cv->isInteger(ctx)) counter = cv->asLong(ctx);
    }
    if (!underlying || underlying == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* nextFn = iterGetNext(ctx, underlying, "filter");
    if (!nextFn) return PROTO_NONE;
    for (;;) {
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, underlying, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) return makeIterResult(ctx, getUndefinedSentinel(), true);
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, val ? val : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(counter++));
        const proto::ProtoObject* r = callJSFunction(ctx, cb, getUndefinedSentinel(), cbArgs);
        if (hasCallException()) { iterClose(ctx, underlying); return PROTO_NONE; }
        bool truthy = !(r == nullptr || r == PROTO_NONE || r == PROTO_FALSE
            || r == getUndefinedSentinel() || r == getNullSentinel()
            || (r->isInteger(ctx) && r->asLong(ctx) == 0));
        if (truthy) {
            if (nK) self->setAttribute(ctx, nK, ctx->fromInteger(counter));
            return makeIterResult(ctx, val, false);
        }
    }
}

static const proto::ProtoObject* iteratorFilter(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "filter")) return PROTO_NONE;
    const proto::ProtoObject* fn = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    bool callable = fn && fn != PROTO_NONE && fn->isMethod(ctx);
    if (!callable && fn && fn != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    }
    if (!callable) {
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.filter: callback is not callable"));
        return PROTO_NONE;
    }
    return makeHelperIterator(ctx, iteratorFilterNext, self, fn, 0);
}

// .next for take(): yields first n items.  __helper_counter__ used as
// remaining-count.
static const proto::ProtoObject* iteratorTakeNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* underlying = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    const proto::ProtoObject* nKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* nK = nKo ? nKo->asString(ctx) : nullptr;
    long long remaining = 0;
    if (nK) {
        const proto::ProtoObject* cv = self->getAttribute(ctx, nK, false);
        if (cv && cv->isInteger(ctx)) remaining = cv->asLong(ctx);
    }
    if (!underlying || underlying == PROTO_NONE || remaining <= 0) {
        if (remaining > 0 && underlying && underlying != PROTO_NONE) iterClose(ctx, underlying);
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    }
    const proto::ProtoObject* nextFn = iterGetNext(ctx, underlying, "take");
    if (!nextFn) return PROTO_NONE;
    bool done = false;
    const proto::ProtoObject* val = nullptr;
    (void)iterStep(ctx, underlying, nextFn, &done, &val);
    if (hasCallException()) return PROTO_NONE;
    if (done) return makeIterResult(ctx, getUndefinedSentinel(), true);
    if (nK) self->setAttribute(ctx, nK, ctx->fromInteger(remaining - 1));
    return makeIterResult(ctx, val, false);
}

static const proto::ProtoObject* iteratorTake(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "take")) return PROTO_NONE;
    const proto::ProtoObject* limitArg = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // §27.1.4.5 step 2-4: ToIntegerOrInfinity then RangeError on negative
    // / NaN.  Pre-fix the call swallowed those for non-number args.
    // §27.1.4.5 step 3-6: numLimit = ToNumber(limit), NaN/negative →
    // RangeError, undefined → NaN → RangeError, null → 0, boolean →
    // 0 or 1, strings parsed via ToNumber.  Pre-fix the helper only
    // accepted explicit Integer/Double cells.
    double limitNum = 0.0;
    bool valid = false;
    if (!limitArg || limitArg == PROTO_NONE || limitArg == getUndefinedSentinel()) {
        limitNum = std::nan("");  // ToNumber(undefined) = NaN
    } else if (limitArg == getNullSentinel()) {
        limitNum = 0.0;
        valid = true;
    } else if (limitArg == PROTO_TRUE) {
        limitNum = 1.0;
        valid = true;
    } else if (limitArg == PROTO_FALSE) {
        limitNum = 0.0;
        valid = true;
    } else if (limitArg->isInteger(ctx)) {
        limitNum = static_cast<double>(limitArg->asLong(ctx));
        valid = true;
    } else if (limitArg->isDouble(ctx) || limitArg->isFloat(ctx)) {
        limitNum = limitArg->asDouble(ctx);
        valid = true;
    } else if (limitArg->isString(ctx)) {
        std::string s;
        limitArg->asString(ctx)->toUTF8String(ctx, s);
        try { limitNum = std::stod(s); valid = true; }
        catch (...) { limitNum = std::nan(""); }
    } else {
        // Object → would normally ToPrimitive then ToNumber.  For
        // simplicity reject as NaN.
        limitNum = std::nan("");
    }
    // §27.1.4.5 step 4: NaN → RangeError.  IfAbruptCloseIterator: close
    // the underlying iterator before re-raising.
    if (limitNum != limitNum) {
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.take: limit must not be NaN"));
        return PROTO_NONE;
    }
    // Step 5: integerLimit = ToIntegerOrInfinity(numLimit) — truncate
    // toward zero; for -0.5 this yields 0 (not -1).
    double integerLimit = std::trunc(limitNum);
    if (integerLimit < 0) {
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.take: limit must be non-negative"));
        return PROTO_NONE;
    }
    long long remaining = (integerLimit > static_cast<double>(0x7FFFFFFFLL))
        ? 0x7FFFFFFFLL : static_cast<long long>(integerLimit);
    return makeHelperIterator(ctx, iteratorTakeNext, self, PROTO_NONE, remaining);
}

// .next for drop(): skips first n underlying items, then forwards.
static const proto::ProtoObject* iteratorDropNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* underlying = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    const proto::ProtoObject* nKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* nK = nKo ? nKo->asString(ctx) : nullptr;
    long long remainingToSkip = 0;
    if (nK) {
        const proto::ProtoObject* cv = self->getAttribute(ctx, nK, false);
        if (cv && cv->isInteger(ctx)) remainingToSkip = cv->asLong(ctx);
    }
    if (!underlying || underlying == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* nextFn = iterGetNext(ctx, underlying, "drop");
    if (!nextFn) return PROTO_NONE;
    while (remainingToSkip > 0) {
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, underlying, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) {
            if (nK) self->setAttribute(ctx, nK, ctx->fromInteger(0));
            return makeIterResult(ctx, getUndefinedSentinel(), true);
        }
        remainingToSkip--;
    }
    if (nK) self->setAttribute(ctx, nK, ctx->fromInteger(0));
    bool done = false;
    const proto::ProtoObject* val = nullptr;
    (void)iterStep(ctx, underlying, nextFn, &done, &val);
    if (hasCallException()) return PROTO_NONE;
    if (done) return makeIterResult(ctx, getUndefinedSentinel(), true);
    return makeIterResult(ctx, val, false);
}

static const proto::ProtoObject* iteratorDrop(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "drop")) return PROTO_NONE;
    const proto::ProtoObject* limitArg = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // §27.1.4.6 step 3-6: same ToNumber + NaN-RangeError + non-negative
    // pattern as take.
    double limitNum = 0.0;
    bool valid = false;
    if (!limitArg || limitArg == PROTO_NONE || limitArg == getUndefinedSentinel()) {
        limitNum = std::numeric_limits<double>::quiet_NaN();
    } else if (limitArg == getNullSentinel()) {
        limitNum = 0.0; valid = true;
    } else if (limitArg == PROTO_TRUE) {
        limitNum = 1.0; valid = true;
    } else if (limitArg == PROTO_FALSE) {
        limitNum = 0.0; valid = true;
    } else if (limitArg->isInteger(ctx)) {
        limitNum = static_cast<double>(limitArg->asLong(ctx)); valid = true;
    } else if (limitArg->isDouble(ctx) || limitArg->isFloat(ctx)) {
        limitNum = limitArg->asDouble(ctx); valid = true;
    } else if (limitArg->isString(ctx)) {
        std::string s;
        limitArg->asString(ctx)->toUTF8String(ctx, s);
        try { limitNum = std::stod(s); valid = true; }
        catch (...) { limitNum = std::numeric_limits<double>::quiet_NaN(); }
    } else {
        limitNum = std::numeric_limits<double>::quiet_NaN();
    }
    if (limitNum != limitNum) {
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.drop: limit must not be NaN"));
        return PROTO_NONE;
    }
    double integerLimit = std::trunc(limitNum);
    if (integerLimit < 0) {
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.drop: limit must be non-negative"));
        return PROTO_NONE;
    }
    (void)valid;
    long long skipCount = (integerLimit > static_cast<double>(0x7FFFFFFFLL))
        ? 0x7FFFFFFFLL : static_cast<long long>(integerLimit);
    return makeHelperIterator(ctx, iteratorDropNext, self, PROTO_NONE, skipCount);
}

// .next for flatMap(): pulls value from underlying, maps via cb,
// extracts an iterator from the result (via @@iterator), and yields
// each value from that inner iterator before pulling the next outer
// value.  __helper_underlying__ holds the outer iterator;
// __helper_cb__ holds the mapper; __helper_counter__ holds the outer
// counter; __helper_inner__ (added below) holds the live inner
// iterator.
static const proto::ProtoObject* iteratorFlatMapNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* outer = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    const proto::ProtoObject* cKo = ctx->fromUTF8String("__helper_cb__");
    const proto::ProtoString* cK = cKo ? cKo->asString(ctx) : nullptr;
    const proto::ProtoObject* cb = cK ? self->getAttribute(ctx, cK, false) : nullptr;
    const proto::ProtoObject* nKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* nK = nKo ? nKo->asString(ctx) : nullptr;
    long long counter = 0;
    if (nK) {
        const proto::ProtoObject* cv = self->getAttribute(ctx, nK, false);
        if (cv && cv->isInteger(ctx)) counter = cv->asLong(ctx);
    }
    const proto::ProtoObject* innerKo = ctx->fromUTF8String("__helper_inner__");
    const proto::ProtoString* innerK = innerKo ? innerKo->asString(ctx) : nullptr;
    if (!outer || outer == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    for (;;) {
        // Drain any live inner iterator first.
        const proto::ProtoObject* inner = innerK ? self->getAttribute(ctx, innerK, false) : nullptr;
        if (inner && inner != PROTO_NONE && inner != getUndefinedSentinel()) {
            const proto::ProtoObject* innerNext = iterGetNext(ctx, inner, "flatMap");
            if (!innerNext) return PROTO_NONE;
            bool innerDone = false;
            const proto::ProtoObject* innerVal = nullptr;
            (void)iterStep(ctx, inner, innerNext, &innerDone, &innerVal);
            if (hasCallException()) return PROTO_NONE;
            if (!innerDone) return makeIterResult(ctx, innerVal, false);
            // Inner exhausted — clear and continue to next outer.
            if (innerK) self->setAttribute(ctx, innerK, getUndefinedSentinel());
        }
        // Pull next outer value.
        const proto::ProtoObject* outerNext = iterGetNext(ctx, outer, "flatMap");
        if (!outerNext) return PROTO_NONE;
        bool outerDone = false;
        const proto::ProtoObject* outerVal = nullptr;
        (void)iterStep(ctx, outer, outerNext, &outerDone, &outerVal);
        if (hasCallException()) return PROTO_NONE;
        if (outerDone) return makeIterResult(ctx, getUndefinedSentinel(), true);
        // Invoke mapper.
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, outerVal ? outerVal : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(counter++));
        if (nK) self->setAttribute(ctx, nK, ctx->fromInteger(counter));
        const proto::ProtoObject* mapped = callJSFunction(ctx, cb, getUndefinedSentinel(), cbArgs);
        if (hasCallException()) { iterClose(ctx, outer); return PROTO_NONE; }
        if (!mapped || mapped == PROTO_NONE
            || mapped == getNullSentinel() || mapped == getUndefinedSentinel()
            || mapped->isInteger(ctx) || mapped->isDouble(ctx)
            || mapped->isFloat(ctx) || mapped->isBoolean(ctx)) {
            // §27.1.4.3 step 4.b.iv.2: mapper must return an Object.
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Iterator.prototype.flatMap: mapper must return an Object"));
            iterClose(ctx, outer);
            return PROTO_NONE;
        }
        // GetIterator: invoke mapped[@@iterator]() to extract its iterator.
        const proto::ProtoString* sIK = JSSymbols::symbolIterator(ctx);
        const proto::ProtoObject* itoFn = sIK ? mapped->getAttribute(ctx, sIK, true) : nullptr;
        const proto::ProtoObject* newInner = nullptr;
        if (itoFn && itoFn != PROTO_NONE && itoFn != getUndefinedSentinel()) {
            newInner = callJSFunction(ctx, itoFn, mapped, ctx->newList());
            if (hasCallException()) { iterClose(ctx, outer); return PROTO_NONE; }
        }
        if (!newInner || newInner == PROTO_NONE || newInner == getUndefinedSentinel()) {
            // Treat as no-yield; loop to next outer.
            continue;
        }
        if (innerK) self->setAttribute(ctx, innerK, newInner);
    }
}

static const proto::ProtoObject* iteratorFlatMap(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!iterRequireObject(ctx, self, "flatMap")) return PROTO_NONE;
    const proto::ProtoObject* fn = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    bool callable = fn && fn != PROTO_NONE && fn->isMethod(ctx);
    if (!callable && fn && fn != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (!callable && nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
    }
    if (!callable) {
        iterClose(ctx, self);
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.flatMap: callback is not callable"));
        return PROTO_NONE;
    }
    return makeHelperIterator(ctx, iteratorFlatMapNext, self, fn, 0);
}

// ---------------------------------------------------------------------------
// Install helpers on Iterator.prototype.  Each method gets a
// wrapNativeFunction-shaped wrapper with §17 name/length descriptors.
// ---------------------------------------------------------------------------
static void installHelper(proto::ProtoContext* ctx,
                           const proto::ProtoObject** proto,
                           const char* name,
                           proto::ProtoMethod fn,
                           long long length)
{
    if (!proto || !*proto) return;
    // Wrap the method into a Function-prototype-parented object so
    // name/length descriptors and `.call` work.
    const proto::ProtoObject* parent = ctx->space ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* wrap = parent
        ? parent->newChild(ctx, true) : ctx->newObject(true);
    if (!wrap) return;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK) wrap = wrap->setAttribute(ctx, nfK, ctx->fromMethod(nullptr, fn));
    const proto::ProtoString* nK = JSSymbols::name(ctx);
    if (nK) {
        wrap = wrap->setAttribute(ctx, nK, ctx->fromUTF8String(name));
        const proto::ProtoString* pdnK = JSSymbols::pdName(ctx);
        if (pdnK) wrap = wrap->setAttribute(ctx, pdnK, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* lK = JSSymbols::length(ctx);
    if (lK) {
        wrap = wrap->setAttribute(ctx, lK, ctx->fromInteger(length));
        const proto::ProtoString* pdlK = JSSymbols::pdLength(ctx);
        if (pdlK) wrap = wrap->setAttribute(ctx, pdlK, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
    if (hnwK) wrap = wrap->setAttribute(ctx, hnwK, PROTO_TRUE);
    // Stamp on prototype with §17 descriptor 0x3 (writable, non-enumerable,
    // configurable).
    const proto::ProtoObject* mKo = ctx->fromUTF8String(name);
    const proto::ProtoString* mK = mKo ? mKo->asString(ctx) : nullptr;
    if (mK) {
        *proto = (*proto)->setAttribute(ctx, mK, wrap);
        std::string pdName = std::string("__pd_") + name + "__";
        const proto::ProtoObject* pdo = ctx->fromUTF8String(pdName.c_str());
        const proto::ProtoString* pdK = pdo ? pdo->asString(ctx) : nullptr;
        if (pdK) *proto = (*proto)->setAttribute(ctx, pdK, ctx->fromInteger(0x3LL));
    }
}

const proto::ProtoObject* getIteratorPrototype(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* s_iteratorProto = nullptr;
    if (s_iteratorProto) return s_iteratorProto;
    if (!ctx) return nullptr;
    proto::ProtoObject* objProto =
        ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto
        ? objProto->newChild(ctx, true) : ctx->newObject(true);
    if (!proto) return nullptr;

    // @@iterator returning this — required by GetIterator (§27.1.2.1).
    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
    if (symIterKey) {
        const proto::ProtoObject* iterFn = ctx->fromMethod(nullptr, iteratorReturnSelf);
        if (iterFn) proto = proto->setAttribute(ctx, symIterKey, iterFn);
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.iterator__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    }

    // @@toStringTag = "Iterator" — ES2024+ §27.1.2.2.
    const proto::ProtoString* tag = JSSymbols::symbolToStringTag(ctx);
    if (tag) {
        proto = proto->setAttribute(ctx, tag, ctx->fromUTF8String("Iterator"));
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    }

    // Per-target hint stamp for descriptor enforcement.
    const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
    if (hnwK) proto = proto->setAttribute(ctx, hnwK, PROTO_TRUE);

    // ES2024 Iterator Helpers proposal — install the public methods.
    installHelper(ctx, &proto, "toArray", iteratorToArray, 0);
    installHelper(ctx, &proto, "forEach", iteratorForEach, 1);
    installHelper(ctx, &proto, "reduce",  iteratorReduce,  1);
    installHelper(ctx, &proto, "every",   iteratorEvery,   1);
    installHelper(ctx, &proto, "some",    iteratorSome,    1);
    installHelper(ctx, &proto, "find",    iteratorFind,    1);
    installHelper(ctx, &proto, "map",     iteratorMap,     1);
    installHelper(ctx, &proto, "filter",  iteratorFilter,  1);
    installHelper(ctx, &proto, "take",    iteratorTake,    1);
    installHelper(ctx, &proto, "drop",    iteratorDrop,    1);
    installHelper(ctx, &proto, "flatMap", iteratorFlatMap, 1);

    // §27.1.4.13 Iterator.prototype[@@dispose] — calls this.return()
    // if present.  Used by the explicit-resource-management proposal
    // (Stage 3, 2024).
    {
        static const auto iteratorSymbolDispose = [](
            proto::ProtoContext* dctx, const proto::ProtoObject* dself,
            const proto::ParentLink*, const proto::ProtoList*,
            const proto::ProtoSparseList*) -> const proto::ProtoObject* {
            if (!iterRequireObject(dctx, dself, "[Symbol.dispose]")) return PROTO_NONE;
            const proto::ProtoObject* retKo = dctx->fromUTF8String("return");
            const proto::ProtoString* retK = retKo ? retKo->asString(dctx) : nullptr;
            if (retK) {
                const proto::ProtoObject* fn = dself->getAttribute(dctx, retK, true);
                if (fn && fn != PROTO_NONE && fn != getUndefinedSentinel()
                    && fn != getNullSentinel()) {
                    bool callable = fn->isMethod(dctx);
                    const proto::ProtoString* bcK = JSSymbols::bytecodeId(dctx);
                    if (!callable && bcK && fn->hasAttribute(dctx, bcK) == PROTO_TRUE) callable = true;
                    const proto::ProtoString* nfK = JSSymbols::nativeFn(dctx);
                    if (!callable && nfK && fn->hasAttribute(dctx, nfK) == PROTO_TRUE) callable = true;
                    if (callable) {
                        (void)callJSFunction(dctx, fn, dself, dctx->newList());
                        if (hasCallException()) return PROTO_NONE;
                    }
                }
            }
            return getUndefinedSentinel();
        };
        const proto::ProtoObject* dispKo = ctx->fromUTF8String("Symbol.dispose");
        const proto::ProtoString* dispK = dispKo ? dispKo->asString(ctx) : nullptr;
        if (dispK) {
            // Build a Function-prototype-parented wrapper, same shape as
            // installHelper.
            const proto::ProtoObject* parent = ctx->space ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* wrap = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
            if (nfK) wrap = wrap->setAttribute(ctx, nfK, ctx->fromMethod(nullptr, iteratorSymbolDispose));
            const proto::ProtoString* nK = JSSymbols::name(ctx);
            if (nK) {
                wrap = wrap->setAttribute(ctx, nK, ctx->fromUTF8String("[Symbol.dispose]"));
                const proto::ProtoString* pdnK = JSSymbols::pdName(ctx);
                if (pdnK) wrap = wrap->setAttribute(ctx, pdnK, ctx->fromInteger(0x2LL));
            }
            const proto::ProtoString* lK = JSSymbols::length(ctx);
            if (lK) {
                wrap = wrap->setAttribute(ctx, lK, ctx->fromInteger(0LL));
                const proto::ProtoString* pdlK = JSSymbols::pdLength(ctx);
                if (pdlK) wrap = wrap->setAttribute(ctx, pdlK, ctx->fromInteger(0x2LL));
            }
            proto = proto->setAttribute(ctx, dispK, wrap);
            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.dispose__");
            const proto::ProtoString* pdK = pdo ? pdo->asString(ctx) : nullptr;
            if (pdK) proto = proto->setAttribute(ctx, pdK, ctx->fromInteger(0x3LL));
        }
    }

    s_iteratorProto = proto;
    return s_iteratorProto;
}

// ---------------------------------------------------------------------------
// Iterator static methods (ES2024 §27.1.2.*).
// ---------------------------------------------------------------------------

// Helper: GetIterator(O, hint=sync) — calls O[@@iterator]() to extract
// the iterator.  Returns the inner iterator or nullptr on abrupt.
// When O directly carries a callable `.next`, treat it as already an
// iterator and return as-is (§27.1.2.1.1.1 step 3 fallback).
static const proto::ProtoObject* getIteratorFor(
    proto::ProtoContext* ctx, const proto::ProtoObject* O)
{
    if (!O || O == PROTO_NONE || O == getNullSentinel()
        || O == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.from: argument is not iterable"));
        return nullptr;
    }
    // Numeric / Boolean primitives: not iterable.
    if (O->isInteger(ctx) || O->isDouble(ctx) || O->isFloat(ctx)
        || O->isBoolean(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.from: primitive not iterable"));
        return nullptr;
    }
    const proto::ProtoString* itoK = JSSymbols::symbolIterator(ctx);
    const proto::ProtoObject* itoFn = itoK ? O->getAttribute(ctx, itoK, true) : nullptr;
    if (itoFn && itoFn != PROTO_NONE && itoFn != getUndefinedSentinel()
        && itoFn != getNullSentinel()) {
        const proto::ProtoObject* it = callJSFunction(ctx, itoFn, O, ctx->newList());
        if (hasCallException()) return nullptr;
        if (!it || it == PROTO_NONE
            || it == getNullSentinel() || it == getUndefinedSentinel()
            || it->isInteger(ctx) || it->isDouble(ctx)
            || it->isFloat(ctx) || it->isBoolean(ctx) || it->isString(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Iterator.from: @@iterator did not return an Object"));
            return nullptr;
        }
        return it;
    }
    // Fallback: treat O itself as the iterator (must have callable next).
    const proto::ProtoObject* nextFn = iterGetNext(ctx, O, "from");
    if (!nextFn) return nullptr;
    return O;
}

// ---------------------------------------------------------------------------
// Iterator.from(O) — wrap an iterable / iterator into a fresh iterator
// chained on %IteratorPrototype% (§27.1.2.1).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorFromNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* underlying = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    if (!underlying || underlying == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* nextFn = iterGetNext(ctx, underlying, "from");
    if (!nextFn) return PROTO_NONE;
    bool done = false;
    const proto::ProtoObject* val = nullptr;
    (void)iterStep(ctx, underlying, nextFn, &done, &val);
    if (hasCallException()) return PROTO_NONE;
    if (done) return makeIterResult(ctx, getUndefinedSentinel(), true);
    return makeIterResult(ctx, val, false);
}

static const proto::ProtoObject* iteratorFrom(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* O = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // String primitive: wrap via @@iterator route below; ToObject first.
    if (O && O->isString(ctx)) {
        // Use the String wrapper's @@iterator path — but our impl exposes
        // the protostring's iterator directly via stringPrototype.
        // For simplicity, extract characters into an array iterator.
        const proto::ProtoString* itoK = JSSymbols::symbolIterator(ctx);
        if (itoK) {
            const proto::ProtoObject* itoFn = O->getAttribute(ctx, itoK, true);
            if (itoFn && itoFn != PROTO_NONE) {
                const proto::ProtoObject* inner = callJSFunction(ctx, itoFn, O, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (inner && inner != PROTO_NONE) {
                    return makeHelperIterator(ctx, iteratorFromNext, inner, PROTO_NONE, 0);
                }
            }
        }
    }
    const proto::ProtoObject* inner = getIteratorFor(ctx, O);
    if (!inner) return PROTO_NONE;
    // Optimisation: if `inner` is already chained on %IteratorPrototype%,
    // the spec says we can return it directly.  But for simplicity wrap
    // every result so the helpers chain through our prototype.
    return makeHelperIterator(ctx, iteratorFromNext, inner, PROTO_NONE, 0);
}

// ---------------------------------------------------------------------------
// Iterator.concat(...iterables) — flatten the supplied iterables into
// a single iterator (§27.1.2.2 Stage 4 2025).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorConcatNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    // __helper_underlying__ holds an array of the source iterables.
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* sources = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    if (!sources || sources == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* idxKo = ctx->fromUTF8String("__helper_counter__");
    const proto::ProtoString* idxK = idxKo ? idxKo->asString(ctx) : nullptr;
    long long idx = 0;
    if (idxK) {
        const proto::ProtoObject* cv = self->getAttribute(ctx, idxK, false);
        if (cv && cv->isInteger(ctx)) idx = cv->asLong(ctx);
    }
    const proto::ProtoObject* innerKo = ctx->fromUTF8String("__helper_inner__");
    const proto::ProtoString* innerK = innerKo ? innerKo->asString(ctx) : nullptr;
    const proto::ProtoList* arr = getArrayElements(ctx, sources);
    if (!arr) return makeIterResult(ctx, getUndefinedSentinel(), true);
    long long arrLen = static_cast<long long>(arr->getSize(ctx));
    for (;;) {
        const proto::ProtoObject* inner = innerK ? self->getAttribute(ctx, innerK, false) : nullptr;
        if (inner && inner != PROTO_NONE && inner != getUndefinedSentinel()) {
            const proto::ProtoObject* innerNext = iterGetNext(ctx, inner, "concat");
            if (!innerNext) return PROTO_NONE;
            bool done = false;
            const proto::ProtoObject* val = nullptr;
            (void)iterStep(ctx, inner, innerNext, &done, &val);
            if (hasCallException()) return PROTO_NONE;
            if (!done) return makeIterResult(ctx, val, false);
            // Inner exhausted — move to next source.
            if (innerK) self->setAttribute(ctx, innerK, getUndefinedSentinel());
            idx++;
            if (idxK) self->setAttribute(ctx, idxK, ctx->fromInteger(idx));
        }
        if (idx >= arrLen) return makeIterResult(ctx, getUndefinedSentinel(), true);
        const proto::ProtoObject* src = arr->getAt(ctx, static_cast<int>(idx));
        if (!src || src == PROTO_NONE) { idx++; if (idxK) self->setAttribute(ctx, idxK, ctx->fromInteger(idx)); continue; }
        const proto::ProtoObject* newInner = getIteratorFor(ctx, src);
        if (!newInner) return PROTO_NONE;
        if (innerK) self->setAttribute(ctx, innerK, newInner);
    }
}

static const proto::ProtoObject* iteratorConcat(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // §27.1.2.2 step 2: every argument must be an Object with
    // @@iterator (or itself an iterator).  Validate them eagerly before
    // returning the wrapper so the test262 fixtures see the abrupt at
    // call time, not at first .next().
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* sources = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
    if (isArrK) sources = sources->setAttribute(ctx, isArrK, PROTO_TRUE);
    const proto::ProtoList* els = ctx->newList();
    for (int i = 0; i < argc; i++) {
        const proto::ProtoObject* it = args->getAt(ctx, i);
        if (!it || it == PROTO_NONE
            || it == getNullSentinel() || it == getUndefinedSentinel()
            || it->isInteger(ctx) || it->isDouble(ctx)
            || it->isFloat(ctx) || it->isBoolean(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Iterator.concat: argument is not an iterable"));
            return PROTO_NONE;
        }
        // GetMethod(@@iterator) must be callable.  We don't call it
        // here — that happens lazily in .next — but we validate
        // presence/callability up front.
        const proto::ProtoString* itoK = JSSymbols::symbolIterator(ctx);
        const proto::ProtoObject* itoFn = itoK ? it->getAttribute(ctx, itoK, true) : nullptr;
        if (!itoFn || itoFn == PROTO_NONE
            || itoFn == getUndefinedSentinel() || itoFn == getNullSentinel()) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Iterator.concat: argument lacks @@iterator"));
            return PROTO_NONE;
        }
        els = els->appendLast(ctx, it);
    }
    setArrayElements(ctx, sources, els);
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    if (lenK) sources = sources->setAttribute(ctx, lenK, ctx->fromInteger(static_cast<long long>(argc)));
    return makeHelperIterator(ctx, iteratorConcatNext, sources, PROTO_NONE, 0);
}

// ---------------------------------------------------------------------------
// Iterator.zip(iterables) — pair-wise zip of N iterators into tuples.
// (§27.1.2.3 Stage 4 2025).  Stops at the first exhausted source by
// default; options.mode = "longest" / "shortest" / "strict".
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorZipNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    // sources is an Array of inner iterators.
    const proto::ProtoObject* sources = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    if (!sources || sources == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoList* arr = getArrayElements(ctx, sources);
    if (!arr) return makeIterResult(ctx, getUndefinedSentinel(), true);
    unsigned long n = arr->getSize(ctx);
    if (n == 0) return makeIterResult(ctx, getUndefinedSentinel(), true);
    // Build a result array; stop at first exhausted source (shortest).
    const proto::ProtoObject* tuple = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
    if (isArrK) tuple = tuple->setAttribute(ctx, isArrK, PROTO_TRUE);
    const proto::ProtoList* tEls = ctx->newList();
    for (unsigned long i = 0; i < n; i++) {
        const proto::ProtoObject* inner = arr->getAt(ctx, static_cast<int>(i));
        if (!inner || inner == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
        const proto::ProtoObject* nextFn = iterGetNext(ctx, inner, "zip");
        if (!nextFn) return PROTO_NONE;
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, inner, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) {
            // Close remaining inner iterators.
            for (unsigned long j = 0; j < n; j++) {
                if (j != i) {
                    const proto::ProtoObject* other = arr->getAt(ctx, static_cast<int>(j));
                    if (other && other != PROTO_NONE) iterClose(ctx, other);
                }
            }
            return makeIterResult(ctx, getUndefinedSentinel(), true);
        }
        tEls = tEls->appendLast(ctx, val ? val : PROTO_NONE);
    }
    setArrayElements(ctx, tuple, tEls);
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    if (lenK) tuple = tuple->setAttribute(ctx, lenK, ctx->fromInteger(static_cast<long long>(n)));
    return makeIterResult(ctx, tuple, false);
}

static const proto::ProtoObject* iteratorZip(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* iterables = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!iterables || iterables == PROTO_NONE
        || iterables == getNullSentinel() || iterables == getUndefinedSentinel()
        || iterables->isInteger(ctx) || iterables->isDouble(ctx)
        || iterables->isFloat(ctx) || iterables->isBoolean(ctx)
        || iterables->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.zip: iterables must be an Object"));
        return PROTO_NONE;
    }
    // Materialise inner iterators eagerly.
    const proto::ProtoList* outerEls = getArrayElements(ctx, iterables);
    if (!outerEls) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.zip: iterables must be array-like"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* inners = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
    if (isArrK) inners = inners->setAttribute(ctx, isArrK, PROTO_TRUE);
    const proto::ProtoList* innersEls = ctx->newList();
    unsigned long n = outerEls->getSize(ctx);
    for (unsigned long i = 0; i < n; i++) {
        const proto::ProtoObject* src = outerEls->getAt(ctx, static_cast<int>(i));
        const proto::ProtoObject* inner = getIteratorFor(ctx, src);
        if (!inner) {
            // Close previously-acquired iterators.
            for (unsigned long j = 0; j < i; j++) {
                const proto::ProtoObject* prev = innersEls->getAt(ctx, static_cast<int>(j));
                if (prev && prev != PROTO_NONE) iterClose(ctx, prev);
            }
            return PROTO_NONE;
        }
        innersEls = innersEls->appendLast(ctx, inner);
    }
    setArrayElements(ctx, inners, innersEls);
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    if (lenK) inners = inners->setAttribute(ctx, lenK, ctx->fromInteger(static_cast<long long>(n)));
    return makeHelperIterator(ctx, iteratorZipNext, inners, PROTO_NONE, 0);
}

// ---------------------------------------------------------------------------
// Iterator.zipKeyed(iterables) — like zip but yields {key: val, ...}
// objects keyed by the input object's own keys.  Simpler shape: input
// is a plain object whose own enumerable values are the iterables.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* iteratorZipKeyedNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    // __helper_underlying__ holds the {keys, iters} record (encoded as
    // a length-2 array: [keysArray, itersArray]).
    const proto::ProtoObject* uKo = ctx->fromUTF8String("__helper_underlying__");
    const proto::ProtoString* uK = uKo ? uKo->asString(ctx) : nullptr;
    const proto::ProtoObject* pair = uK ? self->getAttribute(ctx, uK, false) : nullptr;
    if (!pair || pair == PROTO_NONE)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoList* pairEls = getArrayElements(ctx, pair);
    if (!pairEls || pairEls->getSize(ctx) < 2)
        return makeIterResult(ctx, getUndefinedSentinel(), true);
    const proto::ProtoObject* keys = pairEls->getAt(ctx, 0);
    const proto::ProtoObject* iters = pairEls->getAt(ctx, 1);
    const proto::ProtoList* keysEls = getArrayElements(ctx, keys);
    const proto::ProtoList* itersEls = getArrayElements(ctx, iters);
    if (!keysEls || !itersEls) return makeIterResult(ctx, getUndefinedSentinel(), true);
    unsigned long n = keysEls->getSize(ctx);
    const proto::ProtoObject* out = ctx->newObject(true);
    for (unsigned long i = 0; i < n; i++) {
        const proto::ProtoObject* inner = itersEls->getAt(ctx, static_cast<int>(i));
        if (!inner || inner == PROTO_NONE) return makeIterResult(ctx, getUndefinedSentinel(), true);
        const proto::ProtoObject* nextFn = iterGetNext(ctx, inner, "zipKeyed");
        if (!nextFn) return PROTO_NONE;
        bool done = false;
        const proto::ProtoObject* val = nullptr;
        (void)iterStep(ctx, inner, nextFn, &done, &val);
        if (hasCallException()) return PROTO_NONE;
        if (done) {
            for (unsigned long j = 0; j < n; j++) {
                if (j != i) {
                    const proto::ProtoObject* other = itersEls->getAt(ctx, static_cast<int>(j));
                    if (other && other != PROTO_NONE) iterClose(ctx, other);
                }
            }
            return makeIterResult(ctx, getUndefinedSentinel(), true);
        }
        const proto::ProtoObject* key = keysEls->getAt(ctx, static_cast<int>(i));
        if (key && key->isString(ctx)) {
            const proto::ProtoString* keyS = key->asString(ctx);
            if (keyS) out = out->setAttribute(ctx, keyS, val ? val : PROTO_NONE);
        }
    }
    return makeIterResult(ctx, out, false);
}

static const proto::ProtoObject* iteratorZipKeyed(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!obj || obj == PROTO_NONE
        || obj == getNullSentinel() || obj == getUndefinedSentinel()
        || obj->isInteger(ctx) || obj->isDouble(ctx)
        || obj->isFloat(ctx) || obj->isBoolean(ctx) || obj->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.zipKeyed: argument must be an Object"));
        return PROTO_NONE;
    }
    // Walk own enumerable keys, gather (key, iterator) pairs.
    const proto::ProtoObject* keysArr = createNewArray(ctx, nullptr);
    const proto::ProtoObject* itersArr = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
    if (isArrK) {
        keysArr = keysArr->setAttribute(ctx, isArrK, PROTO_TRUE);
        itersArr = itersArr->setAttribute(ctx, isArrK, PROTO_TRUE);
    }
    const proto::ProtoList* keysEls = ctx->newList();
    const proto::ProtoList* itersEls = ctx->newList();
    long long count = 0;
    const proto::ProtoSparseList* own = obj->getOwnAttributes(ctx);
    if (own) {
        const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            unsigned long rawKey = it->nextKey(ctx);
            const proto::ProtoObject* val = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            const proto::ProtoString* propKey =
                reinterpret_cast<const proto::ProtoString*>(rawKey);
            if (!propKey || !val || val == PROTO_NONE) continue;
            std::string ks;
            propKey->toUTF8String(ctx, ks);
            if (ks.size() >= 2 && ks[0] == '_' && ks[1] == '_') continue; // internal
            const proto::ProtoObject* inner = getIteratorFor(ctx, val);
            if (!inner) {
                // Close any already-gathered iterators.
                unsigned long sz = itersEls->getSize(ctx);
                for (unsigned long j = 0; j < sz; j++) {
                    const proto::ProtoObject* prev = itersEls->getAt(ctx, static_cast<int>(j));
                    if (prev && prev != PROTO_NONE) iterClose(ctx, prev);
                }
                return PROTO_NONE;
            }
            keysEls = keysEls->appendLast(ctx, ctx->fromUTF8String(ks.c_str()));
            itersEls = itersEls->appendLast(ctx, inner);
            count++;
        }
    }
    setArrayElements(ctx, keysArr, keysEls);
    setArrayElements(ctx, itersArr, itersEls);
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    if (lenK) {
        keysArr = keysArr->setAttribute(ctx, lenK, ctx->fromInteger(count));
        itersArr = itersArr->setAttribute(ctx, lenK, ctx->fromInteger(count));
    }
    const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
    if (isArrK) pair = pair->setAttribute(ctx, isArrK, PROTO_TRUE);
    const proto::ProtoList* pairEls = ctx->newList();
    pairEls = pairEls->appendLast(ctx, keysArr);
    pairEls = pairEls->appendLast(ctx, itersArr);
    setArrayElements(ctx, pair, pairEls);
    if (lenK) pair = pair->setAttribute(ctx, lenK, ctx->fromInteger(2));
    return makeHelperIterator(ctx, iteratorZipKeyedNext, pair, PROTO_NONE, 0);
}

const proto::ProtoObject* installIteratorStatics(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* stub)
{
    if (!ctx || !stub || stub == PROTO_NONE) return stub;
    auto installStatic = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoObject* parent = ctx->space ? ctx->space->methodPrototype : nullptr;
        const proto::ProtoObject* wrap = parent
            ? parent->newChild(ctx, true) : ctx->newObject(true);
        if (!wrap) return;
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (nfK) wrap = wrap->setAttribute(ctx, nfK, ctx->fromMethod(nullptr, fn));
        const proto::ProtoString* nK = JSSymbols::name(ctx);
        if (nK) {
            wrap = wrap->setAttribute(ctx, nK, ctx->fromUTF8String(name));
            const proto::ProtoString* pdnK = JSSymbols::pdName(ctx);
            if (pdnK) wrap = wrap->setAttribute(ctx, pdnK, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* lK = JSSymbols::length(ctx);
        if (lK) {
            wrap = wrap->setAttribute(ctx, lK, ctx->fromInteger(length));
            const proto::ProtoString* pdlK = JSSymbols::pdLength(ctx);
            if (pdlK) wrap = wrap->setAttribute(ctx, pdlK, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
        if (hnwK) wrap = wrap->setAttribute(ctx, hnwK, PROTO_TRUE);
        const proto::ProtoObject* mKo = ctx->fromUTF8String(name);
        const proto::ProtoString* mK = mKo ? mKo->asString(ctx) : nullptr;
        if (mK) {
            stub = stub->setAttribute(ctx, mK, wrap);
            std::string pdName = std::string("__pd_") + name + "__";
            const proto::ProtoObject* pdo = ctx->fromUTF8String(pdName.c_str());
            const proto::ProtoString* pdK = pdo ? pdo->asString(ctx) : nullptr;
            if (pdK) stub = stub->setAttribute(ctx, pdK, ctx->fromInteger(0x3LL));
        }
    };
    installStatic("from",     iteratorFrom,     1);
    installStatic("concat",   iteratorConcat,   0);
    installStatic("zip",      iteratorZip,      1);
    installStatic("zipKeyed", iteratorZipKeyed, 1);
    return stub;
}

} // namespace protojs
