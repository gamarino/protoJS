#include "../../ProtoCoreTypes.h"
#include "../../runtime/JSStringBytes.h"
#include "../../runtime/PinnedBuiltin.h"
#include "NetModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../PromisePrototype.h"
#include "../../JSContext.h"
#include "../../EventLoop.h"
#include "../../GcOrphanQueue.h"
#include "../../ThreadProtoContext.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../runtime/ProtoBytecodeModule.h"
#include "../buffer/BufferModule.h"
#include "../events/EventsModule.h"
#include "../../platform/Sockets.h"
#if !defined(_WIN32)
#include <fcntl.h>
#endif
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstring>

namespace protojs {

namespace {

// ---- Attribute keys (re-intern per call: SymbolTable is per-space) ----

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

bool argString(proto::ProtoContext* ctx, const proto::ProtoArguments* args,
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
// A Socket and a Server are EventEmitters, as in Node: Socket.prototype and
// the server prototype inherit from events.EventEmitter.prototype, so the
// listeners live on the socket itself and run with the socket as `this`, and
// every EventEmitter method (once, off, listeners, ...) is there.  Until
// 2026-10 each one carried a separate EventEmitter in an `__events__`
// attribute and forwarded on/emit to it, so a listener's `this` was that
// hidden object.

// events.EventEmitter.prototype of the running realm, or nullptr.
const proto::ProtoObject* eventEmitterPrototype(proto::ProtoContext* ctx) {
    JSContextWrapper* w = JSContextWrapper::current();
    const proto::ProtoObject* nativeGlobal = w ? w->getNativeGlobal() : nullptr;
    if (!ctx || !nativeGlobal) return nullptr;
    const proto::ProtoString* eventsKey =
        ctx->fromUTF8String("events")->asString(ctx);
    const proto::ProtoObject* eventsMod =
        eventsKey ? nativeGlobal->getAttribute(ctx, eventsKey, false) : nullptr;
    if (!eventsMod || eventsMod == PROTO_NONE) return nullptr;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* eeProto =
        protoKey ? eventsMod->getAttribute(ctx, protoKey, false) : nullptr;
    return (eeProto && eeProto != PROTO_NONE) ? eeProto : nullptr;
}

// A prototype holding `entries` whose parent is EventEmitter.prototype.
const proto::ProtoObject* buildEmitterPrototype(proto::ProtoContext* ctx,
                                                const NativeEntry* entries) {
    const proto::ProtoObject* eeProto = eventEmitterPrototype(ctx);
    const proto::ProtoObject* p = eeProto ? eeProto->newChild(ctx, /*mutable=*/true)
                                          : ctx->newObject(/*mutable=*/true);
    for (const NativeEntry* e = entries; p && e->name; ++e)
        p = ProtoNativeModule::addMethod(ctx, p, e->name, e->fn);
    return p;
}

// Calls the EventEmitter method `methodName` of `self` with `self` as `this`.
const proto::ProtoObject* invokeEEMethod(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoArguments* args) {
    if (!self || !ctx) return PROTO_NONE;
    const proto::ProtoString* mk =
        ctx->fromUTF8String(methodName)->asString(ctx);
    if (!mk) return PROTO_NONE;
    const proto::ProtoObject* fn = self->getAttribute(ctx, mk, true);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoArguments noArgs(nullptr, 0);
    return callJSFunction(ctx, fn, self, args ? args : &noArgs);
}

// The same from an event-loop callback (a socket's data, connect or close).
void invokeEEMethodFromAsync(
        JSContextWrapper* wrapper,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoList* args) {
    if (!wrapper || !self) return;
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) return;
    const proto::ProtoString* mk =
        ctx->fromUTF8String(methodName)->asString(ctx);
    if (!mk) return;
    const proto::ProtoObject* fn = self->getAttribute(ctx, mk, true);
    if (!fn || fn == PROTO_NONE) return;
    const ProtoBytecodeModule* mod =
        static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    callJSFunctionFromAsync(ctx, fn, self,
                             args ? args : ctx->newList(),
                             mod, wrapper->getNativeGlobalRootPtr());
}

// ---- ServerState / SocketState -----------------------------------------
//
// Carried on the Server/Socket instances via ProtoExternalPointer so the
// std::thread + atomic flags + ProtoRootSet pin are all freed in a
// single C++ destructor when the JS instance is collected.

struct SocketState;

struct ServerState : GcOrphanQueue::Orphan {
    JSContextWrapper* mainWrapper{nullptr};
    proto::ProtoRootSet::Handle serverPin{
        proto::ProtoRootSet::kNullHandle};
    std::atomic<int> socketFd{-1};
    int port{0};
    std::string host;
    std::atomic<bool> listening{false};
    std::atomic<bool> closed{false};
    std::thread acceptThread;
    platform::AcceptWaker acceptWaker;   // stops the accept loop (requestServerTeardown)

    void releaseOnMutator() override;
};

struct SocketState : GcOrphanQueue::Orphan {
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

    void releaseOnMutator() override;
};

// Teardown is split in two halves, and the split is the fix.
//
// The REQUEST half flips the flags and closes the listening/connected descriptor.
// Closing the descriptor is what makes the accept/recv loop return, so it is the
// part that has to happen promptly; it blocks on nothing and calls no protoCore
// API, which is exactly what protoCore's finalizer contract permits a finalizer to
// do (docs/GarbageCollector.md S7: "free an external buffer, run an external
// pointer's callback").
//
// The AWAIT half joins the loop thread. It blocks, and a finalizer must not block:
// it runs on the single GC thread inside the sweep, so a wait there stalls
// collection for the whole space -- and joining an accept loop or a thread sitting
// in ::recv on a silent peer is an unbounded wait. Section 7 is explicit that a
// finalizer is not even proof the OS thread has stopped, since sweep runs with the
// world going. So the await half runs only where a mutator thread can bracket it.

// Returns true exactly once per server: `listening` is the once-only token, so
// whichever of an explicit close() and the finalizer gets there first owns the
// g_active decrement and the other one does nothing.
//
// For a server the request half clears `listening` and wakes the accept loop
// (platform::AcceptWaker); the listening descriptor is closed by the await half
// after the join, never under a thread inside accept()
// (platform::acceptUntilStopped: on macOS that close stalls).
bool requestServerTeardown(ServerState* s) {
    if (!s) return false;
    bool wasListening = s->listening.exchange(false);
    s->closed.store(true);
    s->acceptWaker.wake();
    return wasListening;
}

// The blocking half.  The guard lives HERE rather than at each caller, so a new
// caller cannot forget it: a registered thread that blocks inside protoCore's running
// set makes the stop-the-world quorum unreachable.  `ctx` may be null, in which case
// the calling thread's own registered context is used.
void awaitServerTeardown(ServerState* s, proto::ProtoContext* ctx) {
    if (!s) return;
    ThreadUnmanagedScope parked(ctx);   // an UnmanagedScope on ctx, or on this thread's
    if (s->acceptThread.joinable()) s->acceptThread.join();
    const int fd = s->socketFd.exchange(-1);
    if (fd >= 0) platform::closeSocket(fd);
}

// Returns true exactly once per socket, for the same reason.
bool requestSocketTeardown(SocketState* s) {
    if (!s) return false;
    bool wasReading = s->connected.exchange(false);
    s->destroyed.store(true);
    int fd = s->socketFd.exchange(-1);
    if (fd >= 0) { ::shutdown(fd, SHUT_RDWR); platform::closeSocket(fd); }
    return wasReading;
}

// The blocking half -- see awaitServerTeardown for why the guard is here.
void awaitSocketTeardown(SocketState* s, proto::ProtoContext* ctx) {
    if (!s) return;
    ThreadUnmanagedScope parked(ctx);   // an UnmanagedScope on ctx, or on this thread's
    if (s->readThread.joinable()) s->readThread.join();
}

void ServerState::releaseOnMutator() {
    // awaitServerTeardown brackets the join itself; no context is in hand here, so it
    // uses the calling thread's own.
    awaitServerTeardown(this, nullptr);
    // Only legal here, not in the finalizer: ProtoRootSet::remove takes the very
    // mutex the collector holds during root collection, so it both blocks and
    // publishes to a shared structure.
    if (mainWrapper && serverPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = mainWrapper->getRootSet();
        if (rs) rs->remove(serverPin);
        serverPin = proto::ProtoRootSet::kNullHandle;
    }
    delete this;
}

void SocketState::releaseOnMutator() {
    awaitSocketTeardown(this, nullptr);
    if (mainWrapper && socketPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = mainWrapper->getRootSet();
        if (rs) rs->remove(socketPin);
        socketPin = proto::ProtoRootSet::kNullHandle;
    }
    delete this;
}

// The finalizers. Each does only the non-blocking request half and then records the
// orphan; GcOrphanQueue::post allocates nothing, takes no lock, calls no protoCore
// API and never waits.
void freeServerState(void* p) {
    auto* s = static_cast<ServerState*>(p);
    if (!s) return;
    if (requestServerTeardown(s)) g_active.fetch_sub(1);
    GcOrphanQueue::post(s);
}

void freeSocketState(void* p) {
    auto* s = static_cast<SocketState*>(p);
    if (!s) return;
    if (requestSocketTeardown(s)) g_active.fetch_sub(1);
    GcOrphanQueue::post(s);
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
    proto::proto_ulong n = bb->getSize(ctx);
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

const proto::ProtoObject* socketWriteImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
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
        sent = ::send(fd, reinterpret_cast<const char*>(bytes.data()),
                      platform::socketLength(bytes.size()), 0);
    }
    return (sent > 0) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* socketEndImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kw) {
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
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
    SocketState* s = getSocketState(ctx, self);
    if (s) {
        // The owner relinquish point -- see serverCloseImpl.
        const bool wasReading = requestSocketTeardown(s);
        awaitSocketTeardown(s, ctx);
        if (wasReading) g_active.fetch_sub(1);
        if (s->mainWrapper && s->socketPin != proto::ProtoRootSet::kNullHandle) {
            proto::ProtoRootSet* rs = s->mainWrapper->getRootSet();
            if (rs) rs->remove(s->socketPin);
            s->socketPin = proto::ProtoRootSet::kNullHandle;
        }
    }
    return PROTO_NONE;
}

const proto::ProtoObject* socketAddressImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
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
        protojs::jsStringFromUTF8(ctx, s->localAddress));
    return obj;
}

// `socket.connect(port, host?, callback?)` or
// `socket.connect({ port, host }, callback?)`.
const proto::ProtoObject* socketConnectImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    SocketState* s = getSocketState(ctx, self);
    if (!s || s->connected.load()) return self ? self : PROTO_NONE;

    // The connect listener, the last argument when it is a function, is added
    // as a 'connect' listener with once(), as Node does.  Until 2026-10 it
    // was ignored and never ran.
    const unsigned long argc = args ? args->getSize(ctx) : 0;
    if (argc > 1) {
        const proto::ProtoObject* listener = args->getAt(ctx, static_cast<int>(argc - 1));
        if (jsIsCallable(ctx, listener)) {
            const proto::ProtoList* onceArgs = ctx->newList()
                ->appendLast(ctx, ctx->fromUTF8String("connect"))
                ->appendLast(ctx, listener);
            invokeEEMethod(ctx, self, "once", argsView(proto::ProtoListArguments(ctx, onceArgs)));
        }
    }

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

    int fd = platform::openSocket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return self;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        // Fallback for "localhost".
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        platform::closeSocket(fd);
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
    // Made on first use; pinned for the wrapper's life (runtime/PinnedBuiltin.h).
    static PinnedBuiltin protoCache;
    if (const proto::ProtoObject* cached = protoCache.get(ctx)) return cached;
    static const NativeEntry entries[] = {
        {"connect",  socketConnectImpl},
        {"write",    socketWriteImpl},
        {"end",      socketEndImpl},
        {"destroy",  socketDestroyImpl},
        {"address",  socketAddressImpl},
        NATIVE_MODULE_END
    };
    return protoCache.keep(ctx, buildEmitterPrototype(ctx, entries));
}

const proto::ProtoObject* makeSocketInstance(
        JSContextWrapper* wrapper,
        proto::ProtoContext* ctx,
        const proto::ProtoObject* /*nativeGlobal*/) {
    const proto::ProtoObject* proto = getSocketProto(ctx);
    const proto::ProtoObject* sock = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

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

const proto::ProtoObject* serverAddressImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
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
        protojs::jsStringFromUTF8(ctx, s->host));
    return obj;
}

const proto::ProtoObject* serverCloseImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
    ServerState* s = getServerState(ctx, self);
    if (s) {
        // The owner relinquish point. protoCore's finalizer contract
        // (docs/GarbageCollector.md S7) says a resource whose release blocks,
        // publishes, or depends on another thread having finished belongs "at the
        // point where the owner itself gives it up" -- this is it, so the join and
        // the pin release both happen here rather than on the collector.
        const bool wasListening = requestServerTeardown(s);
        awaitServerTeardown(s, ctx);
        if (wasListening) g_active.fetch_sub(1);
        if (s->mainWrapper && s->serverPin != proto::ProtoRootSet::kNullHandle) {
            proto::ProtoRootSet* rs = s->mainWrapper->getRootSet();
            if (rs) rs->remove(s->serverPin);
            s->serverPin = proto::ProtoRootSet::kNullHandle;
        }
    }
    return PROTO_NONE;
}

const proto::ProtoObject* serverListenImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
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

    int fd = platform::openSocket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return self;
#if !defined(_WIN32)
    // On Windows SO_REUSEADDR would let a second server take a port in use;
    // reusing a port in TIME_WAIT, what it buys here, is Windows' default.
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        platform::closeSocket(fd);
        return self;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        platform::closeSocket(fd);
        return self;
    }
    if (::listen(fd, 128) < 0 || !platform::setSocketBlocking(fd, false)) {
        platform::closeSocket(fd);
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
    s->acceptWaker.reset();
    s->socketFd.store(fd);
    s->listening.store(true);
    g_active.fetch_add(1);

    JSContextWrapper* wrapper = s->mainWrapper;
    proto::ProtoRootSet::Handle pin = s->serverPin;

    s->acceptThread = std::thread([wrapper, pin, s, fd]() {
        while (s->listening.load() && !s->closed.load()) {
            sockaddr_in client{};
            socklen_t clen = sizeof(client);
            const int cfd = platform::acceptUntilStopped(fd, s->listening, s->acceptWaker,
                                 reinterpret_cast<sockaddr*>(&client),
                                 &clen);
            if (cfd < 0) break;   // stopped
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
                        platform::closeSocket(cfd);
                        return;
                    }
                    proto::ProtoRootSet* rs = wrapper->getRootSet();
                    const proto::ProtoObject* server =
                        rs ? rs->resolve(pin) : nullptr;
                    if (!server || server == PROTO_NONE) {
                        platform::closeSocket(cfd);
                        return;
                    }
                    const proto::ProtoObject* sock = makeSocketInstance(
                        wrapper, mctx, wrapper->getNativeGlobal());
                    if (!sock || sock == PROTO_NONE) {
                        platform::closeSocket(cfd);
                        return;
                    }
                    SocketState* sst = getSocketState(mctx, sock);
                    if (!sst) {
                        platform::closeSocket(cfd);
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

    // server.listen(port, [host], cb) adds cb as a once('listening') listener,
    // and 'listening' is emitted on a later event-loop turn, as in Node (until
    // 2026-10 cb was called synchronously, inside listen, and no 'listening'
    // event was emitted).
    if (cbArgIdx >= 0 && args
         && static_cast<int>(args->getSize(ctx)) > cbArgIdx) {
        const proto::ProtoObject* cb =
            args->getAt(ctx, cbArgIdx);
        if (cb && cb != PROTO_NONE && cb != getUndefinedSentinel()) {
            const proto::ProtoList* onceArgs = ctx->newList()
                ->appendLast(ctx, ctx->fromUTF8String("listening"))
                ->appendLast(ctx, cb);
            invokeEEMethod(ctx, self, "once", argsView(proto::ProtoListArguments(ctx, onceArgs)));
        }
    }
    EventLoop::getInstance().enqueueCallback([wrapper, pin]() {
        if (!wrapper) return;
        JSContextWrapper::CurrentScope ws(wrapper);
        proto::ProtoContext* mctx = wrapper->getProtoContext();
        proto::ProtoRootSet* rs = wrapper->getRootSet();
        const proto::ProtoObject* server = (mctx && rs) ? rs->resolve(pin) : nullptr;
        if (!server || server == PROTO_NONE) return;
        invokeEEMethodFromAsync(wrapper, server, "emit",
            mctx->newList()->appendLast(mctx, mctx->fromUTF8String("listening")));
    });
    return self;
}

const proto::ProtoObject* getServerProto(proto::ProtoContext* ctx) {
    // Made on first use; pinned for the wrapper's life (runtime/PinnedBuiltin.h).
    static PinnedBuiltin protoCache;
    if (const proto::ProtoObject* cached = protoCache.get(ctx)) return cached;
    static const NativeEntry entries[] = {
        {"listen",  serverListenImpl},
        {"close",   serverCloseImpl},
        {"address", serverAddressImpl},
        NATIVE_MODULE_END
    };
    return protoCache.keep(ctx, buildEmitterPrototype(ctx, entries));
}

// ---- Module-level: createServer / createConnection --------------------

const proto::ProtoObject* createServerImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx) return PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;

    const proto::ProtoObject* serverProto = getServerProto(ctx);
    const proto::ProtoObject* server = serverProto
        ? serverProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

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
            invokeEEMethod(ctx, server, "on", argsView(proto::ProtoListArguments(ctx, registerArgs)));
        }
    }
    return server;
}

const proto::ProtoObject* createConnectionImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kw) {
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
