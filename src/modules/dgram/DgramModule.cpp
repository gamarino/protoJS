#include "DgramModule.h"
#include "../../ProtoNativeModule.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../runtime/ProtoInterpreter.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace protojs {

namespace {

const proto::ProtoString* fdKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__fd__");
}
const proto::ProtoString* typeKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__type__");
}

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

int getFd(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self) return -1;
    const proto::ProtoObject* v = self->getAttribute(ctx, fdKey(ctx), false);
    return (v && v->isInteger(ctx)) ? static_cast<int>(v->asLong(ctx)) : -1;
}

bool isIPv6Socket(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self) return false;
    const proto::ProtoObject* v = self->getAttribute(ctx, typeKey(ctx), false);
    if (!v || !v->isString(ctx)) return false;
    std::string s;
    v->asString(ctx)->toUTF8String(ctx, s);
    return s == "udp6";
}

const proto::ProtoObject* socketBindImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd < 0) return PROTO_FALSE;
    long long port = 0;
    argInt(ctx, args, 0, port);
    std::string addr;
    if (!argString(ctx, args, 1, addr)) addr = "0.0.0.0";

    if (isIPv6Socket(ctx, self)) {
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(static_cast<uint16_t>(port));
        if (addr == "0.0.0.0" || addr.empty()) sa.sin6_addr = in6addr_any;
        else inet_pton(AF_INET6, addr.c_str(), &sa.sin6_addr);
        if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
            return PROTO_FALSE;
    } else {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(static_cast<uint16_t>(port));
        sa.sin_addr.s_addr = (addr == "0.0.0.0" || addr.empty())
            ? htonl(INADDR_ANY)
            : inet_addr(addr.c_str());
        if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
            return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* socketSendImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd < 0) return PROTO_FALSE;
    std::string data;
    long long port = 0;
    std::string host = "127.0.0.1";
    if (!argString(ctx, args, 0, data)) return PROTO_FALSE;
    argInt(ctx, args, 1, port);
    argString(ctx, args, 2, host);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    sa.sin_addr.s_addr = inet_addr(host.c_str());
    ssize_t n = sendto(fd, data.data(), data.size(), 0,
                       reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    return (n == static_cast<ssize_t>(data.size())) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* socketCloseImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd >= 0) {
        ::close(fd);
        if (self) self->setAttribute(ctx, fdKey(ctx), ctx->fromInteger(-1));
    }
    return PROTO_NONE;
}

// addMembership(multicastAddr, interfaceAddr?) — joins the multicast group
// via setsockopt.  For IPv4 sockets uses IP_ADD_MEMBERSHIP; for IPv6 sockets
// uses IPV6_JOIN_GROUP with an optional interface name resolved to an index.
const proto::ProtoObject* socketAddMembershipImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd < 0) return PROTO_FALSE;

    std::string mcastAddr;
    if (!argString(ctx, args, 0, mcastAddr)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "addMembership requires a multicast address string"));
        return PROTO_NONE;
    }
    std::string ifaceAddr;
    argString(ctx, args, 1, ifaceAddr);  // optional

    if (isIPv6Socket(ctx, self)) {
        ipv6_mreq mreq6{};
        if (inet_pton(AF_INET6, mcastAddr.c_str(), &mreq6.ipv6mr_multiaddr) <= 0) {
            signalNativeException(makeNativeError(ctx, "Error",
                "addMembership: invalid IPv6 multicast address"));
            return PROTO_NONE;
        }
        // interface index 0 = kernel picks the default interface
        mreq6.ipv6mr_interface = 0;
        if (!ifaceAddr.empty()) {
            unsigned int idx = if_nametoindex(ifaceAddr.c_str());
            if (idx == 0) {
                signalNativeException(makeNativeError(ctx, "Error",
                    "addMembership: unknown interface name"));
                return PROTO_NONE;
            }
            mreq6.ipv6mr_interface = idx;
        }
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                        &mreq6, sizeof(mreq6)) != 0) {
            return PROTO_FALSE;
        }
        return PROTO_TRUE;
    }

    // IPv4 path (unchanged)
    ip_mreq mreq{};
    if (inet_pton(AF_INET, mcastAddr.c_str(), &mreq.imr_multiaddr) <= 0) {
        signalNativeException(makeNativeError(ctx, "Error",
            "addMembership: invalid multicast address"));
        return PROTO_NONE;
    }
    if (ifaceAddr.empty()) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, ifaceAddr.c_str(), &mreq.imr_interface) <= 0) {
        signalNativeException(makeNativeError(ctx, "Error",
            "addMembership: invalid interface address"));
        return PROTO_NONE;
    }
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    &mreq, sizeof(mreq)) != 0) {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* socketSetBroadcastImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd < 0 || !args || args->getSize(ctx) == 0) return PROTO_FALSE;
    int on = (args->getAt(ctx, 0) == PROTO_TRUE) ? 1 : 0;
    return (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) == 0)
        ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* socketAddressImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd < 0) return PROTO_NONE;
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len) != 0)
        return PROTO_NONE;
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    auto setStr = [&](const char* k, const char* v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, ctx->fromUTF8String(v));
    };
    auto setI = [&](const char* k, long long v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, ctx->fromInteger(v));
    };
    setStr("address", ip);
    setStr("family", "IPv4");
    setI("port", static_cast<long long>(ntohs(sa.sin_port)));
    return obj;
}

const proto::ProtoObject* getSocketProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"bind",          socketBindImpl},
        {"send",          socketSendImpl},
        {"close",         socketCloseImpl},
        {"addMembership", socketAddMembershipImpl},
        {"setBroadcast",  socketSetBroadcastImpl},
        {"address",       socketAddressImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 6);
    return proto;
}

const proto::ProtoObject* createSocketImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string type = "udp4";
    argString(ctx, args, 0, type);
    int family = (type == "udp6") ? AF_INET6 : AF_INET;
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) return PROTO_NONE;
    const proto::ProtoObject* proto = getSocketProto(ctx);
    const proto::ProtoObject* sock = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    sock->setAttribute(ctx, fdKey(ctx), ctx->fromInteger(fd));
    sock->setAttribute(ctx, typeKey(ctx), ctx->fromUTF8String(type.c_str()));
    return sock;
}

}  // namespace

const proto::ProtoObject* DgramModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"createSocket", createSocketImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 1);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "dgram", mod);
}

} // namespace protojs
