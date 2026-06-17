#include "DNSModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../JSContext.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../runtime/ProtoBytecodeModule.h"
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>

namespace protojs {

namespace {

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

const proto::ProtoObject* argAt(proto::ProtoContext* ctx,
                                 const proto::ProtoList* args, int idx) {
    if (!ctx || !args) return nullptr;
    if (idx >= static_cast<int>(args->getSize(ctx))) return nullptr;
    return args->getAt(ctx, idx);
}

bool isCallable(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!ctx || !o || o == PROTO_NONE) return false;
    if (o->isMethod(ctx)) return true;
    const proto::ProtoString* bcKey =
        proto::ProtoString::createSymbol(ctx, "__bytecode_id__");
    if (bcKey) {
        const proto::ProtoObject* v = o->getAttribute(ctx, bcKey, false);
        if (v && v->isInteger(ctx)) return true;
    }
    return false;
}

// Build an Array<string> from a vector of IPs.
const proto::ProtoObject* makeIpArray(proto::ProtoContext* ctx,
                                       const std::vector<std::string>& ips) {
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    if (!arr) return PROTO_NONE;
    const proto::ProtoList* els = ctx->newList();
    for (const auto& ip : ips) {
        els = els->appendLast(ctx, ctx->fromUTF8String(ip.c_str()));
    }
    setArrayElements(ctx, arr, els);
    return arr;
}

// ---- Sync helpers ------------------------------------------------------

const proto::ProtoObject* syncLookup(proto::ProtoContext* ctx,
                                      const std::string& host, int family) {
    struct addrinfo hints, *result = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = (family == 6) ? AF_INET6 :
                      (family == 4) ? AF_INET  : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (err != 0 || !result) {
        if (result) freeaddrinfo(result);
        return PROTO_NONE;  // surfacing as undefined; matches the
                            // observable JS contract (empty / undefined
                            // result on failure for the sync path).
    }
    char ip[INET6_ADDRSTRLEN] = {};
    int fam = 4;
    if (result->ai_family == AF_INET) {
        inet_ntop(AF_INET,
                  &reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr,
                  ip, sizeof(ip));
    } else if (result->ai_family == AF_INET6) {
        inet_ntop(AF_INET6,
                  &reinterpret_cast<sockaddr_in6*>(result->ai_addr)->sin6_addr,
                  ip, sizeof(ip));
        fam = 6;
    }
    int aiFamily = result->ai_family;
    (void)aiFamily;
    freeaddrinfo(result);

    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* addrKey =
        ctx->fromUTF8String("address")->asString(ctx);
    if (addrKey) obj->setAttribute(ctx, addrKey, ctx->fromUTF8String(ip));
    const proto::ProtoString* famKey =
        ctx->fromUTF8String("family")->asString(ctx);
    if (famKey) obj->setAttribute(ctx, famKey, ctx->fromInteger(fam));
    return obj;
}

std::vector<std::string> syncResolve4(const std::string& host) {
    std::vector<std::string> out;
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) return out;
    for (int i = 0; he->h_addr_list && he->h_addr_list[i]; ++i) {
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, he->h_addr_list[i], ip, sizeof(ip));
        out.emplace_back(ip);
    }
    return out;
}

// ---- ProtoMethods ------------------------------------------------------

const proto::ProtoObject* dnsLookup(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    std::string host;
    if (!argString(ctx, args, 0, host)) return PROTO_NONE;

    int family = 0;
    const proto::ProtoObject* cb = nullptr;
    const proto::ProtoObject* a1 = argAt(ctx, args, 1);
    const proto::ProtoObject* a2 = argAt(ctx, args, 2);
    if (a1 && a1->isInteger(ctx)) {
        family = static_cast<int>(a1->asLong(ctx));
        if (isCallable(ctx, a2)) cb = a2;
    } else if (isCallable(ctx, a1)) {
        cb = a1;
    } else if (a1 && !a1->isString(ctx) && !a1->isNone(ctx) &&
               !a1->isInteger(ctx) && !a1->isMethod(ctx)) {
        // Options object: read .family.
        const proto::ProtoString* fk =
            ctx->fromUTF8String("family")->asString(ctx);
        if (fk) {
            const proto::ProtoObject* fv = a1->getAttribute(ctx, fk, false);
            if (fv && fv->isInteger(ctx)) family = static_cast<int>(fv->asLong(ctx));
        }
        if (isCallable(ctx, a2)) cb = a2;
    }

    if (!cb) {
        // Sync path.
        return syncLookup(ctx, host, family);
    }

    // Async path: pin the callback in the wrapper's protoCore root
    // set, hand off the resolution to IOThreadPool, and dispatch the
    // result back through the EventLoop.
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    if (!rs) return PROTO_NONE;
    proto::ProtoRootSet::Handle pin = rs->add(cb);

    IOThreadPool::getInstance().getExecutor().submit([host, family, wrapper, pin]() {
        struct addrinfo hints, *result = nullptr;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = (family == 6) ? AF_INET6 :
                          (family == 4) ? AF_INET  : AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        int err = getaddrinfo(host.c_str(), nullptr, &hints, &result);

        char ip[INET6_ADDRSTRLEN] = {};
        int fam = 4;
        if (err == 0 && result) {
            if (result->ai_family == AF_INET) {
                inet_ntop(AF_INET,
                    &reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr,
                    ip, sizeof(ip));
            } else if (result->ai_family == AF_INET6) {
                inet_ntop(AF_INET6,
                    &reinterpret_cast<sockaddr_in6*>(result->ai_addr)->sin6_addr,
                    ip, sizeof(ip));
                fam = 6;
            }
        }
        if (result) freeaddrinfo(result);

        std::string errMsg = (err != 0) ? gai_strerror(err) : "";
        std::string ipStr = ip;
        EventLoop::getInstance().enqueueCallback(
            [wrapper, pin, errMsg, ipStr, fam, err]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope ws(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            proto::ProtoRootSet* rs = wrapper->getRootSet();
            const proto::ProtoObject* cb = rs ? rs->resolve(pin) : nullptr;
            if (rs) rs->remove(pin);
            if (!cb || cb == PROTO_NONE) return;

            const proto::ProtoList* cbArgs = c->newList();
            if (err != 0) {
                cbArgs = cbArgs->appendLast(c, c->fromUTF8String(errMsg.c_str()));
            } else {
                cbArgs = cbArgs
                    ->appendLast(c, PROTO_NONE)
                    ->appendLast(c, c->fromUTF8String(ipStr.c_str()))
                    ->appendLast(c, c->fromInteger(fam));
            }
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            callJSFunctionFromAsync(c, cb, PROTO_NONE, cbArgs, mod,
                                     wrapper->getNativeGlobalRootPtr());
        });
    });
    return PROTO_NONE;
}

const proto::ProtoObject* dnsResolve(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string host;
    if (!argString(ctx, args, 0, host)) return PROTO_NONE;
    return makeIpArray(ctx, syncResolve4(host));
}

const proto::ProtoObject* dnsResolve4(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return dnsResolve(ctx, self, pl, args, kw);
}

const proto::ProtoObject* dnsResolve6(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    // Mirror of the original module: same path as resolve4.  A real
    // IPv6 resolver would use AAAA records; tracked separately.
    return dnsResolve(ctx, self, pl, args, kw);
}

const proto::ProtoObject* dnsReverse(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string ipStr;
    if (!argString(ctx, args, 0, ipStr)) return PROTO_NONE;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, ipStr.c_str(), &sa.sin_addr) <= 0) {
        return makeIpArray(ctx, {});
    }
    struct hostent* he = gethostbyaddr(&sa.sin_addr, sizeof(sa.sin_addr), AF_INET);
    if (!he || !he->h_name) return makeIpArray(ctx, {});
    return makeIpArray(ctx, {std::string(he->h_name)});
}

const proto::ProtoObject* dnsLookupService(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string addr;
    const proto::ProtoObject* portArg = argAt(ctx, args, 1);
    if (!ctx || !argString(ctx, args, 0, addr) ||
        !portArg || !portArg->isInteger(ctx)) {
        return PROTO_NONE;
    }
    int port = static_cast<int>(portArg->asLong(ctx));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) <= 0) return PROTO_NONE;
    char host[NI_MAXHOST] = {};
    char serv[NI_MAXSERV] = {};
    int rc = getnameinfo(reinterpret_cast<sockaddr*>(&sa), sizeof(sa),
                          host, sizeof(host), serv, sizeof(serv), 0);
    if (rc != 0) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* hk = ctx->fromUTF8String("hostname")->asString(ctx);
    const proto::ProtoString* sk = ctx->fromUTF8String("service")->asString(ctx);
    if (hk) obj->setAttribute(ctx, hk, ctx->fromUTF8String(host));
    if (sk) obj->setAttribute(ctx, sk, ctx->fromUTF8String(serv));
    return obj;
}

}  // namespace

const proto::ProtoObject* DNSModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"lookup",        dnsLookup},
        {"resolve",       dnsResolve},
        {"resolve4",      dnsResolve4},
        {"resolve6",      dnsResolve6},
        {"reverse",       dnsReverse},
        {"lookupService", dnsLookupService},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 6);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "dns", mod);
}

} // namespace protojs
