#include "HTTPModule.h"
#include "../../ProtoNativeModule.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../JSContext.h"
#include "../../EventLoop.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../runtime/ProtoBytecodeModule.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>
#include <cctype>
#include <thread>
#include <sstream>
#include <string>
#include <vector>
#include <map>

namespace protojs {

namespace {

// ---- Attribute keys ----------------------------------------------------
//
// Each instance carries its mutable state under these symbol keys.  They
// are interned strongly via createSymbol so the keys are perpetual and
// pointer-stable across GC cycles.

const proto::ProtoString* keyFD(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__fd__");
}
const proto::ProtoString* keyPort(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__port__");
}
const proto::ProtoString* keyServerState(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__server_state__");
}
const proto::ProtoString* keyMethod(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__method__");
}
const proto::ProtoString* keyURL(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__url__");
}
const proto::ProtoString* keyHeaders(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__headers__");
}
const proto::ProtoString* keyBody(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__body__");
}
const proto::ProtoString* keyStatus(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__status__");
}
const proto::ProtoString* keyClientFD(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__client_fd__");
}
const proto::ProtoString* keyHeadersSent(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__headers_sent__");
}

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

int getIntAttr(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                const proto::ProtoString* k, int defv = -1) {
    if (!self || !k) return defv;
    const proto::ProtoObject* v = self->getAttribute(ctx, k, false);
    if (!v || !v->isInteger(ctx)) return defv;
    return static_cast<int>(v->asLong(ctx));
}

// ---- ServerState (carried via ExternalPointer) -------------------------
//
// Holds the cross-thread bits that the GC cannot rebuild: the std::thread
// running the accept loop, the atomic stop flag, and the pin handle for
// the request listener.  Lifetime: freed by `freeServerState` when the
// owning Server instance is collected.

struct ServerState {
    std::atomic<bool> listening{false};
    int socketFd{-1};
    std::thread thread;
    JSContextWrapper* wrapper{nullptr};
    proto::ProtoRootSet::Handle listenerPin{proto::ProtoRootSet::kNullHandle};
};

// Counts servers whose accept loop is active.  Decremented from
// serverClose / freeServerState.  main.cpp's drain loop keeps spinning
// while this is non-zero so the process doesn't exit before the user
// has had a chance to call .close().
std::atomic<int> g_activeServers{0};

// Counts http.request invocations whose response has not yet been
// delivered to the user callback.  main.cpp's drain loop also keys off
// this so the process doesn't exit while a client request is in flight.
std::atomic<int> g_activeClients{0};

void freeServerState(void* p) {
    auto* s = static_cast<ServerState*>(p);
    if (!s) return;
    bool wasListening = s->listening.exchange(false);
    if (s->socketFd >= 0) {
        ::shutdown(s->socketFd, SHUT_RDWR);
        ::close(s->socketFd);
        s->socketFd = -1;
    }
    if (s->thread.joinable()) {
        s->thread.join();
    }
    if (wasListening) g_activeServers.fetch_sub(1);
    if (s->wrapper && s->listenerPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = s->wrapper->getRootSet();
        if (rs) rs->remove(s->listenerPin);
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

// ---- IncomingMessage / ServerResponse / ClientRequest builders ---------

const proto::ProtoObject* getIncomingProto(proto::ProtoContext* ctx);
const proto::ProtoObject* getResponseProto(proto::ProtoContext* ctx);
const proto::ProtoObject* getRequestProto(proto::ProtoContext* ctx);
const proto::ProtoObject* getServerProto(proto::ProtoContext* ctx);

// Build a fresh `headers` object from a flat key→value map.  Headers are
// kept on the Response side too so that responseEnd can serialise them.
const proto::ProtoObject* makeHeadersObject(
        proto::ProtoContext* ctx,
        const std::map<std::string, std::string>& headers) {
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    if (!obj) return PROTO_NONE;
    for (const auto& [k, v] : headers) {
        const proto::ProtoString* sk =
            ctx->fromUTF8String(k.c_str())->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk,
                                   ctx->fromUTF8String(v.c_str()));
    }
    return obj;
}

// ---- IncomingMessage methods -------------------------------------------

const proto::ProtoObject* incomingGetHeader(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string name;
    if (!argString(ctx, args, 0, name) || !self) return PROTO_NONE;
    const proto::ProtoObject* hdrs =
        self->getAttribute(ctx, keyHeaders(ctx), false);
    if (!hdrs || hdrs == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoString* nk =
        ctx->fromUTF8String(name.c_str())->asString(ctx);
    if (!nk) return PROTO_NONE;
    const proto::ProtoObject* v = hdrs->getAttribute(ctx, nk, false);
    return (v && v != PROTO_NONE) ? v : PROTO_NONE;
}

const proto::ProtoObject* getIncomingProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"getHeader", incomingGetHeader},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 1);
    return proto;
}

// ---- ServerResponse methods --------------------------------------------

const proto::ProtoObject* responseWriteHead(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!self || !ctx) return self ? self : PROTO_NONE;
    const proto::ProtoObject* sentVal =
        self->getAttribute(ctx, keyHeadersSent(ctx), false);
    bool sent = (sentVal == PROTO_TRUE);
    if (sent) return self;

    long long status = 200;
    argInt(ctx, args, 0, status);
    self->setAttribute(ctx, keyStatus(ctx), ctx->fromInteger(status));

    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* hdrsArg = args->getAt(ctx, 1);
        if (hdrsArg && hdrsArg != PROTO_NONE) {
            // Replace any existing __headers__ object with the caller's
            // map.  The original implementation merged into a
            // std::map, but storing the raw object works the same for
            // attribute lookup at end() time.
            self->setAttribute(ctx, keyHeaders(ctx), hdrsArg);
        }
    }
    return self;
}

const proto::ProtoObject* responseWrite(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!self || !ctx || !args || args->getSize(ctx) == 0) return PROTO_FALSE;
    std::string chunk;
    if (!argString(ctx, args, 0, chunk)) return PROTO_FALSE;
    const proto::ProtoObject* prev =
        self->getAttribute(ctx, keyBody(ctx), false);
    std::string body;
    if (prev && prev->isString(ctx)) {
        prev->asString(ctx)->toUTF8String(ctx, body);
    }
    body += chunk;
    self->setAttribute(ctx, keyBody(ctx),
                        ctx->fromUTF8String(body.c_str()));
    return PROTO_TRUE;
}

void appendHeadersFromObject(proto::ProtoContext* ctx,
                              const proto::ProtoObject* hdrsObj,
                              std::ostringstream& out,
                              bool& haveContentType) {
    if (!hdrsObj || hdrsObj == PROTO_NONE) return;
    const proto::ProtoSparseList* own = hdrsObj->getOwnAttributes(ctx);
    if (!own) return;
    const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long rawKey = it->nextKey(ctx);
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoString* k =
            reinterpret_cast<const proto::ProtoString*>(rawKey);
        if (!k || !v) continue;
        std::string keyStr;
        k->toUTF8String(ctx, keyStr);
        // Skip our internal markers (none expected here, but defensive).
        if (keyStr.size() >= 2 && keyStr[0] == '_' && keyStr[1] == '_') continue;
        std::string valStr;
        if (v->isString(ctx)) {
            v->asString(ctx)->toUTF8String(ctx, valStr);
        } else if (v->isInteger(ctx)) {
            valStr = std::to_string(v->asLong(ctx));
        } else {
            continue;
        }
        if (keyStr == "Content-Type" || keyStr == "content-type")
            haveContentType = true;
        out << keyStr << ": " << valStr << "\r\n";
    }
}

const proto::ProtoObject* responseEnd(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    if (!self || !ctx) return self ? self : PROTO_NONE;
    if (args && args->getSize(ctx) > 0) {
        responseWrite(ctx, self, pl, args, kw);
    }
    int clientFd = getIntAttr(ctx, self, keyClientFD(ctx));
    if (clientFd < 0) return self;

    long long status = 200;
    const proto::ProtoObject* sv =
        self->getAttribute(ctx, keyStatus(ctx), false);
    if (sv && sv->isInteger(ctx)) status = sv->asLong(ctx);

    std::string body;
    const proto::ProtoObject* bv =
        self->getAttribute(ctx, keyBody(ctx), false);
    if (bv && bv->isString(ctx)) {
        bv->asString(ctx)->toUTF8String(ctx, body);
    }

    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " OK\r\n";
    bool haveContentType = false;
    appendHeadersFromObject(ctx,
        self->getAttribute(ctx, keyHeaders(ctx), false),
        resp, haveContentType);
    if (!haveContentType) resp << "Content-Type: text/plain\r\n";
    resp << "Content-Length: " << body.size() << "\r\n\r\n" << body;

    std::string out = resp.str();
    ssize_t wn = ::write(clientFd, out.data(), out.size());
    (void)wn;
    self->setAttribute(ctx, keyHeadersSent(ctx), PROTO_TRUE);
    ::close(clientFd);
    self->setAttribute(ctx, keyClientFD(ctx), ctx->fromInteger(-1));
    return self;
}

const proto::ProtoObject* getResponseProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"writeHead", responseWriteHead},
        {"write",     responseWrite},
        {"end",       responseEnd},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 3);
    return proto;
}

// ---- HTTP wire parsing (used by accept loop) ---------------------------

struct ParsedRequest {
    std::string method;
    std::string url;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

ParsedRequest parseRequest(const std::string& raw) {
    ParsedRequest p;
    std::istringstream iss(raw);
    iss >> p.method >> p.url >> p.version;
    std::string line;
    // discard rest of request line
    std::getline(iss, line);
    while (std::getline(iss, line) && line != "\r" && !line.empty()) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\r')) {
            value.erase(0, 1);
        }
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
        }
        p.headers[key] = value;
    }
    return p;
}

// Build the IncomingMessage and ServerResponse for a single accepted
// connection — runs on the JS thread (inside an EventLoop callback).
void dispatchRequest(JSContextWrapper* wrapper,
                      proto::ProtoRootSet::Handle pin,
                      ParsedRequest req,
                      int clientFd) {
    if (!wrapper) {
        if (clientFd >= 0) ::close(clientFd);
        return;
    }
    JSContextWrapper::CurrentScope ws(wrapper);
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) {
        if (clientFd >= 0) ::close(clientFd);
        return;
    }
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    const proto::ProtoObject* listener = rs ? rs->resolve(pin) : nullptr;
    // Note: do NOT remove the pin — listener is reused for every
    // request the server handles.  The pin is freed by freeServerState
    // when the server itself is collected.

    // Build IncomingMessage
    const proto::ProtoObject* incomingProto = getIncomingProto(ctx);
    const proto::ProtoObject* incoming = incomingProto
        ? incomingProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    incoming->setAttribute(ctx, keyMethod(ctx),
        ctx->fromUTF8String(req.method.c_str()));
    incoming->setAttribute(ctx, keyURL(ctx),
        ctx->fromUTF8String(req.url.c_str()));
    incoming->setAttribute(ctx, keyHeaders(ctx),
        makeHeadersObject(ctx, req.headers));
    incoming->setAttribute(ctx, keyBody(ctx),
        ctx->fromUTF8String(req.body.c_str()));
    // Public fields the original exposed.
    incoming->setAttribute(ctx,
        ctx->fromUTF8String("method")->asString(ctx),
        ctx->fromUTF8String(req.method.c_str()));
    incoming->setAttribute(ctx,
        ctx->fromUTF8String("url")->asString(ctx),
        ctx->fromUTF8String(req.url.c_str()));

    // Build ServerResponse
    const proto::ProtoObject* responseProto = getResponseProto(ctx);
    const proto::ProtoObject* response = responseProto
        ? responseProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    response->setAttribute(ctx, keyStatus(ctx), ctx->fromInteger(200));
    response->setAttribute(ctx, keyClientFD(ctx),
        ctx->fromInteger(clientFd));
    response->setAttribute(ctx, keyHeadersSent(ctx), PROTO_FALSE);
    response->setAttribute(ctx, keyHeaders(ctx),
        ctx->newObject(/*mutable=*/true));
    response->setAttribute(ctx, keyBody(ctx),
        ctx->fromUTF8String(""));

    if (!listener || listener == PROTO_NONE) {
        // No request listener — close the connection.
        if (clientFd >= 0) ::close(clientFd);
        return;
    }
    const proto::ProtoList* cbArgs = ctx->newList()
        ->appendLast(ctx, incoming)
        ->appendLast(ctx, response);
    const ProtoBytecodeModule* mod =
        static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    callJSFunctionFromAsync(ctx, listener, PROTO_NONE, cbArgs, mod,
                             wrapper->getNativeGlobalRootPtr());
    // The user is expected to call response.end() — the response
    // object will close clientFd at that point.
}

// ---- server.listen / server.close --------------------------------------

const proto::ProtoObject* serverListen(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!self || !ctx) return self ? self : PROTO_NONE;
    long long port = 0;
    if (!argInt(ctx, args, 0, port)) return PROTO_NONE;

    ServerState* state = getServerState(ctx, self);
    if (!state) return PROTO_NONE;
    if (state->listening.load()) return self;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return PROTO_NONE;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return PROTO_NONE;
    }
    if (::listen(fd, 16) < 0) {
        ::close(fd);
        return PROTO_NONE;
    }

    state->socketFd = fd;
    self->setAttribute(ctx, keyFD(ctx), ctx->fromInteger(fd));
    self->setAttribute(ctx, keyPort(ctx), ctx->fromInteger(port));
    state->listening.store(true);
    g_activeServers.fetch_add(1);

    // Capture into the worker thread.  The wrapper pointer is stable
    // for the remainder of the process; the listener is held alive by
    // ProtoRootSet via state->listenerPin (set by createServer).
    JSContextWrapper* wrapper = state->wrapper;
    proto::ProtoRootSet::Handle pin = state->listenerPin;
    state->thread = std::thread([state, wrapper, pin]() {
        while (state->listening.load()) {
            sockaddr_in client{};
            socklen_t cl = sizeof(client);
            int cfd = ::accept(state->socketFd,
                                reinterpret_cast<sockaddr*>(&client), &cl);
            if (cfd < 0) {
                if (!state->listening.load()) break;
                continue;
            }
            char buf[4096];
            ssize_t n = ::read(cfd, buf, sizeof(buf) - 1);
            if (n <= 0) {
                ::close(cfd);
                continue;
            }
            buf[n] = '\0';
            ParsedRequest req = parseRequest(std::string(buf, n));
            EventLoop::getInstance().enqueueCallback(
                [wrapper, pin, req = std::move(req), cfd]() mutable {
                    dispatchRequest(wrapper, pin, std::move(req), cfd);
                });
        }
    });

    // Optional success callback (server.listen(port, callback)).
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* cb = args->getAt(ctx, 1);
        if (cb && cb != PROTO_NONE) {
            callJSFunction(ctx, cb, self, ctx->newList());
        }
    }
    return self;
}

const proto::ProtoObject* serverClose(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    ServerState* state = getServerState(ctx, self);
    if (!state) return PROTO_NONE;
    bool wasListening = state->listening.exchange(false);
    if (state->socketFd >= 0) {
        ::shutdown(state->socketFd, SHUT_RDWR);
        ::close(state->socketFd);
        state->socketFd = -1;
    }
    if (state->thread.joinable()) state->thread.join();
    if (wasListening) g_activeServers.fetch_sub(1);
    if (self) self->setAttribute(ctx, keyFD(ctx), ctx->fromInteger(-1));
    return PROTO_NONE;
}

const proto::ProtoObject* getServerProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"listen", serverListen},
        {"close",  serverClose},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return proto;
}

// ---- Parsed HTTP response -----------------------------------------------

struct ParsedResponse {
    int                                statusCode = 200;
    std::map<std::string, std::string> headers;
    std::string                        body;
};

ParsedResponse parseHttpResponse(const std::string& raw) {
    ParsedResponse r;
    std::istringstream iss(raw);
    std::string version, statusMsg;
    iss >> version >> r.statusCode;
    std::string line;
    std::getline(iss, statusMsg);   // consume rest of status line
    while (std::getline(iss, line) && line != "\r" && !line.empty()) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\r')) val.erase(0, 1);
        while (!val.empty() && (val.back()  == '\r' || val.back()  == '\n')) val.pop_back();
        for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        r.headers[key] = val;
    }
    std::ostringstream bodyStream;
    std::string bodyLine;
    bool first = true;
    while (std::getline(iss, bodyLine)) {
        if (!first) bodyStream << '\n';
        bodyStream << bodyLine;
        first = false;
    }
    r.body = bodyStream.str();
    while (!r.body.empty() && (r.body.back() == '\r' || r.body.back() == '\n'))
        r.body.pop_back();
    return r;
}

// ---- ClientResponse -----------------------------------------------------
//
// The response object passed to the http.request callback.
// Supports .on('data', fn) and .on('end', fn) for streaming-style consumption.
// Since http.request buffers the full response before dispatching, both events
// fire synchronously after the user callback returns.

const proto::ProtoObject* clientResponseOnImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || !args || args->getSize(ctx) < 2) return self ? self : PROTO_NONE;
    std::string eventName;
    if (!argString(ctx, args, 0, eventName)) return self;
    const proto::ProtoObject* handler = args->getAt(ctx, 1);
    if (!handler || handler == PROTO_NONE) return self;

    const proto::ProtoString* lisKey =
        proto::ProtoString::createSymbol(ctx, "__listeners__");
    const proto::ProtoObject* listeners =
        lisKey ? self->getAttribute(ctx, lisKey, false) : nullptr;
    if (!listeners || listeners == PROTO_NONE) {
        listeners = ctx->newObject(/*mutable=*/true);
        if (lisKey) self->setAttribute(ctx, lisKey, listeners);
    }
    const proto::ProtoString* ek =
        ctx->fromUTF8String(eventName.c_str())->asString(ctx);
    if (ek) listeners->setAttribute(ctx, ek, handler);
    return self;
}

const proto::ProtoObject* getClientResponseProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* crproto = nullptr;
    if (crproto) return crproto;
    static const NativeEntry entries[] = {
        {"on", clientResponseOnImpl},
        NATIVE_MODULE_END
    };
    crproto = ProtoNativeModule::buildModule(ctx, entries, 1);
    return crproto;
}

// ---- dispatchClientResponse ---------------------------------------------
//
// Runs on the JS/EventLoop thread.  Builds a ClientResponse object,
// calls the user callback, then fires 'data' (if body non-empty) + 'end'.

void dispatchClientResponse(JSContextWrapper* wrapper,
                             proto::ProtoRootSet::Handle cbPin,
                             ParsedResponse resp) {
    if (!wrapper) { g_activeClients.fetch_sub(1); return; }
    JSContextWrapper::CurrentScope ws(wrapper);
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) { g_activeClients.fetch_sub(1); return; }

    proto::ProtoRootSet* rs = wrapper->getRootSet();
    const proto::ProtoObject* cb = rs ? rs->resolve(cbPin) : nullptr;
    if (rs) rs->remove(cbPin);
    g_activeClients.fetch_sub(1);

    if (!cb || cb == PROTO_NONE) return;

    // Build response object
    const proto::ProtoObject* responseProto = getClientResponseProto(ctx);
    const proto::ProtoObject* response = responseProto
        ? responseProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

    const proto::ProtoString* scKey = ctx->fromUTF8String("statusCode")->asString(ctx);
    if (scKey) response->setAttribute(ctx, scKey, ctx->fromInteger(resp.statusCode));

    response->setAttribute(ctx, keyHeaders(ctx), makeHeadersObject(ctx, resp.headers));
    response->setAttribute(ctx, keyBody(ctx), ctx->fromUTF8String(resp.body.c_str()));

    // Storage for .on() handlers
    const proto::ProtoString* lisKey =
        proto::ProtoString::createSymbol(ctx, "__listeners__");
    if (lisKey) response->setAttribute(ctx, lisKey, ctx->newObject(/*mutable=*/true));

    // Call user callback(response)
    const proto::ProtoList* cbArgs = ctx->newList()->appendLast(ctx, response);
    const ProtoBytecodeModule* mod =
        static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    callJSFunctionFromAsync(ctx, cb, PROTO_NONE, cbArgs, mod,
                             wrapper->getNativeGlobalRootPtr());

    // Fire registered events after the callback returns
    const proto::ProtoObject* listeners = lisKey
        ? response->getAttribute(ctx, lisKey, false) : nullptr;

    auto fireEvent = [&](const char* eventName, const proto::ProtoObject* arg) {
        if (!listeners || listeners == PROTO_NONE) return;
        const proto::ProtoString* ek =
            ctx->fromUTF8String(eventName)->asString(ctx);
        if (!ek) return;
        const proto::ProtoObject* handler = listeners->getAttribute(ctx, ek, false);
        if (!handler || handler == PROTO_NONE) return;
        const proto::ProtoObject* evArg = arg ? arg : PROTO_NONE;
        const proto::ProtoList* ea = ctx->newList()->appendLast(ctx, evArg);
        callJSFunctionFromAsync(ctx, handler, response, ea, mod,
                                 wrapper->getNativeGlobalRootPtr());
    };

    if (!resp.body.empty())
        fireEvent("data", ctx->fromUTF8String(resp.body.c_str()));
    fireEvent("end", PROTO_NONE);
}

// ---- ClientRequestState -------------------------------------------------

struct ClientRequestState {
    std::string                        method;
    std::string                        hostname;
    int                                port   = 80;
    std::string                        path;
    std::string                        body;
    bool                               sent   = false;
    JSContextWrapper*                  wrapper = nullptr;
    proto::ProtoRootSet::Handle        cbPin = proto::ProtoRootSet::kNullHandle;
};

void freeClientRequestState(void* p) {
    auto* s = static_cast<ClientRequestState*>(p);
    if (!s) return;
    if (s->wrapper && s->cbPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = s->wrapper->getRootSet();
        if (rs) rs->remove(s->cbPin);
        g_activeClients.fetch_sub(1);
    }
    delete s;
}

ClientRequestState* getClientRequestState(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoString* k =
        proto::ProtoString::createSymbol(ctx, "__creq_state__");
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<ClientRequestState*>(ext->getPointer(ctx)) : nullptr;
}

const proto::ProtoObject* clientRequestWriteImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ClientRequestState* s = getClientRequestState(ctx, self);
    if (!s || s->sent) return PROTO_FALSE;
    std::string chunk;
    if (argString(ctx, args, 0, chunk)) s->body += chunk;
    return PROTO_TRUE;
}

const proto::ProtoObject* clientRequestEndImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    // Append optional final chunk
    clientRequestWriteImpl(ctx, self, pl, args, kw);

    ClientRequestState* s = getClientRequestState(ctx, self);
    if (!s || s->sent) return self ? self : PROTO_NONE;
    s->sent = true;

    // Capture by value for the background thread
    std::string  method   = s->method;
    std::string  hostname = s->hostname;
    int          port     = s->port;
    std::string  path     = s->path;
    std::string  body     = s->body;
    JSContextWrapper* wrapper = s->wrapper;
    proto::ProtoRootSet::Handle cbPin = s->cbPin;
    s->cbPin = proto::ProtoRootSet::kNullHandle;  // ownership transferred to thread

    std::ostringstream reqStream;
    reqStream << method << " " << path << " HTTP/1.1\r\n";
    reqStream << "Host: " << hostname << "\r\n";
    reqStream << "Content-Length: " << body.size() << "\r\n";
    reqStream << "Connection: close\r\n\r\n" << body;
    std::string wireRequest = reqStream.str();

    std::thread([wrapper, cbPin, hostname, port, wireRequest]() mutable {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(static_cast<uint16_t>(port));

        if (inet_pton(AF_INET, hostname.c_str(), &addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(hostname.c_str());
            if (!he) {
                EventLoop::getInstance().enqueueCallback(
                    [wrapper, cbPin]() mutable {
                        if (wrapper) {
                            proto::ProtoRootSet* rs = wrapper->getRootSet();
                            if (rs && cbPin != proto::ProtoRootSet::kNullHandle)
                                rs->remove(cbPin);
                            g_activeClients.fetch_sub(1);
                        }
                    });
                return;
            }
            addr.sin_addr = *reinterpret_cast<in_addr*>(he->h_addr_list[0]);
        }

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            EventLoop::getInstance().enqueueCallback(
                [wrapper, cbPin]() mutable {
                    if (wrapper) {
                        proto::ProtoRootSet* rs = wrapper->getRootSet();
                        if (rs && cbPin != proto::ProtoRootSet::kNullHandle)
                            rs->remove(cbPin);
                        g_activeClients.fetch_sub(1);
                    }
                });
            return;
        }

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            EventLoop::getInstance().enqueueCallback(
                [wrapper, cbPin]() mutable {
                    if (wrapper) {
                        proto::ProtoRootSet* rs = wrapper->getRootSet();
                        if (rs && cbPin != proto::ProtoRootSet::kNullHandle)
                            rs->remove(cbPin);
                        g_activeClients.fetch_sub(1);
                    }
                });
            return;
        }

        // Send request
        const char* buf = wireRequest.data();
        size_t remaining = wireRequest.size();
        while (remaining > 0) {
            ssize_t n = ::write(fd, buf, remaining);
            if (n <= 0) break;
            buf += n;
            remaining -= static_cast<size_t>(n);
        }

        // Read full response until EOF (server sends Connection: close)
        std::string rawResponse;
        char chunk[4096];
        for (;;) {
            ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) break;
            rawResponse.append(chunk, static_cast<size_t>(n));
        }
        ::close(fd);

        ParsedResponse resp = parseHttpResponse(rawResponse);

        EventLoop::getInstance().enqueueCallback(
            [wrapper, cbPin, resp = std::move(resp)]() mutable {
                dispatchClientResponse(wrapper, cbPin, std::move(resp));
            });
    }).detach();

    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* getClientRequestProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* crqproto = nullptr;
    if (crqproto) return crqproto;
    static const NativeEntry entries[] = {
        {"write", clientRequestWriteImpl},
        {"end",   clientRequestEndImpl},
        NATIVE_MODULE_END
    };
    crqproto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return crqproto;
}

// ---- Module-level functions --------------------------------------------

const proto::ProtoObject* createServer(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;

    const proto::ProtoObject* serverProto = getServerProto(ctx);
    const proto::ProtoObject* server = serverProto
        ? serverProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

    auto* state = new ServerState{};
    state->wrapper = JSContextWrapper::current();

    // Pin the request listener (if provided) so it survives across the
    // accept-loop / event-loop hop without depending on JS-side reach.
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* cb = args->getAt(ctx, 0);
        if (cb && cb != PROTO_NONE && state->wrapper) {
            proto::ProtoRootSet* rs = state->wrapper->getRootSet();
            if (rs) state->listenerPin = rs->add(cb);
        }
    }

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeServerState);
    if (!extPtr) {
        delete state;
        return PROTO_NONE;
    }
    server->setAttribute(ctx, keyServerState(ctx), extPtr);
    server->setAttribute(ctx, keyFD(ctx), ctx->fromInteger(-1));
    server->setAttribute(ctx, keyPort(ctx), ctx->fromInteger(0));
    return server;
}

const proto::ProtoObject* clientRequest(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;

    const proto::ProtoObject* opts = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;
    const proto::ProtoObject* cb = (args && args->getSize(ctx) > 1)
        ? args->getAt(ctx, 1) : nullptr;

    auto getStrOpt = [&](const char* field, const std::string& def) -> std::string {
        if (!opts || opts == PROTO_NONE) return def;
        const proto::ProtoString* k = ctx->fromUTF8String(field)->asString(ctx);
        if (!k) return def;
        const proto::ProtoObject* v = opts->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE || !v->isString(ctx)) return def;
        std::string s; v->asString(ctx)->toUTF8String(ctx, s); return s;
    };
    auto getIntOpt = [&](const char* field, long long def) -> long long {
        if (!opts || opts == PROTO_NONE) return def;
        const proto::ProtoString* k = ctx->fromUTF8String(field)->asString(ctx);
        if (!k) return def;
        const proto::ProtoObject* v = opts->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE || !v->isInteger(ctx)) return def;
        return v->asLong(ctx);
    };

    auto* state = new ClientRequestState{};
    state->method   = getStrOpt("method",   "GET");
    state->hostname = getStrOpt("hostname", "localhost");
    state->port     = static_cast<int>(getIntOpt("port", 80));
    state->path     = getStrOpt("path",     "/");
    state->wrapper  = JSContextWrapper::current();

    if (cb && cb != PROTO_NONE && state->wrapper) {
        proto::ProtoRootSet* rs = state->wrapper->getRootSet();
        if (rs) {
            state->cbPin = rs->add(cb);
            g_activeClients.fetch_add(1);
        }
    }

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeClientRequestState);
    if (!extPtr) { delete state; return PROTO_NONE; }

    const proto::ProtoObject* reqProto = getClientRequestProto(ctx);
    const proto::ProtoObject* reqObj   = reqProto
        ? reqProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__creq_state__");
    if (stateKey) reqObj->setAttribute(ctx, stateKey, extPtr);
    return reqObj;
}

}  // namespace

int HTTPModule::getActiveServerCount() {
    return g_activeServers.load();
}

int HTTPModule::getActiveClientCount() {
    return g_activeClients.load();
}

const proto::ProtoObject* HTTPModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"createServer", createServer},
        {"request",      clientRequest},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 2);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "http", mod);
}

} // namespace protojs
