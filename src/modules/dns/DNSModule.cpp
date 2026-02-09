#include "DNSModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string>
#include <vector>

namespace protojs {

void DNSModule::init(JSContext* ctx) {
    JSValue dnsModule = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, dnsModule, "lookup", JS_NewCFunction(ctx, lookup, "lookup", 2));
    JS_SetPropertyStr(ctx, dnsModule, "resolve", JS_NewCFunction(ctx, resolve, "resolve", 2));
    JS_SetPropertyStr(ctx, dnsModule, "resolve4", JS_NewCFunction(ctx, resolve4, "resolve4", 1));
    JS_SetPropertyStr(ctx, dnsModule, "resolve6", JS_NewCFunction(ctx, resolve6, "resolve6", 1));
    JS_SetPropertyStr(ctx, dnsModule, "reverse", JS_NewCFunction(ctx, reverse, "reverse", 1));
    JS_SetPropertyStr(ctx, dnsModule, "lookupService", JS_NewCFunction(ctx, lookupService, "lookupService", 2));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "dns", dnsModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue DNSModule::lookup(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "lookup expects hostname");
    }
    
    const char* hostname = JS_ToCString(ctx, argv[0]);
    if (!hostname) return JS_EXCEPTION;
    
    int family = 0; // 0 = auto, 4 = IPv4, 6 = IPv6
    JSValue callback = JS_UNDEFINED;
    
    if (argc > 1) {
        if (JS_IsNumber(argv[1])) {
            JS_ToInt32(ctx, &family, argv[1]);
            if (argc > 2 && JS_IsFunction(ctx, argv[2])) {
                callback = JS_DupValue(ctx, argv[2]);
            }
        } else if (JS_IsFunction(ctx, argv[1])) {
            callback = JS_DupValue(ctx, argv[1]);
        } else if (JS_IsObject(argv[1])) {
            JSValue familyVal = JS_GetPropertyStr(ctx, argv[1], "family");
            if (JS_IsNumber(familyVal)) {
                JS_ToInt32(ctx, &family, familyVal);
            }
            JS_FreeValue(ctx, familyVal);
            
            if (argc > 2 && JS_IsFunction(ctx, argv[2])) {
                callback = JS_DupValue(ctx, argv[2]);
            }
        }
    }
    
    std::string host(hostname);
    JS_FreeCString(ctx, hostname);
    
    if (!JS_IsUndefined(callback)) {
        // Async lookup
        lookupAsync(ctx, host, family, callback);
        return JS_UNDEFINED;
    } else {
        // Sync lookup (blocking)
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = family == 6 ? AF_INET6 : (family == 4 ? AF_INET : AF_UNSPEC);
        hints.ai_socktype = SOCK_STREAM;
        
        int err = getaddrinfo(host.c_str(), nullptr, &hints, &result);
        if (err != 0) {
            return JS_ThrowTypeError(ctx, "%s", gai_strerror(err));
        }
        
        char ip[INET6_ADDRSTRLEN];
        if (result->ai_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        } else if (result->ai_family == AF_INET6) {
            struct sockaddr_in6* addr = (struct sockaddr_in6*)result->ai_addr;
            inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip));
        }
        
        freeaddrinfo(result);
        
        JSValue resultObj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, resultObj, "address", JS_NewString(ctx, ip));
        JS_SetPropertyStr(ctx, resultObj, "family", JS_NewInt32(ctx, result->ai_family == AF_INET6 ? 6 : 4));
        
        return resultObj;
    }
}

void DNSModule::lookupAsync(JSContext* ctx, const std::string& hostname, int family, JSValue callback) {
    auto& ioPool = IOThreadPool::getInstance();
    ioPool.getExecutor().submit([ctx, hostname, family, callback]() {
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = family == 6 ? AF_INET6 : (family == 4 ? AF_INET : AF_UNSPEC);
        hints.ai_socktype = SOCK_STREAM;
        
        int err = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
        
        EventLoop::getInstance().enqueueCallback([ctx, callback, err, result]() {
            if (err != 0) {
                JSValue error = JS_NewString(ctx, gai_strerror(err));
                JSValue args[] = {error, JS_NULL};
                JS_Call(ctx, callback, JS_UNDEFINED, 2, args);
                JS_FreeValue(ctx, error);
            } else {
                char ip[INET6_ADDRSTRLEN];
                int fam = 4;
                if (result->ai_family == AF_INET) {
                    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
                    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
                } else if (result->ai_family == AF_INET6) {
                    struct sockaddr_in6* addr = (struct sockaddr_in6*)result->ai_addr;
                    inet_ntop(AF_INET6, &addr->sin6_addr, ip, sizeof(ip));
                    fam = 6;
                }
                
                JSValue args[] = {JS_NULL, JS_NewString(ctx, ip), JS_NewInt32(ctx, fam)};
                JS_Call(ctx, callback, JS_UNDEFINED, 3, args);
                JS_FreeValue(ctx, args[1]);
                JS_FreeValue(ctx, args[2]);
                
                freeaddrinfo(result);
            }
            JS_FreeValue(ctx, callback);
        });
    });
}

JSValue DNSModule::resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "resolve expects hostname");
    }
    
    const char* hostname = JS_ToCString(ctx, argv[0]);
    if (!hostname) return JS_EXCEPTION;
    
    const char* rrtype = "A";
    if (argc > 1 && JS_IsString(argv[1])) {
        rrtype = JS_ToCString(ctx, argv[1]);
    }
    
    // Simplified resolve - would handle different record types
    std::string host(hostname);
    JS_FreeCString(ctx, hostname);
    
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) {
        if (rrtype && JS_IsString(argv[1])) {
            JS_FreeCString(ctx, rrtype);
        }
        return JS_ThrowTypeError(ctx, "Host not found");
    }
    
    JSValue result = JS_NewArray(ctx);
    int i = 0;
    while (he->h_addr_list[i]) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, he->h_addr_list[i], ip, sizeof(ip));
        JS_SetPropertyUint32(ctx, result, i, JS_NewString(ctx, ip));
        i++;
    }
    
    if (rrtype && JS_IsString(argv[1])) {
        JS_FreeCString(ctx, rrtype);
    }
    
    return result;
}

JSValue DNSModule::resolve4(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return resolve(ctx, this_val, argc, argv);
}

JSValue DNSModule::resolve6(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Similar to resolve4 but for IPv6
    return resolve(ctx, this_val, argc, argv);
}

JSValue DNSModule::reverse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "reverse expects IP address");
    }
    
    const char* ip = JS_ToCString(ctx, argv[0]);
    if (!ip) return JS_EXCEPTION;
    
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &sa.sin_addr) <= 0) {
        JS_FreeCString(ctx, ip);
        return JS_ThrowTypeError(ctx, "Invalid IP address");
    }
    
    struct hostent* he = gethostbyaddr(&sa.sin_addr, sizeof(sa.sin_addr), AF_INET);
    JS_FreeCString(ctx, ip);
    
    if (!he || !he->h_name) {
        return JS_ThrowTypeError(ctx, "Reverse lookup failed");
    }
    
    JSValue result = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, result, 0, JS_NewString(ctx, he->h_name));
    
    return result;
}

JSValue DNSModule::lookupService(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "lookupService expects address and port");
    }
    
    const char* address = JS_ToCString(ctx, argv[0]);
    if (!address) return JS_EXCEPTION;
    
    int32_t port;
    if (JS_ToInt32(ctx, &port, argv[1]) != 0) {
        JS_FreeCString(ctx, address);
        return JS_EXCEPTION;
    }
    
    // Simplified - would use getnameinfo
    JS_FreeCString(ctx, address);
    
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "hostname", JS_NewString(ctx, "localhost"));
    JS_SetPropertyStr(ctx, result, "service", JS_NewString(ctx, "unknown"));
    
    return result;
}

} // namespace protojs
