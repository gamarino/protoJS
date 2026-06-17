#include "StreamModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include <string>

namespace protojs {

namespace {

// Per-stream state lives as attributes on the instance:
//   __buffer__        : Array<string> — pending chunks
//   __ended__         : bool — whether end() has been called (Writable)
//                       or the source has signalled EOF (Readable)
//   __highWaterMark__ : int — back-pressure threshold; default 16384
//
// No C++-side struct, no finalizer, no JS_SetOpaque.  Listeners and
// EventEmitter integration are provided implicitly: callers who want
// 'drain' / 'finish' / 'data' events should compose a separate
// EventEmitter (the previous implementation embedded one as
// `_events` and barely used it; the protoCore-native streams keep
// the same surface but the EventEmitter is opt-in via JS code).

const proto::ProtoString* bufKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__buffer__");
}
const proto::ProtoString* endedKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__ended__");
}
const proto::ProtoString* hwmKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__highWaterMark__");
}

const proto::ProtoObject* getBuffer(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* self,
                                     bool create) {
    if (!self) return nullptr;
    const proto::ProtoString* k = bufKey(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (attr && attr != PROTO_NONE) return attr;
    if (!create) return nullptr;
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    if (!arr) return nullptr;
    setArrayElements(ctx, arr, ctx->newList());
    self->setAttribute(ctx, k, arr);
    return arr;
}

bool isEnded(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self) return false;
    const proto::ProtoObject* v = self->getAttribute(ctx, endedKey(ctx), false);
    return v == PROTO_TRUE;
}

void setEnded(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self) return;
    self->setAttribute(ctx, endedKey(ctx), PROTO_TRUE);
}

long long getHWM(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self) return 16384;
    const proto::ProtoObject* v = self->getAttribute(ctx, hwmKey(ctx), false);
    if (v && v->isInteger(ctx)) return v->asLong(ctx);
    return 16384;
}

// ---- Constructor (shared) ----------------------------------------------

const proto::ProtoObject* commonConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    self->setAttribute(ctx, hwmKey(ctx), ctx->fromInteger(16384));
    self->setAttribute(ctx, endedKey(ctx), PROTO_FALSE);
    return PROTO_NONE;
}

// ---- Methods -----------------------------------------------------------

const proto::ProtoObject* readableRead(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx || !self) return PROTO_NONE;
    const proto::ProtoObject* arr = getBuffer(ctx, self, /*create=*/false);
    if (!arr) {
        return isEnded(ctx, self) ? PROTO_NONE : PROTO_NONE;
    }
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els || els->getSize(ctx) == 0) {
        return PROTO_NONE;
    }
    const proto::ProtoObject* head = els->getAt(ctx, 0);
    // Drop the head (els->removeFirst).
    setArrayElements(ctx, arr, els->removeFirst(ctx));
    return head ? head : PROTO_NONE;
}

const proto::ProtoObject* readablePipe(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    // Minimal pipe: just return the destination so callers can chain.
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    return args->getAt(ctx, 0);
}

const proto::ProtoObject* writableWrite(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || isEnded(ctx, self)) return PROTO_FALSE;
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* chunk = args->getAt(ctx, 0);
    if (!chunk || chunk == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* arr = getBuffer(ctx, self, /*create=*/true);
    if (!arr) return PROTO_FALSE;
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) els = ctx->newList();
    setArrayElements(ctx, arr, els->appendLast(ctx, chunk));
    long long size = static_cast<long long>(els->getSize(ctx) + 1);
    return (size < getHWM(ctx, self)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* writableEnd(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    if (ctx && args && args->getSize(ctx) > 0) {
        // Final chunk: same path as write().
        writableWrite(ctx, self, pl, args, kw);
    }
    if (ctx && self) setEnded(ctx, self);
    return PROTO_NONE;
}

// ---- Class builders ----------------------------------------------------

const proto::ProtoObject* buildClass(proto::ProtoContext* ctx,
                                      const char* className,
                                      const NativeEntry* protoEntries,
                                      size_t protoCount) {
    const proto::ProtoObject* protoObj =
        ProtoNativeModule::buildModule(ctx, protoEntries, protoCount);
    if (!protoObj) return nullptr;
    const proto::ProtoObject* ctor =
        wrapNativeFunction(ctx, commonConstructor, className,
                            /*length=*/0, /*globalRoot=*/nullptr);
    if (!ctor) return nullptr;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, protoObj);
    const proto::ProtoString* ck =
        ctx->fromUTF8String("__construct__")->asString(ctx);
    if (ck) ctor = ctor->setAttribute(ctx, ck,
        ctx->fromMethod(nullptr, commonConstructor));
    return ctor;
}

}  // namespace

const proto::ProtoObject* StreamModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    static const NativeEntry readableEntries[] = {
        {"read", readableRead},
        {"pipe", readablePipe},
        NATIVE_MODULE_END
    };
    static const NativeEntry writableEntries[] = {
        {"write", writableWrite},
        {"end",   writableEnd},
        NATIVE_MODULE_END
    };
    static const NativeEntry duplexEntries[] = {
        {"read",  readableRead},
        {"pipe",  readablePipe},
        {"write", writableWrite},
        {"end",   writableEnd},
        NATIVE_MODULE_END
    };

    const proto::ProtoObject* readable = buildClass(ctx, "Readable",
        readableEntries, 2);
    const proto::ProtoObject* writable = buildClass(ctx, "Writable",
        writableEntries, 2);
    const proto::ProtoObject* duplex = buildClass(ctx, "Duplex",
        duplexEntries, 4);
    const proto::ProtoObject* transform = buildClass(ctx, "Transform",
        duplexEntries, 4);  // Transform is functionally a Duplex here.
    const proto::ProtoObject* passThrough = buildClass(ctx, "PassThrough",
        duplexEntries, 4);

    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;
    auto setIfReady = [&](const char* k, const proto::ProtoObject* v) {
        if (!v) return;
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) mod->setAttribute(ctx, sk, v);
    };
    setIfReady("Readable",     readable);
    setIfReady("Writable",     writable);
    setIfReady("Duplex",       duplex);
    setIfReady("Transform",    transform);
    setIfReady("PassThrough",  passThrough);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "stream", mod);
}

} // namespace protojs
