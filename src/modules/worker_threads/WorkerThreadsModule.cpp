#include "WorkerThreadsModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../JSContext.h"
#include "../../EventLoop.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../runtime/ProtoBytecodeModule.h"
#include "../events/EventsModule.h"
#include "../../console.h"
#include <fstream>
#include <sstream>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <cstdio>
#include <cstring>

namespace protojs {

namespace {

// ---- Attribute keys ----------------------------------------------------
//
// IMPORTANT: do NOT cache via thread_local.  Each JSContextWrapper owns
// its own ProtoSpace and therefore its own SymbolTable, so the
// "__events__" symbol interned on the main space is a DIFFERENT pointer
// from the one interned on a worker space.  Caching across calls would
// silently mismatch attribute keys when a callback running on the main
// thread inspects an object that lives in the worker space.  Re-intern
// per-call: createSymbol is a hash-bucket lookup, very cheap.

const proto::ProtoString* keyEvents(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__events__");
}
const proto::ProtoString* keyWorkerState(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__worker_state__");
}
const proto::ProtoString* keyWorkerRef(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__worker_ref__");
}

// Counter — main.cpp's drain loop already checks getActiveWorkerCount.
std::atomic<int> g_activeWorkers{0};

// ---- Minimal JSON encode / decode for ProtoObject ----------------------
//
// Cross-thread message payloads need to traverse the worker / main
// JSContextWrapper boundary, and ProtoObjects bound to one ProtoSpace
// can't be passed into another.  The original module did the same
// dance through QuickJS's native JS_JSONStringify / JS_ParseJSON; here
// we do it directly in C++ so the migration doesn't depend on any
// JS-side polyfill being installed in the worker context.

void escapeJSONString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void serializeJSON(proto::ProtoContext* ctx,
                    const proto::ProtoObject* val,
                    std::string& out) {
    if (!val || val == PROTO_NONE) { out += "null"; return; }
    if (val == PROTO_TRUE)  { out += "true";  return; }
    if (val == PROTO_FALSE) { out += "false"; return; }
    if (!ctx) { out += "null"; return; }
    if (val->isString(ctx)) {
        std::string s;
        val->asString(ctx)->toUTF8String(ctx, s);
        escapeJSONString(s, out);
        return;
    }
    if (val->isInteger(ctx)) {
        out += std::to_string(val->asLong(ctx));
        return;
    }
    if (val->isFloat(ctx)) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", val->asDouble(ctx));
        out += buf;
        return;
    }
    // Array? Look for the canonical JS-array marker.
    const proto::ProtoString* arrKey = JSSymbols::isArray(ctx);
    if (arrKey) {
        const proto::ProtoObject* flag = val->getAttribute(ctx, arrKey, false);
        if (flag && flag != PROTO_NONE) {
            out.push_back('[');
            const proto::ProtoList* els = getArrayElements(ctx, val);
            long long n = els
                ? static_cast<long long>(els->getSize(ctx)) : 0;
            for (long long i = 0; i < n; ++i) {
                if (i > 0) out.push_back(',');
                serializeJSON(ctx, els->getAt(ctx, static_cast<int>(i)), out);
            }
            out.push_back(']');
            return;
        }
    }
    // Plain object: enumerate own attributes, skip internal markers.
    out.push_back('{');
    const proto::ProtoSparseList* own = val->getOwnAttributes(ctx);
    if (own) {
        const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
        bool first = true;
        while (it && it->hasNext(ctx)) {
            unsigned long rawKey = it->nextKey(ctx);
            const proto::ProtoObject* v = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            const proto::ProtoString* k =
                reinterpret_cast<const proto::ProtoString*>(rawKey);
            if (!k || !v) continue;
            std::string ks;
            k->toUTF8String(ctx, ks);
            // Skip our own internal markers and metadata.
            if (ks.size() >= 2 && ks[0] == '_' && ks[1] == '_') continue;
            if (!first) out.push_back(',');
            escapeJSONString(ks, out);
            out.push_back(':');
            serializeJSON(ctx, v, out);
            first = false;
        }
    }
    out.push_back('}');
}

// ---- JSON parser -------------------------------------------------------

struct JSONParser {
    const char* p;
    const char* end;
    proto::ProtoContext* ctx;

    void skipWS() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++p; }
            else break;
        }
    }
    const proto::ProtoObject* parseValue();
    const proto::ProtoObject* parseString();
    const proto::ProtoObject* parseNumber();
    const proto::ProtoObject* parseArray();
    const proto::ProtoObject* parseObject();
};

const proto::ProtoObject* JSONParser::parseString() {
    if (p >= end || *p != '"') return PROTO_NONE;
    ++p;
    std::string out;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char esc = *p++;
            switch (esc) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'u': {
                    if (p + 4 > end) return PROTO_NONE;
                    int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++;
                        int d = (h >= '0' && h <= '9') ? h - '0'
                              : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                              : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                        if (d < 0) return PROTO_NONE;
                        code = (code << 4) | d;
                    }
                    if (code < 0x80) out.push_back(static_cast<char>(code));
                    else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(esc); break;
            }
        } else {
            out.push_back(c);
        }
    }
    if (p >= end || *p != '"') return PROTO_NONE;
    ++p;
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* JSONParser::parseNumber() {
    const char* start = p;
    if (p < end && (*p == '-' || *p == '+')) ++p;
    bool isFloat = false;
    while (p < end) {
        char c = *p;
        if (c >= '0' && c <= '9') { ++p; continue; }
        if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            isFloat = true; ++p; continue;
        }
        break;
    }
    std::string s(start, p - start);
    if (isFloat) return ctx->fromUTF8String(s.c_str());  // float fallback
    try {
        return ctx->fromInteger(std::stoll(s));
    } catch (...) {
        return PROTO_NONE;
    }
}

const proto::ProtoObject* JSONParser::parseArray() {
    if (p >= end || *p != '[') return PROTO_NONE;
    ++p;
    skipWS();
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    const proto::ProtoList* els = ctx->newList();
    if (p < end && *p == ']') { ++p; setArrayElements(ctx, arr, els); return arr; }
    while (p < end) {
        skipWS();
        const proto::ProtoObject* v = parseValue();
        els = els->appendLast(ctx, v ? v : PROTO_NONE);
        skipWS();
        if (p < end && *p == ',') { ++p; continue; }
        if (p < end && *p == ']') { ++p; break; }
        return PROTO_NONE;
    }
    setArrayElements(ctx, arr, els);
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) arr->setAttribute(ctx, lk,
        ctx->fromInteger(static_cast<long long>(els->getSize(ctx))));
    return arr;
}

const proto::ProtoObject* JSONParser::parseObject() {
    if (p >= end || *p != '{') return PROTO_NONE;
    ++p;
    skipWS();
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    if (p < end && *p == '}') { ++p; return obj; }
    while (p < end) {
        skipWS();
        const proto::ProtoObject* keyObj = parseString();
        if (!keyObj || keyObj == PROTO_NONE) return PROTO_NONE;
        skipWS();
        if (p >= end || *p != ':') return PROTO_NONE;
        ++p;
        skipWS();
        const proto::ProtoObject* v = parseValue();
        const proto::ProtoString* k = keyObj->asString(ctx);
        if (k) obj->setAttribute(ctx, k, v ? v : PROTO_NONE);
        skipWS();
        if (p < end && *p == ',') { ++p; continue; }
        if (p < end && *p == '}') { ++p; break; }
        return PROTO_NONE;
    }
    return obj;
}

const proto::ProtoObject* JSONParser::parseValue() {
    skipWS();
    if (p >= end) return PROTO_NONE;
    char c = *p;
    if (c == '"') return parseString();
    if (c == '[') return parseArray();
    if (c == '{') return parseObject();
    if (c == '-' || c == '+' || (c >= '0' && c <= '9')) return parseNumber();
    if (end - p >= 4 && std::strncmp(p, "true",  4) == 0) { p += 4; return PROTO_TRUE; }
    if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) { p += 5; return PROTO_FALSE; }
    if (end - p >= 4 && std::strncmp(p, "null",  4) == 0) { p += 4; return PROTO_NONE; }
    return PROTO_NONE;
}

const proto::ProtoObject* parseJSON(proto::ProtoContext* ctx,
                                      const std::string& text) {
    if (!ctx) return PROTO_NONE;
    JSONParser pr{text.data(), text.data() + text.size(), ctx};
    return pr.parseValue();
}

// ---- EventEmitter helper ----------------------------------------------
//
// Build a fresh EventEmitter instance.  We bypass the JS-level
// constructor (which is a no-op) and just clone the prototype chain
// directly: events.EventEmitter.prototype → newChild.

const proto::ProtoObject* makeEventEmitterInstance(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* nativeGlobal) {
    if (!ctx || !nativeGlobal) return ctx ? ctx->newObject(true) : nullptr;
    const proto::ProtoString* eventsKey =
        ctx->fromUTF8String("events")->asString(ctx);
    if (!eventsKey) return ctx->newObject(true);
    const proto::ProtoObject* eventsMod =
        nativeGlobal->getAttribute(ctx, eventsKey, false);
    if (!eventsMod || eventsMod == PROTO_NONE) return ctx->newObject(true);
    const proto::ProtoString* eeKey =
        ctx->fromUTF8String("EventEmitter")->asString(ctx);
    const proto::ProtoObject* eeCtor =
        eeKey ? eventsMod->getAttribute(ctx, eeKey, false) : nullptr;
    if (!eeCtor || eeCtor == PROTO_NONE) return ctx->newObject(true);
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* eeProto =
        protoKey ? eeCtor->getAttribute(ctx, protoKey, false) : nullptr;
    if (!eeProto || eeProto == PROTO_NONE) return ctx->newObject(true);
    return eeProto->newChild(ctx, /*mutable=*/true);
}

// Forward declare: workerThreadEntry (the thread body) uses these.
struct WorkerState;

void workerThreadEntry(WorkerState* state);

// ---- WorkerState (carried via ExternalPointer) -------------------------

struct WorkerState {
    JSContextWrapper* mainWrapper{nullptr};
    proto::ProtoRootSet::Handle workerPin{
        proto::ProtoRootSet::kNullHandle};
    std::unique_ptr<JSContextWrapper> workerWrapper;
    std::thread thread;
    std::string filename;
    std::string workerDataJson;
    std::atomic<bool> terminated{false};
    std::atomic<bool> running{false};
};

void freeWorkerState(void* p) {
    auto* s = static_cast<WorkerState*>(p);
    if (!s) return;
    s->terminated.store(true);
    s->running.store(false);
    if (s->thread.joinable()) s->thread.join();
    if (s->mainWrapper && s->workerPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = s->mainWrapper->getRootSet();
        if (rs) rs->remove(s->workerPin);
    }
    s->workerWrapper.reset();
    delete s;
}

WorkerState* getWorkerState(proto::ProtoContext* ctx,
                              const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoObject* attr =
        self->getAttribute(ctx, keyWorkerState(ctx), false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<WorkerState*>(ext->getPointer(ctx)) : nullptr;
}

// ---- Worker prototype methods (main side) ------------------------------

const proto::ProtoObject* invokeEEMethod(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoList* args) {
    if (!self) return PROTO_NONE;
    const proto::ProtoObject* ee =
        self->getAttribute(ctx, keyEvents(ctx), false);
    if (!ee || ee == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoString* mk =
        ctx->fromUTF8String(methodName)->asString(ctx);
    if (!mk) return PROTO_NONE;
    const proto::ProtoObject* fn = ee->getAttribute(ctx, mk, true);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    return callJSFunction(ctx, fn, ee, args ? args : ctx->newList());
}

// Async variant used from EventLoop callbacks: those do not run inside
// a runBytecode frame, so callJSFunction would lack thread-local
// module / global-root context.  callJSFunctionFromAsync restores
// both from the wrapper before delegating.
void invokeEEMethodFromAsync(
        JSContextWrapper* wrapper,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoList* args) {
    if (!wrapper || !self) return;
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) return;
    const proto::ProtoObject* ee =
        self->getAttribute(ctx, keyEvents(ctx), false);
    if (!ee || ee == PROTO_NONE) return;
    const proto::ProtoString* mk =
        ctx->fromUTF8String(methodName)->asString(ctx);
    if (!mk) return;
    const proto::ProtoObject* fn = ee->getAttribute(ctx, mk, true);
    if (!fn || fn == PROTO_NONE) return;
    const ProtoBytecodeModule* mod =
        static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    callJSFunctionFromAsync(ctx, fn, ee,
                             args ? args : ctx->newList(),
                             mod, wrapper->getNativeGlobalRootPtr());
}

const proto::ProtoObject* workerOn(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    invokeEEMethod(ctx, self, "on", args);
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* workerEmit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return invokeEEMethod(ctx, self, "emit", args);
}

const proto::ProtoObject* workerPostMessage(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    WorkerState* state = getWorkerState(ctx, self);
    if (!state || !state->workerWrapper || state->terminated.load())
        return PROTO_NONE;

    std::string payload;
    serializeJSON(ctx, args->getAt(ctx, 0), payload);

    JSContextWrapper* wrapperRaw = state->workerWrapper.get();
    EventLoop::getInstance().enqueueCallback(
        [wrapperRaw, payload]() {
            if (!wrapperRaw) return;
            JSContextWrapper::CurrentScope ws(wrapperRaw);
            proto::ProtoContext* wctx = wrapperRaw->getProtoContext();
            if (!wctx) return;
            const proto::ProtoObject* wg = wrapperRaw->getNativeGlobal();
            if (!wg) return;
            const proto::ProtoString* ppKey =
                wctx->fromUTF8String("parentPort")->asString(wctx);
            const proto::ProtoObject* pp =
                ppKey ? wg->getAttribute(wctx, ppKey, false) : nullptr;
            if (!pp || pp == PROTO_NONE) return;
            const proto::ProtoObject* msgVal = parseJSON(wctx, payload);
            const proto::ProtoList* a = wctx->newList()
                ->appendLast(wctx, wctx->fromUTF8String("message"))
                ->appendLast(wctx, msgVal);
            invokeEEMethodFromAsync(wrapperRaw, pp, "emit", a);
        });
    return PROTO_NONE;
}

const proto::ProtoObject* workerTerminate(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    WorkerState* state = getWorkerState(ctx, self);
    if (state) {
        state->terminated.store(true);
        state->running.store(false);
    }
    return PROTO_NONE;
}

const proto::ProtoObject* getWorkerProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"on",           workerOn},
        {"emit",         workerEmit},
        {"postMessage",  workerPostMessage},
        {"terminate",    workerTerminate},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 4);
    return proto;
}

// ---- Worker side: parentPort.postMessage -------------------------------
//
// Runs on the worker thread.  Serializes the value (worker space),
// captures by std::string into the EventLoop callback, and dispatches
// back into the main wrapper to emit('message') on the Worker instance.

const proto::ProtoObject* parentPortPostMessage(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* ref =
        self->getAttribute(ctx, keyWorkerRef(ctx), false);
    if (!ref) return PROTO_NONE;
    const proto::ProtoExternalPointer* refExt = ref->asExternalPointer(ctx);
    if (!refExt) return PROTO_NONE;
    auto* state = static_cast<WorkerState*>(refExt->getPointer(ctx));
    if (!state || !state->mainWrapper) return PROTO_NONE;

    std::string payload;
    serializeJSON(ctx, args->getAt(ctx, 0), payload);

    JSContextWrapper* mainWrapper = state->mainWrapper;
    proto::ProtoRootSet::Handle pin = state->workerPin;
    EventLoop::getInstance().enqueueCallback(
        [mainWrapper, pin, payload]() {
            if (!mainWrapper) return;
            JSContextWrapper::CurrentScope ws(mainWrapper);
            proto::ProtoContext* mctx = mainWrapper->getProtoContext();
            if (!mctx) return;
            proto::ProtoRootSet* rs = mainWrapper->getRootSet();
            const proto::ProtoObject* worker =
                rs ? rs->resolve(pin) : nullptr;
            if (!worker || worker == PROTO_NONE) return;
            const proto::ProtoObject* msgVal = parseJSON(mctx, payload);
            const proto::ProtoList* a = mctx->newList()
                ->appendLast(mctx, mctx->fromUTF8String("message"))
                ->appendLast(mctx, msgVal);
            invokeEEMethodFromAsync(mainWrapper, worker, "emit", a);
        });
    return PROTO_NONE;
}

const proto::ProtoObject* getParentPortProto(proto::ProtoContext* ctx) {
    static thread_local const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"postMessage", parentPortPostMessage},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 1);
    return proto;
}

// ---- Worker thread body ------------------------------------------------

void emitOnWorker(JSContextWrapper* mainWrapper,
                   proto::ProtoRootSet::Handle pin,
                   const std::string& eventName,
                   const std::string& argJson,
                   bool argIsString) {
    EventLoop::getInstance().enqueueCallback(
        [mainWrapper, pin, eventName, argJson, argIsString]() {
            if (!mainWrapper) return;
            JSContextWrapper::CurrentScope ws(mainWrapper);
            proto::ProtoContext* mctx = mainWrapper->getProtoContext();
            if (!mctx) return;
            proto::ProtoRootSet* rs = mainWrapper->getRootSet();
            const proto::ProtoObject* worker =
                rs ? rs->resolve(pin) : nullptr;
            if (!worker || worker == PROTO_NONE) return;
            const proto::ProtoList* a = mctx->newList()
                ->appendLast(mctx, mctx->fromUTF8String(eventName.c_str()));
            if (!argJson.empty()) {
                const proto::ProtoObject* arg = argIsString
                    ? mctx->fromUTF8String(argJson.c_str())
                    : parseJSON(mctx, argJson);
                a = a->appendLast(mctx, arg);
            }
            invokeEEMethodFromAsync(mainWrapper, worker, "emit", a);
        });
}

void workerThreadEntry(WorkerState* state) {
    if (!state) return;
    state->running.store(true);

    // Build the worker's own JSContextWrapper.  setUseProtoEval keeps
    // the worker's eval on the protoCore path.
    auto wrapper = std::make_unique<JSContextWrapper>(0, 0, 3.0);
    wrapper->setUseProtoEval(true);
    state->workerWrapper = std::move(wrapper);

    // Initialise events module on the worker's native global so the
    // user's worker script (and our parentPort) can use EventEmitter.
    {
        JSContextWrapper::CurrentScope ws(state->workerWrapper.get());
        proto::ProtoContext* wctx = state->workerWrapper->getProtoContext();
        if (!wctx) {
            state->running.store(false);
            g_activeWorkers.fetch_sub(1);
            return;
        }
        const proto::ProtoObject* wg = state->workerWrapper->getNativeGlobal();
        // Console + EventsModule on the worker's global so the worker
        // script can call console.log() and parentPort.on() out of the
        // box.  Console::init mutates wg in-place; events module
        // returns the new pointer.
        Console::init(wctx, wg);
        wg = EventsModule::init(wctx, wg);
        state->workerWrapper->updateNativeGlobal(wg);

        // Build parentPort instance from a prototype + an EventEmitter
        // mixin so users can call parentPort.on('message', cb).
        const proto::ProtoObject* ppProto = getParentPortProto(wctx);
        const proto::ProtoObject* parentPort = ppProto
            ? ppProto->newChild(wctx, /*mutable=*/true)
            : wctx->newObject(/*mutable=*/true);
        // Mix in an EventEmitter as a delegate stored under __events__,
        // and forward parentPort.on/emit to it via the same trick used
        // by Worker.  For simplicity we just install on/emit directly
        // on parentPort by pulling them off the EventEmitter prototype.
        const proto::ProtoObject* ee =
            makeEventEmitterInstance(wctx, wg);
        parentPort->setAttribute(wctx, keyEvents(wctx), ee);
        // Forward on/emit by referencing the same listeners object.
        // The simplest approach: copy on/emit/once/removeListener
        // from the EventEmitter prototype straight onto parentPort,
        // using __events__ as the receiver target — but our EE
        // methods use `self` as the EE itself, not via a delegate.
        // So we add tiny shim methods that forward via invokeEEMethod.
        struct Shim {
            static const proto::ProtoObject* on_(
                proto::ProtoContext* c,
                const proto::ProtoObject* s,
                const proto::ParentLink*,
                const proto::ProtoList* a,
                const proto::ProtoSparseList*) {
                invokeEEMethod(c, s, "on", a);
                return s;
            }
            static const proto::ProtoObject* emit_(
                proto::ProtoContext* c,
                const proto::ProtoObject* s,
                const proto::ParentLink*,
                const proto::ProtoList* a,
                const proto::ProtoSparseList*) {
                return invokeEEMethod(c, s, "emit", a);
            }
        };
        // Use thread-local cached shims to avoid rebuilding per thread.
        static thread_local const proto::ProtoObject* ppShimsProto = nullptr;
        if (!ppShimsProto) {
            static const NativeEntry e[] = {
                {"on",   Shim::on_},
                {"emit", Shim::emit_},
                NATIVE_MODULE_END
            };
            ppShimsProto = ProtoNativeModule::buildModule(wctx, e, 2);
        }
        if (ppShimsProto) {
            // Copy the on/emit methods onto parentPort directly.
            const proto::ProtoString* onK =
                wctx->fromUTF8String("on")->asString(wctx);
            const proto::ProtoString* emK =
                wctx->fromUTF8String("emit")->asString(wctx);
            if (onK)
                parentPort->setAttribute(wctx, onK,
                    ppShimsProto->getAttribute(wctx, onK, true));
            if (emK)
                parentPort->setAttribute(wctx, emK,
                    ppShimsProto->getAttribute(wctx, emK, true));
        }
        // Pin a back-reference so parentPort.postMessage can resolve
        // the WorkerState (and from there the main wrapper + pin).
        const proto::ProtoObject* refExt =
            wctx->fromExternalPointer(state, /*finalizer=*/nullptr);
        if (refExt) parentPort->setAttribute(wctx, keyWorkerRef(wctx), refExt);

        // Install parentPort on the worker global.
        const proto::ProtoString* ppKey =
            wctx->fromUTF8String("parentPort")->asString(wctx);
        if (ppKey) {
            wg = wg->setAttribute(wctx, ppKey, parentPort);
            state->workerWrapper->updateNativeGlobal(wg);
        }

        // workerData (parsed from the JSON snapshot the main side
        // produced).  Empty json → leave undefined.
        if (!state->workerDataJson.empty()) {
            const proto::ProtoObject* wd =
                parseJSON(wctx, state->workerDataJson);
            const proto::ProtoString* wdKey =
                wctx->fromUTF8String("workerData")->asString(wctx);
            if (wdKey && wd) {
                wg = wg->setAttribute(wctx, wdKey, wd);
                state->workerWrapper->updateNativeGlobal(wg);
            }
        }
    }

    // Read and eval the file.  Eval is QuickJS-side here because the
    // wrapper.eval entry point is still the QuickJS eval; protoCore
    // path is selected via setUseProtoEval(true) (already done).
    std::ifstream file(state->filename);
    if (!file.is_open()) {
        emitOnWorker(state->mainWrapper, state->workerPin, "error",
                     "Cannot open file: " + state->filename, true);
        state->running.store(false);
        g_activeWorkers.fetch_sub(1);
        return;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string code = buf.str();

    JSValue rv = state->workerWrapper->eval(code, state->filename);
    if (JS_IsException(rv)) {
        JSContext* wctx = state->workerWrapper->getJSContext();
        JSValue exc = JS_GetException(wctx);
        const char* err = JS_ToCString(wctx, exc);
        std::string msg = err ? err : "worker exception";
        if (err) JS_FreeCString(wctx, err);
        JS_FreeValue(wctx, exc);
        emitOnWorker(state->mainWrapper, state->workerPin, "error", msg, true);
    }
    JS_FreeValue(state->workerWrapper->getJSContext(), rv);

    emitOnWorker(state->mainWrapper, state->workerPin, "exit", "", true);
    state->running.store(false);
    g_activeWorkers.fetch_sub(1);
}

// ---- Worker constructor (main side) ------------------------------------

const proto::ProtoObject* workerConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fileArg = args->getAt(ctx, 0);
    if (!fileArg || !fileArg->isString(ctx)) return PROTO_NONE;
    std::string filename;
    fileArg->asString(ctx)->toUTF8String(ctx, filename);

    // workerData: pull from options.workerData and serialise.
    std::string workerDataJson;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* opts = args->getAt(ctx, 1);
        if (opts && opts != PROTO_NONE) {
            const proto::ProtoString* wdKey =
                ctx->fromUTF8String("workerData")->asString(ctx);
            const proto::ProtoObject* wdv =
                wdKey ? opts->getAttribute(ctx, wdKey, false) : nullptr;
            if (wdv && wdv != PROTO_NONE) serializeJSON(ctx, wdv, workerDataJson);
        }
    }

    JSContextWrapper* mainWrapper = JSContextWrapper::current();
    if (!mainWrapper) return PROTO_NONE;

    // Build the Worker instance.
    const proto::ProtoObject* proto = getWorkerProto(ctx);
    const proto::ProtoObject* worker = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

    // Attach an EventEmitter (so on/emit forwarding has a target).
    const proto::ProtoObject* ee =
        makeEventEmitterInstance(ctx, mainWrapper->getNativeGlobal());
    worker->setAttribute(ctx, keyEvents(ctx), ee);

    auto* state = new WorkerState{};
    state->mainWrapper = mainWrapper;
    state->filename    = filename;
    state->workerDataJson = std::move(workerDataJson);

    // Pin the Worker instance so the worker thread can resolve it
    // when emitting message/error/exit events.
    proto::ProtoRootSet* rs = mainWrapper->getRootSet();
    if (rs) state->workerPin = rs->add(worker);

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeWorkerState);
    if (!extPtr) {
        if (rs && state->workerPin != proto::ProtoRootSet::kNullHandle) {
            rs->remove(state->workerPin);
        }
        delete state;
        return PROTO_NONE;
    }
    worker->setAttribute(ctx, keyWorkerState(ctx), extPtr);

    g_activeWorkers.fetch_add(1);
    state->thread = std::thread([state]() { workerThreadEntry(state); });
    return worker;
}

// ---- Module-level functions: isMainThread / parentPort / workerData ---
//
// In a Worker, these are populated on the worker's own global (see
// workerThreadEntry).  At module-init time on the main side the
// wrapper is the main thread's, so `isMainThread` is true and
// `parentPort` / `workerData` are absent / null.

const proto::ProtoObject* isMainThreadImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    JSContextWrapper* w = JSContextWrapper::current();
    // The main wrapper is the first one created; subsequent wrappers
    // are workers.  We don't have a flag for this distinction yet, so
    // approximate: a wrapper is a worker if its global has parentPort.
    if (!w) return PROTO_TRUE;
    proto::ProtoContext* c = w->getProtoContext();
    const proto::ProtoObject* g = w->getNativeGlobal();
    if (!c || !g) return PROTO_TRUE;
    const proto::ProtoString* k =
        c->fromUTF8String("parentPort")->asString(c);
    const proto::ProtoObject* pp = k ? g->getAttribute(c, k, false) : nullptr;
    return (pp && pp != PROTO_NONE) ? PROTO_FALSE : PROTO_TRUE;
}

const proto::ProtoObject* parentPortImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w || !ctx) return PROTO_NONE;
    const proto::ProtoString* k =
        ctx->fromUTF8String("parentPort")->asString(ctx);
    const proto::ProtoObject* g = w->getNativeGlobal();
    if (!k || !g) return PROTO_NONE;
    const proto::ProtoObject* pp = g->getAttribute(ctx, k, false);
    return (pp && pp != PROTO_NONE) ? pp : PROTO_NONE;
}

const proto::ProtoObject* workerDataImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w || !ctx) return PROTO_NONE;
    const proto::ProtoString* k =
        ctx->fromUTF8String("workerData")->asString(ctx);
    const proto::ProtoObject* g = w->getNativeGlobal();
    if (!k || !g) return PROTO_NONE;
    const proto::ProtoObject* wd = g->getAttribute(ctx, k, false);
    return (wd && wd != PROTO_NONE) ? wd : PROTO_NONE;
}

}  // namespace

// ---- Public ------------------------------------------------------------

int WorkerThreadsModule::getActiveWorkerCount() {
    return g_activeWorkers.load();
}

const proto::ProtoObject* WorkerThreadsModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    const proto::ProtoObject* workerProto = getWorkerProto(ctx);

    // Build the Worker constructor (wrapNativeFunction so the runtime
    // dispatches via __native_fn__ + __construct__ for `new Worker(...)`).
    const proto::ProtoObject* workerCtor =
        wrapNativeFunction(ctx, workerConstructor, "Worker",
                            /*length=*/1, /*globalRoot=*/nullptr);
    if (!workerCtor) return globalObj;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey)
        workerCtor = workerCtor->setAttribute(ctx, protoKey, workerProto);
    {
        const proto::ProtoString* ck =
            ctx->fromUTF8String("__construct__")->asString(ctx);
        if (ck) workerCtor = workerCtor->setAttribute(ctx, ck,
            ctx->fromMethod(nullptr, workerConstructor));
    }

    // Build the worker_threads module object exposing Worker +
    // isMainThread / parentPort / workerData.
    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;
    {
        const proto::ProtoString* k =
            ctx->fromUTF8String("Worker")->asString(ctx);
        if (k) mod->setAttribute(ctx, k, workerCtor);
    }
    auto installFn = [&](const char* name,
                           proto::ProtoMethod fn) {
        const proto::ProtoString* k =
            ctx->fromUTF8String(name)->asString(ctx);
        if (!k) return;
        mod->setAttribute(ctx, k, ctx->fromMethod(nullptr, fn));
    };
    installFn("isMainThread", isMainThreadImpl);
    installFn("parentPort",   parentPortImpl);
    installFn("workerData",   workerDataImpl);

    return ProtoNativeModule::registerOnGlobal(
        ctx, globalObj, "worker_threads", mod);
}

} // namespace protojs
