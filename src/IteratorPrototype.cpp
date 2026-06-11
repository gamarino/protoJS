#include "IteratorPrototype.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"

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
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.map: callback is not callable"));
        return PROTO_NONE;
    }
    // Validate underlying iterator now so test262 can verify ordering.
    if (!iterGetNext(ctx, self, "map")) return PROTO_NONE;
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
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Iterator.prototype.filter: callback is not callable"));
        return PROTO_NONE;
    }
    if (!iterGetNext(ctx, self, "filter")) return PROTO_NONE;
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
    double limitNum = 0.0;
    if (!limitArg || limitArg == PROTO_NONE || limitArg == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.take: limit must be a non-negative integer"));
        return PROTO_NONE;
    }
    if (limitArg->isInteger(ctx)) limitNum = static_cast<double>(limitArg->asLong(ctx));
    else if (limitArg->isDouble(ctx) || limitArg->isFloat(ctx)) limitNum = limitArg->asDouble(ctx);
    else {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.take: limit must be a non-negative integer"));
        return PROTO_NONE;
    }
    if (limitNum != limitNum /*NaN*/ || limitNum < 0) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.take: limit must be a non-negative integer"));
        return PROTO_NONE;
    }
    long long remaining = (limitNum > static_cast<double>(0x7FFFFFFFLL))
        ? 0x7FFFFFFFLL : static_cast<long long>(limitNum);
    if (!iterGetNext(ctx, self, "take")) return PROTO_NONE;
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
    double limitNum = 0.0;
    if (!limitArg || limitArg == PROTO_NONE || limitArg == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.drop: limit must be a non-negative integer"));
        return PROTO_NONE;
    }
    if (limitArg->isInteger(ctx)) limitNum = static_cast<double>(limitArg->asLong(ctx));
    else if (limitArg->isDouble(ctx) || limitArg->isFloat(ctx)) limitNum = limitArg->asDouble(ctx);
    else {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.drop: limit must be a non-negative integer"));
        return PROTO_NONE;
    }
    if (limitNum != limitNum /*NaN*/ || limitNum < 0) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Iterator.prototype.drop: limit must be a non-negative integer"));
        return PROTO_NONE;
    }
    long long skipCount = (limitNum > static_cast<double>(0x7FFFFFFFLL))
        ? 0x7FFFFFFFLL : static_cast<long long>(limitNum);
    if (!iterGetNext(ctx, self, "drop")) return PROTO_NONE;
    return makeHelperIterator(ctx, iteratorDropNext, self, PROTO_NONE, skipCount);
}

// flatMap stub: not implemented; method exists so typeof checks pass.
// Actual semantics require nested iterator dispatch which is more
// involved than the linear helpers above.
static const proto::ProtoObject* iteratorFlatMap(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Iterator.prototype.flatMap is not yet implemented"));
    return PROTO_NONE;
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

    s_iteratorProto = proto;
    return s_iteratorProto;
}

} // namespace protojs
