#include "NetModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../JSContext.h"
#include "../../EventLoop.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../runtime/ProtoBytecodeModule.h"
#include "../buffer/BufferModule.h"
#include "../events/EventsModule.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstring>

namespace protojs {

namespace {

// ---- Attribute keys (re-intern per call: SymbolTable is per-space) ----

const proto::ProtoString* keyEvents(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__events__");
}
const proto::ProtoString* keyServerState(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__server_state__");
}
const proto::ProtoString* keySocketState(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__socket_state__");
}

// Shared active-counter so main.cpp's drain loop keeps the process
// alive while any net Server / Socket has an in-flight thread.
std::atomic<int> g_active{0};

// ---- Argument helpers --------------------------------------------------

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

bool argInt(proto::ProtoContext* ctx, const proto::ProtoList* args,
             int idx, long long& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isInteger(ctx)) return false;
    out = a->asLong(ctx);
    return true;
}

// Read int / string fields out of an options object.
bool optInt(proto::ProtoContext* ctx, const proto::ProtoObject* opts,
              const char* name, long long& out) {
    if (!ctx || !opts) return false;
    const proto::ProtoString* k =
        ctx->fromUTF8String(name)->asString(ctx);
    if (!k) return false;
    const proto::ProtoObject* v = opts->getAttribute(ctx, k, false);
    if (!v || !v->isInteger(ctx)) return false;
    out = v->asLong(ctx);
    return true;
}
bool optString(proto::ProtoContext* ctx, const proto::ProtoObject* opts,
                 const char* name, std::string& out) {
    if (!ctx || !opts) return false;
    const proto::ProtoString* k =
        ctx->fromUTF8String(name)->asString(ctx);
    if (!k) return false;
    const proto::ProtoObject* v = opts->getAttribute(ctx, k, false);
    if (!v || !v->isString(ctx)) return false;
    v->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

// ---- EventEmitter helpers ---------------------------------------------
//
// Same pattern used by worker_threads: each Server / Socket carries an
// `__events__` attribute holding a fresh EventEmitter instance built
// from `events.EventEmitter.prototype`.  Worker-style invokeEEMethod*
// forwards on/emit through that delegate.

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

const proto::ProtoObject* invokeEEMethod(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoList* args) {
    if (!self || !ctx) return PROTO_NONE;
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

// ---- ServerState / SocketState -----------------------------------------
//
// Carried on the Server/Socket instances via ProtoExternalPointer so the
// std::thread + atomic flags + ProtoRootSet pin are all freed in a
// single C++ destructor when the JS instance is collected.

struct SocketState;

struct ServerState {
    JSContextWrapper* mainWrapper{nullptr};
    proto::ProtoRootSet::Handle serverPin{
        proto::ProtoRootSet::kNullHandle};
    std::atomic<int> socketFd{-1};
    int port{0};
    std::string host;
    std::atomic<bool> listening{false};
    std::atomic<bool> closed{false};
    std::thread acceptThread;
};

struct SocketState {
    JSContextWrapper* mainWrapper{nullptr};
    proto::ProtoRootSet::Handle socketPin{
        proto::ProtoRootSet::kNullHandle};
    std::atomic<int> socketFd{-1};
    std::atomic<bool> connected{false};
    std::atomic<bool> destroyed{false};
    std::string remoteAddress;
    int remotePort{0};
    std::string localAddress;
    int localPort{0};
    std::thread readThread;
};

void teardownServer(ServerState* s) {
    if (!s) return;
    bool wasListening = s->listening.exchange(false);
    s->closed.store(true);
    int fd = s->socketFd.exchange(-1);
    if (fd >= 0) { ::shutdown(fd, SHUT_RDWR); ::close(fd); }
    if (s->acceptThread.joinable()) s->acceptThread.join();
    if (wasListening) g_active.fetch_sub(1);
}

void teardownSocket(SocketState* s) {
    if (!s) return;
    bool wasReading = s->connected.exchange(false);
    s->destroyed.store(true);
    int fd = s->socketFd.exchange(-1);
    if (fd >= 0) { ::shutdown(fd, SHUT_RDWR); ::close(fd); }
    if (s->readThread.joinable()) s->readThread.join();
    if (wasReading) g_active.fetch_sub(1);
}

void freeServerState(void* p) {
    auto* s = static_cast<ServerState*>(p);
    if (!s) return;
    teardownServer(s);
    if (s->mainWrapper && s->serverPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = s->mainWrapper->getRootSet();
        if (rs) rs->remove(s->serverPin);
    }
    delete s;
}

void freeSocketState(void* p) {
    auto* s = static_cast<SocketState*>(p);
    if (!s) return;
    teardownSocket(s);
    if (s->mainWrapper && s->socketPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = s->mainWrapper->getRootSet();
        if (rs) rs->remove(s->socketPin);
    }
    delete s;
}

ServerState* getServerState(proto::ProtoContext* ctx,
                              const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoObject* attr =
        self->getAttribute(ctx, keyServerState(ctx), false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<ServerState*>(ext->getPointer(ctx)) : nullptr;
}
SocketState* getSocketState(proto::ProtoContext* ctx,
                              const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoObject* attr =
        self->getAttribute(ctx, keySocketState(ctx), false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<SocketState*>(ext->getPointer(ctx)) : nullptr;
}

// ---- Forward declarations ---------------------------------------------

const proto::ProtoObject* getSocketProto(proto::ProtoContext* ctx);
const proto::ProtoObject* getServerProto(proto::ProtoContext* ctx);

const proto::ProtoObject* makeSocketInstance(
        JSContextWrapper* wrapper,
        proto::ProtoContext* ctx,
        const proto::ProtoObject* nativeGlobal);

void startSocketReadLoop(JSContextWrapper* wrapper,
                          proto::ProtoRootSet::Handle pin,
                          SocketState* state);

// Coerce a value to bytes for write().  Accepts string or Buffer.
std::vector<uint8_t> toBytes(proto::ProtoContext* ctx,
                               const proto::ProtoObject* val) {
    std::vector<uint8_t> out;
    if (!ctx || !val || val == PROTO_NONE) return out;
    if (val->isString(ctx)) {
        std::string s;
        val->asString(ctx)->toUTF8String(ctx, s);
        out.assign(s.begin(), s.end());
        return out;
    }
    // Buffer instance: pull __byte_buffer__ as ProtoByteBuffer.
    const proto::ProtoString* bbKey =
        ctx->fromUTF8String("__byte_buffer__")->asString(ctx);
    if (!bbKey) return out;
    const proto::ProtoObject* attr = val->getAttribute(ctx, bbKey, false);
    if (!attr || attr == PROTO_NONE) return out;
    const proto::ProtoByteBuffer* bb = attr->asByteBuffer(ctx);
    if (!bb) return out;
    unsigned long n = bb->getSize(ctx);
    const char* src = bb->getBuffer(ctx);
    out.assign(src, src + n);
    return out;
}

// Build a Buffer instance directly from a byte slice.  Needed when the
// read loop hands raw bytes back to the JS event listener.
const proto::ProtoObject* makeBufferFromBytes(
        proto::ProtoContext* ctx,
        const char* data, size_t len) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* bufObj = ctx->newBuffer(len);
    if (!bufObj) return PROTO_NONE;
    const proto::ProtoByteBuffer* bb = bufObj->asByteBuffer(ctx);
    if (bb && len) std::memcpy(bb->getBuffer(ctx), data, len);

    // Wrap into a Buffer instance using the global Buffer prototype if
    // available; if not (Buffer module wasn't installed), return the
    // raw ProtoByteBuffer object — the `data` listener can still
    // toString it via JS, but it's the best we can do without a
    // public Buffer prototype hook.
    JSContextWrapper* w = JSContextWrapper::current();
    const proto::ProtoObject* g = w ? w->getNativeGlobal() : nullptr;
    if (!g) return bufObj;
    const proto::ProtoString* bk =
        ctx->fromUTF8String("Buffer")->asString(ctx);
    const proto::ProtoObject* bCtor =
        bk ? g->getAttribute(ctx, bk, false) : nullptr;
    if (!bCtor) return bufObj;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* bProto =
        protoKey ? bCtor->getAttribute(ctx, protoKey, false) : nullptr;
    if (!bProto || bProto == PROTO_NONE) return bufObj;
    const proto::ProtoObject* inst = bProto->newChild(ctx, true);
    inst->setAttribute(ctx,
        ctx->fromUTF8String("__byte_buffer__")->asString(ctx),
        bufObj);
    inst->setAttribute(ctx,
        ctx->fromUTF8String("__is_buffer__")->asString(ctx),
        PROTO_TRUE);
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) inst->setAttribute(ctx, lk,
        ctx->fromInteger(static_cast<long long>(len)));
    return inst;
}

// ---- Socket prototype methods ------------------------------------------

const proto::ProtoObject* socketOnImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    invokeEEMethod(ctx, self, "on", args);
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* socketEmitImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return invokeEEMethod(ctx, self, "emit", args);
}

const proto::ProtoObject* socketWriteImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SocketState* s = getSocketState(ctx, self);
    if (!s || !s->connected.load() || !args || args->getSize(ctx) == 0)
        return PROTO_FALSE;
    std::vector<uint8_t> bytes = toBytes(ctx, args->getAt(ctx, 0));
    if (bytes.empty()) return PROTO_FALSE;
    int fd = s->socketFd.load();
    if (fd < 0) return PROTO_FALSE;
    ssize_t sent;
    {
        proto::ProtoContext::UnmanagedScope u(ctx);
        sent = ::send(fd, bytes.data(), bytes.size(), 0);
    }
    return (sent > 0) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* socketEndImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    if (args && args->getSize(ctx) > 0) {
        socketWriteImpl(ctx, self, pl, args, kw);
    }
    SocketState* s = getSocketState(ctx, self);
    if (s) {
        int fd = s->socketFd.load();
        if (fd >= 0 && s->connected.load()) ::shutdown(fd, SHUT_WR);
    }
    return PROTO_NONE;
}

const proto::ProtoObject* socketDestroyImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    SocketState* s = getSocketState(ctx, self);
    if (s) {
        proto::ProtoContext::UnmanagedScope u(ctx);
        teardownSocket(s);
    }
    return PROTO_NONE;
}

const proto::ProtoObject* socketAddressImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    SocketState* s = getSocketState(ctx, self);
    if (!s || !s->connected.load()) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    obj->setAttribute(ctx,
        ctx->fromUTF8String("port")->asString(ctx),
        ctx->fromInteger(s->localPort));
    obj->setAttribute(ctx,
        ctx->fromUTF8String("family")->asString(ctx),
        ctx->fromUTF8String("IPv4"));
    obj->setAttribute(ctx,
        ctx->fromUTF8String("address")->asString(ctx),
        ctx->fromUTF8String(s->localAddress.c_str()));
    return obj;
}

// `socket.connect(port, host?, callback?)` or
// `socket.connect({ port, host }, callback?)`.
const proto::ProtoObject* socketConnectImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SocketState* s = getSocketState(ctx, self);
    if (!s || s->connected.load()) return self ? self : PROTO_NONE;

    long long port = 0;
    std::string host = "127.0.0.1";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) {
            port = a0->asLong(ctx);
            if (args->getSize(ctx) > 1) argString(ctx, args, 1, host);
        } else if (a0) {
            optInt(ctx, a0, "port", port);
            optString(ctx, a0, "host", host);
        }
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return self;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        // Fallback for "localhost".
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return self;
    }
    s->socketFd.store(fd);
    s->connected.store(true);
    s->remoteAddress = host;
    s->remotePort = static_cast<int>(port);
    sockaddr_in la{};
    socklen_t llen = sizeof(la);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&la), &llen) == 0) {
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &la.sin_addr, ip, sizeof(ip));
        s->localAddress = ip;
        s->localPort = ntohs(la.sin_port);
    }

    JSContextWrapper* wrapper = s->mainWrapper;
    proto::ProtoRootSet::Handle pin = s->socketPin;

    // Emit 'connect' asynchronously so the caller has a chance to
    // register listeners before the event fires.
    EventLoop::getInstance().enqueueCallback([wrapper, pin]() {
        if (!wrapper) return;
        JSContextWrapper::CurrentScope ws(wrapper);
        proto::ProtoContext* mctx = wrapper->getProtoContext();
        if (!mctx) return;
        proto::ProtoRootSet* rs = wrapper->getRootSet();
        const proto::ProtoObject* sock = rs ? rs->resolve(pin) : nullptr;
        if (!sock || sock == PROTO_NONE) return;
        const proto::ProtoList* a = mctx->newList()
            ->appendLast(mctx, mctx->fromUTF8String("connect"));
        invokeEEMethodFromAsync(wrapper, sock, "emit", a);
    });

    g_active.fetch_add(1);
    startSocketReadLoop(wrapper, pin, s);
    return self;
}

const proto::ProtoObject* getSocketProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"on",       socketOnImpl},
        {"emit",     socketEmitImpl},
        {"connect",  socketConnectImpl},
        {"write",    socketWriteImpl},
        {"end",      socketEndImpl},
        {"destroy",  socketDestroyImpl},
        {"address",  socketAddressImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 7);
    return proto;
}

const proto::ProtoObject* makeSocketInstance(
        JSContextWrapper* wrapper,
        proto::ProtoContext* ctx,
        const proto::ProtoObject* nativeGlobal) {
    const proto::ProtoObject* proto = getSocketProto(ctx);
    const proto::ProtoObject* sock = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

    const proto::ProtoObject* ee =
        makeEventEmitterInstance(ctx, nativeGlobal);
    sock->setAttribute(ctx, keyEvents(ctx), ee);

    auto* state = new SocketState{};
    state->mainWrapper = wrapper;
    proto::ProtoRootSet* rs = wrapper ? wrapper->getRootSet() : nullptr;
    if (rs) state->socketPin = rs->add(sock);

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeSocketState);
    if (!extPtr) {
        if (rs && state->socketPin != proto::ProtoRootSet::kNullHandle)
            rs->remove(state->socketPin);
        delete state;
        return PROTO_NONE;
    }
    sock->setAttribute(ctx, keySocketState(ctx), extPtr);
    return sock;
}

void startSocketReadLoop(JSContextWrapper* wrapper,
                          proto::ProtoRootSet::Handle pin,
                          SocketState* state) {
    state->readThread = std::thread([wrapper, pin, state]() {
        char buffer[4096];
        while (state->connected.load() && !state->destroyed.load()) {
            int fd = state->socketFd.load();
            if (fd < 0) break;
            ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                state->connected.store(false);
                break;
            }
            std::string chunk(buffer, static_cast<size_t>(n));
            EventLoop::getInstance().enqueueCallback(
                [wrapper, pin, chunk]() {
                    if (!wrapper) return;
                    JSContextWrapper::CurrentScope ws(wrapper);
                    proto::ProtoContext* mctx = wrapper->getProtoContext();
                    if (!mctx) return;
                    proto::ProtoRootSet* rs = wrapper->getRootSet();
                    const proto::ProtoObject* sock =
                        rs ? rs->resolve(pin) : nullptr;
                    if (!sock || sock == PROTO_NONE) return;
                    const proto::ProtoObject* buf =
                        makeBufferFromBytes(mctx, chunk.data(),
                                              chunk.size());
                    const proto::ProtoList* a = mctx->newList()
                        ->appendLast(mctx, mctx->fromUTF8String("data"))
                        ->appendLast(mctx, buf);
                    invokeEEMethodFromAsync(wrapper, sock, "emit", a);
                });
        }
        // Emit 'end' once the read loop has terminated.
        EventLoop::getInstance().enqueueCallback([wrapper, pin]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope ws(wrapper);
            proto::ProtoContext* mctx = wrapper->getProtoContext();
            if (!mctx) return;
            proto::ProtoRootSet* rs = wrapper->getRootSet();
            const proto::ProtoObject* sock = rs ? rs->resolve(pin) : nullptr;
            if (!sock || sock == PROTO_NONE) return;
            const proto::ProtoList* a = mctx->newList()
                ->appendLast(mctx, mctx->fromUTF8String("end"));
            invokeEEMethodFromAsync(wrapper, sock, "emit", a);
        });
        // Drop the active-counter contribution made by the start path.
        g_active.fetch_sub(1);
    });
}

// ---- Server prototype methods ------------------------------------------

const proto::ProtoObject* serverOnImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    invokeEEMethod(ctx, self, "on", args);
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* serverEmitImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return invokeEEMethod(ctx, self, "emit", args);
}

const proto::ProtoObject* serverAddressImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    ServerState* s = getServerState(ctx, self);
    if (!s || !s->listening.load()) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    obj->setAttribute(ctx,
        ctx->fromUTF8String("port")->asString(ctx),
        ctx->fromInteger(s->port));
    obj->setAttribute(ctx,
        ctx->fromUTF8String("family")->asString(ctx),
        ctx->fromUTF8String("IPv4"));
    obj->setAttribute(ctx,
        ctx->fromUTF8String("address")->asString(ctx),
        ctx->fromUTF8String(s->host.c_str()));
    return obj;
}

const proto::ProtoObject* serverCloseImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    ServerState* s = getServerState(ctx, self);
    if (s) {
        proto::ProtoContext::UnmanagedScope u(ctx);
        teardownServer(s);
    }
    return PROTO_NONE;
}

const proto::ProtoObject* serverListenImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ServerState* s = getServerState(ctx, self);
    if (!s || s->listening.load()) return self ? self : PROTO_NONE;

    long long port = 0;
    std::string host = "0.0.0.0";
    int cbArgIdx = -1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) {
            port = a0->asLong(ctx);
            if (args->getSize(ctx) > 1) {
                const proto::ProtoObject* a1 = args->getAt(ctx, 1);
                if (a1 && a1->isString(ctx)) {
                    a1->asString(ctx)->toUTF8String(ctx, host);
                    if (args->getSize(ctx) > 2) cbArgIdx = 2;
                } else {
                    cbArgIdx = 1;
                }
            }
        } else if (a0) {
            optInt(ctx, a0, "port", port);
            optString(ctx, a0, "host", host);
            if (args->getSize(ctx) > 1) cbArgIdx = 1;
        }
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return self;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(fd);
        return self;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return self;
    }
    if (::listen(fd, 128) < 0) {
        ::close(fd);
        return self;
    }
    // If port=0 was requested, recover the OS-assigned port.
    if (port == 0) {
        sockaddr_in la{};
        socklen_t llen = sizeof(la);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&la), &llen) == 0) {
            s->port = ntohs(la.sin_port);
        }
    } else {
        s->port = static_cast<int>(port);
    }
    s->host = host;
    s->socketFd.store(fd);
    s->listening.store(true);
    g_active.fetch_add(1);

    JSContextWrapper* wrapper = s->mainWrapper;
    proto::ProtoRootSet::Handle pin = s->serverPin;

    s->acceptThread = std::thread([wrapper, pin, s]() {
        while (s->listening.load() && !s->closed.load()) {
            sockaddr_in client{};
            socklen_t clen = sizeof(client);
            int cfd = ::accept(s->socketFd.load(),
                                 reinterpret_cast<sockaddr*>(&client),
                                 &clen);
            if (cfd < 0) {
                if (!s->listening.load()) break;
                continue;
            }
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
            std::string remote = ip;
            int rport = ntohs(client.sin_port);
            EventLoop::getInstance().enqueueCallback(
                [wrapper, pin, cfd, remote, rport]() {
                    if (!wrapper) return;
                    JSContextWrapper::CurrentScope ws(wrapper);
                    proto::ProtoContext* mctx = wrapper->getProtoContext();
                    if (!mctx) {
                        ::close(cfd);
                        return;
                    }
                    proto::ProtoRootSet* rs = wrapper->getRootSet();
                    const proto::ProtoObject* server =
                        rs ? rs->resolve(pin) : nullptr;
                    if (!server || server == PROTO_NONE) {
                        ::close(cfd);
                        return;
                    }
                    const proto::ProtoObject* sock = makeSocketInstance(
                        wrapper, mctx, wrapper->getNativeGlobal());
                    if (!sock || sock == PROTO_NONE) {
                        ::close(cfd);
                        return;
                    }
                    SocketState* sst = getSocketState(mctx, sock);
                    if (!sst) {
                        ::close(cfd);
                        return;
                    }
                    sst->socketFd.store(cfd);
                    sst->connected.store(true);
                    sst->remoteAddress = remote;
                    sst->remotePort = rport;
                    sockaddr_in la{};
                    socklen_t llen = sizeof(la);
                    if (::getsockname(cfd, reinterpret_cast<sockaddr*>(&la),
                                       &llen) == 0) {
                        char lip[INET_ADDRSTRLEN] = {};
                        inet_ntop(AF_INET, &la.sin_addr, lip, sizeof(lip));
                        sst->localAddress = lip;
                        sst->localPort = ntohs(la.sin_port);
                    }
                    g_active.fetch_add(1);
                    startSocketReadLoop(wrapper, sst->socketPin, sst);

                    // Emit 'connection' with the new socket on the server.
                    const proto::ProtoList* a = mctx->newList()
                        ->appendLast(mctx, mctx->fromUTF8String("connection"))
                        ->appendLast(mctx, sock);
                    invokeEEMethodFromAsync(wrapper, server, "emit", a);
                });
        }
    });

    // Optional 'listening' callback (server.listen(port, [host], cb)).
    if (cbArgIdx >= 0 && args
         && static_cast<int>(args->getSize(ctx)) > cbArgIdx) {
        const proto::ProtoObject* cb =
            args->getAt(ctx, cbArgIdx);
        if (cb && cb != PROTO_NONE) {
            callJSFunction(ctx, cb, self, ctx->newList());
        }
    }
    return self;
}

const proto::ProtoObject* getServerProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"on",      serverOnImpl},
        {"emit",    serverEmitImpl},
        {"listen",  serverListenImpl},
        {"close",   serverCloseImpl},
        {"address", serverAddressImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 5);
    return proto;
}

// ---- Module-level: createServer / createConnection --------------------

const proto::ProtoObject* createServerImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;

    const proto::ProtoObject* serverProto = getServerProto(ctx);
    const proto::ProtoObject* server = serverProto
        ? serverProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoObject* ee =
        makeEventEmitterInstance(ctx, wrapper->getNativeGlobal());
    server->setAttribute(ctx, keyEvents(ctx), ee);

    auto* state = new ServerState{};
    state->mainWrapper = wrapper;
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    if (rs) state->serverPin = rs->add(server);

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeServerState);
    if (!extPtr) {
        if (rs && state->serverPin != proto::ProtoRootSet::kNullHandle)
            rs->remove(state->serverPin);
        delete state;
        return PROTO_NONE;
    }
    server->setAttribute(ctx, keyServerState(ctx), extPtr);

    // Optional connection listener: pre-register on('connection', cb).
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE) {
            const proto::ProtoList* registerArgs = ctx->newList()
                ->appendLast(ctx, ctx->fromUTF8String("connection"))
                ->appendLast(ctx, a0);
            invokeEEMethod(ctx, server, "on", registerArgs);
        }
    }
    return server;
}

const proto::ProtoObject* createConnectionImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    if (!ctx) return PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    const proto::ProtoObject* sock =
        makeSocketInstance(wrapper, ctx, wrapper->getNativeGlobal());
    if (!sock || sock == PROTO_NONE) return PROTO_NONE;
    if (args && args->getSize(ctx) > 0) {
        socketConnectImpl(ctx, sock, pl, args, kw);
    }
    return sock;
}

}  // namespace

// ---- Public ------------------------------------------------------------

int NetModule::getActiveCount() {
    return g_active.load();
}

const proto::ProtoObject* NetModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"createServer",     createServerImpl},
        {"createConnection", createConnectionImpl},
        {"connect",          createConnectionImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 3);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "net", mod);
}

} // namespace protojs
