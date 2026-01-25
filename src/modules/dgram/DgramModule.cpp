#include "DgramModule.h"
#include "../events/EventsModule.h"
#include "../buffer/BufferModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <mutex>

namespace protojs {

static JSClassID dgram_socket_class_id;

struct DgramSocketData {
    int socketFd;
    bool bound;
    bool closed;
    int port;
    std::string address;
    JSRuntime* rt;
    JSValue eventEmitter;
    std::thread receiveThread;
    std::mutex mutex;
    
    DgramSocketData(JSRuntime* r) : socketFd(-1), bound(false), closed(false), port(0), rt(r), eventEmitter(JS_UNDEFINED) {}
    ~DgramSocketData() {
        close();
        if (!JS_IsUndefined(eventEmitter)) {
            JS_FreeValueRT(rt, eventEmitter);
        }
    }
    
    void close() {
        std::lock_guard<std::mutex> lock(mutex);
        if (closed) return;
        closed = true;
        bound = false;
        if (socketFd >= 0) {
            ::close(socketFd);
            socketFd = -1;
        }
        if (receiveThread.joinable()) {
            receiveThread.join();
        }
    }
};

void DgramModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Socket class
    JS_NewClassID(&dgram_socket_class_id);
    JSClassDef socketClassDef = {
        "DgramSocket",
        SocketFinalizer
    };
    JS_NewClass(rt, dgram_socket_class_id, &socketClassDef);
    
    JSValue socketProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, socketProto, "bind", JS_NewCFunction(ctx, socketBind, "bind", 1));
    JS_SetPropertyStr(ctx, socketProto, "send", JS_NewCFunction(ctx, socketSend, "send", 1));
    JS_SetPropertyStr(ctx, socketProto, "close", JS_NewCFunction(ctx, socketClose, "close", 0));
    JS_SetPropertyStr(ctx, socketProto, "addMembership", JS_NewCFunction(ctx, socketAddMembership, "addMembership", 1));
    JS_SetPropertyStr(ctx, socketProto, "setBroadcast", JS_NewCFunction(ctx, socketSetBroadcast, "setBroadcast", 1));
    JS_SetPropertyStr(ctx, socketProto, "address", JS_NewCFunction(ctx, socketAddress, "address", 0));
    JS_SetClassProto(ctx, dgram_socket_class_id, socketProto);
    
    // Create dgram module
    JSValue dgramModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, dgramModule, "createSocket", JS_NewCFunction(ctx, createSocket, "createSocket", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "dgram", dgramModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue DgramModule::createSocket(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "createSocket expects type ('udp4' or 'udp6')");
    }
    
    const char* type = JS_ToCString(ctx, argv[0]);
    if (!type) return JS_EXCEPTION;
    
    int domain = AF_INET;
    if (strcmp(type, "udp6") == 0) {
        domain = AF_INET6;
    } else if (strcmp(type, "udp4") != 0) {
        JS_FreeCString(ctx, type);
        return JS_ThrowTypeError(ctx, "createSocket expects 'udp4' or 'udp6'");
    }
    JS_FreeCString(ctx, type);
    
    JSValue socket = JS_NewObjectClass(ctx, dgram_socket_class_id);
    if (JS_IsException(socket)) return socket;
    
    DgramSocketData* data = new DgramSocketData(JS_GetRuntime(ctx));
    
    // Create socket
    int sock = ::socket(domain, SOCK_DGRAM, 0);
    if (sock < 0) {
        delete data;
        return JS_ThrowTypeError(ctx, "Failed to create UDP socket");
    }
    
    data->socketFd = sock;
    
    // Create EventEmitter
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            data->eventEmitter = JS_DupValue(ctx, emitter);
            JS_SetPropertyStr(ctx, socket, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    JS_SetOpaque(socket, data);
    return socket;
}

JSValue DgramModule::socketBind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(this_val, dgram_socket_class_id));
    if (!data) {
        return JS_ThrowTypeError(ctx, "Invalid socket object");
    }
    
    std::lock_guard<std::mutex> lock(data->mutex);
    if (data->bound) {
        return JS_ThrowTypeError(ctx, "Socket already bound");
    }
    
    int port = 0;
    std::string address = "0.0.0.0";
    
    if (argc > 0 && JS_IsNumber(argv[0])) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (argc > 1 && JS_IsString(argv[1])) {
            const char* addrStr = JS_ToCString(ctx, argv[1]);
            if (addrStr) {
                address = addrStr;
                JS_FreeCString(ctx, addrStr);
            }
        }
    } else if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue portVal = JS_GetPropertyStr(ctx, argv[0], "port");
        if (JS_IsNumber(portVal)) {
            JS_ToInt32(ctx, &port, portVal);
        }
        JS_FreeValue(ctx, portVal);
        
        JSValue addrVal = JS_GetPropertyStr(ctx, argv[0], "address");
        if (JS_IsString(addrVal)) {
            const char* addrStr = JS_ToCString(ctx, addrVal);
            if (addrStr) {
                address = addrStr;
                JS_FreeCString(ctx, addrStr);
            }
        }
        JS_FreeValue(ctx, addrVal);
    }
    
    if (port < 0 || port > 65535) {
        return JS_ThrowTypeError(ctx, "Invalid port number");
    }
    
    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (address == "0.0.0.0" || address.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
            return JS_ThrowTypeError(ctx, "Invalid address");
        }
    }
    
    if (bind(data->socketFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return JS_ThrowTypeError(ctx, "bind() failed");
    }
    
    // Get actual port if port was 0
    if (port == 0) {
        struct sockaddr_in actualAddr;
        socklen_t len = sizeof(actualAddr);
        if (getsockname(data->socketFd, (struct sockaddr*)&actualAddr, &len) == 0) {
            port = ntohs(actualAddr.sin_port);
        }
    }
    
    data->port = port;
    data->address = address;
    data->bound = true;
    
    // Start receive thread
    data->receiveThread = std::thread([data, ctx]() {
        char buffer[65507]; // Max UDP datagram size
        while (data->bound && !data->closed) {
            struct sockaddr_in fromAddr;
            socklen_t fromLen = sizeof(fromAddr);
            
            ssize_t n = recvfrom(data->socketFd, buffer, sizeof(buffer), 0, 
                                (struct sockaddr*)&fromAddr, &fromLen);
            if (n < 0) {
                if (data->bound && !data->closed) {
                    continue;
                }
                break;
            }
            
            // Emit message event
            if (!JS_IsUndefined(data->eventEmitter)) {
                JSValue messageEvent = JS_NewString(ctx, "message");
                JSValue bufferObj = JS_NewArrayBufferCopy(ctx, (uint8_t*)buffer, n);
                
                // Create rinfo object
                JSValue rinfo = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, rinfo, "address", JS_NewString(ctx, inet_ntoa(fromAddr.sin_addr)));
                JS_SetPropertyStr(ctx, rinfo, "port", JS_NewInt32(ctx, ntohs(fromAddr.sin_port)));
                JS_SetPropertyStr(ctx, rinfo, "family", JS_NewString(ctx, "IPv4"));
                JS_SetPropertyStr(ctx, rinfo, "size", JS_NewInt32(ctx, n));
                
                EventLoop::getInstance().enqueueCallback([ctx, data, messageEvent, bufferObj, rinfo]() {
                    JSValue emit = JS_GetPropertyStr(ctx, data->eventEmitter, "emit");
                    if (JS_IsFunction(ctx, emit)) {
                        JSValue args[] = {messageEvent, bufferObj, rinfo};
                        JS_Call(ctx, emit, data->eventEmitter, 3, args);
                    }
                    JS_FreeValue(ctx, emit);
                    JS_FreeValue(ctx, messageEvent);
                    JS_FreeValue(ctx, bufferObj);
                    JS_FreeValue(ctx, rinfo);
                });
            }
        }
    });
    
    return JS_UNDEFINED;
}

JSValue DgramModule::socketSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(this_val, dgram_socket_class_id));
    if (!data || !data->bound) {
        return JS_ThrowTypeError(ctx, "Socket not bound");
    }
    
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "send expects (msg, offset, length, port, address, callback)");
    }
    
    std::vector<uint8_t> bytes = getDataFromJSValue(ctx, argv[0]);
    if (bytes.empty()) {
        return JS_NewBool(ctx, false);
    }
    
    int32_t offset = 0;
    int32_t length = bytes.size();
    int32_t port = 0;
    std::string address = "localhost";
    
    if (argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &offset, argv[1]);
    }
    if (argc > 2 && JS_IsNumber(argv[2])) {
        JS_ToInt32(ctx, &length, argv[2]);
    }
    if (argc > 3 && JS_IsNumber(argv[3])) {
        JS_ToInt32(ctx, &port, argv[3]);
    }
    if (argc > 4 && JS_IsString(argv[4])) {
        const char* addrStr = JS_ToCString(ctx, argv[4]);
        if (addrStr) {
            address = addrStr;
            JS_FreeCString(ctx, addrStr);
        }
    }
    
    if (port <= 0 || port > 65535) {
        return JS_ThrowTypeError(ctx, "Invalid port number");
    }
    
    if (offset < 0 || offset + length > (int32_t)bytes.size()) {
        return JS_ThrowTypeError(ctx, "Invalid offset or length");
    }
    
    // Send datagram
    struct sockaddr_in toAddr;
    memset(&toAddr, 0, sizeof(toAddr));
    toAddr.sin_family = AF_INET;
    toAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &toAddr.sin_addr) <= 0) {
        return JS_ThrowTypeError(ctx, "Invalid address");
    }
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([data, bytes, offset, length, toAddr]() -> ssize_t {
        return sendto(data->socketFd, bytes.data() + offset, length, 0, 
                     (struct sockaddr*)&toAddr, sizeof(toAddr));
    });
    
    ssize_t sent = future.get();
    return JS_NewBool(ctx, sent > 0);
}

JSValue DgramModule::socketClose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(this_val, dgram_socket_class_id));
    if (data) {
        data->close();
    }
    return JS_UNDEFINED;
}

JSValue DgramModule::socketAddMembership(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "addMembership expects multicast address");
    }
    
    const char* multicastAddr = JS_ToCString(ctx, argv[0]);
    if (!multicastAddr) return JS_EXCEPTION;
    
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(this_val, dgram_socket_class_id));
    if (!data || data->socketFd < 0) {
        JS_FreeCString(ctx, multicastAddr);
        return JS_ThrowTypeError(ctx, "Invalid socket");
    }
    
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    if (inet_pton(AF_INET, multicastAddr, &mreq.imr_multiaddr) <= 0) {
        JS_FreeCString(ctx, multicastAddr);
        return JS_ThrowTypeError(ctx, "Invalid multicast address");
    }
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(data->socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        JS_FreeCString(ctx, multicastAddr);
        return JS_ThrowTypeError(ctx, "addMembership() failed");
    }
    
    JS_FreeCString(ctx, multicastAddr);
    return JS_UNDEFINED;
}

JSValue DgramModule::socketSetBroadcast(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setBroadcast expects a boolean");
    }
    
    bool flag = JS_ToBool(ctx, argv[0]);
    
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(this_val, dgram_socket_class_id));
    if (!data || data->socketFd < 0) {
        return JS_ThrowTypeError(ctx, "Invalid socket");
    }
    
    int opt = flag ? 1 : 0;
    if (setsockopt(data->socketFd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        return JS_ThrowTypeError(ctx, "setBroadcast() failed");
    }
    
    return JS_UNDEFINED;
}

JSValue DgramModule::socketAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(this_val, dgram_socket_class_id));
    if (!data || !data->bound) {
        return JS_NULL;
    }
    
    JSValue addr = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, addr, "port", JS_NewInt32(ctx, data->port));
    JS_SetPropertyStr(ctx, addr, "family", JS_NewString(ctx, "IPv4"));
    JS_SetPropertyStr(ctx, addr, "address", JS_NewString(ctx, data->address.c_str()));
    
    return addr;
}

void DgramModule::SocketFinalizer(JSRuntime* rt, JSValue val) {
    DgramSocketData* data = static_cast<DgramSocketData*>(JS_GetOpaque(val, dgram_socket_class_id));
    if (data) {
        delete data;
    }
}

std::vector<uint8_t> DgramModule::getDataFromJSValue(JSContext* ctx, JSValueConst val) {
    std::vector<uint8_t> result;
    
    // Check if it's a Buffer
    JSValue bufferCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "Buffer");
    if (!JS_IsUndefined(bufferCtor)) {
        JSValue isBuffer = JS_GetPropertyStr(ctx, bufferCtor, "isBuffer");
        if (JS_IsFunction(ctx, isBuffer)) {
            JSValue dupVal = JS_DupValue(ctx, val);
            JSValueConst args[] = {dupVal};
            JSValue isBuf = JS_Call(ctx, isBuffer, bufferCtor, 1, const_cast<JSValue*>(args));
            JS_FreeValue(ctx, dupVal);
            if (JS_ToBool(ctx, isBuf)) {
                JSValue length = JS_GetPropertyStr(ctx, val, "length");
                uint32_t len;
                if (JS_ToUint32(ctx, &len, length) >= 0) {
                    result.resize(len);
                    // Simplified: convert to string and back
                    JSValue str = JS_GetPropertyStr(ctx, val, "toString");
                    if (JS_IsFunction(ctx, str)) {
                        JSValue strVal = JS_Call(ctx, str, val, 0, nullptr);
                        const char* strData = JS_ToCString(ctx, strVal);
                        if (strData) {
                            result.assign((uint8_t*)strData, (uint8_t*)strData + strlen(strData));
                            JS_FreeCString(ctx, strData);
                        }
                        JS_FreeValue(ctx, strVal);
                    }
                    JS_FreeValue(ctx, str);
                }
                JS_FreeValue(ctx, length);
            }
            JS_FreeValue(ctx, isBuf);
        }
        JS_FreeValue(ctx, isBuffer);
    }
    JS_FreeValue(ctx, bufferCtor);
    
    // If not buffer, try string
    if (result.empty() && JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        if (str) {
            result.assign((uint8_t*)str, (uint8_t*)str + strlen(str));
            JS_FreeCString(ctx, str);
        }
    }
    
    return result;
}

} // namespace protojs
