#include "NetModule.h"
#include "../events/EventsModule.h"
#include "../buffer/BufferModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../JSContext.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <queue>
#include <mutex>

namespace protojs {

static JSClassID net_server_class_id;
static JSClassID net_socket_class_id;

struct NetServerData {
    int socketFd;
    int port;
    std::string host;
    bool listening;
    bool closed;
    JSValue connectionListener;
    JSRuntime* rt;
    std::thread acceptThread;
    std::mutex mutex;
    
    NetServerData(JSRuntime* r) : socketFd(-1), port(0), listening(false), closed(false), 
                                   connectionListener(JS_UNDEFINED), rt(r) {}
    ~NetServerData() {
        close();
        if (!JS_IsUndefined(connectionListener)) {
            JS_FreeValueRT(rt, connectionListener);
        }
    }
    
    void close() {
        std::lock_guard<std::mutex> lock(mutex);
        if (closed) return;
        closed = true;
        listening = false;
        if (socketFd >= 0) {
            ::close(socketFd);
            socketFd = -1;
        }
        if (acceptThread.joinable()) {
            acceptThread.join();
        }
    }
};

struct NetSocketData {
    int socketFd;
    bool connected;
    bool destroyed;
    std::string remoteAddress;
    int remotePort;
    std::string localAddress;
    int localPort;
    JSRuntime* rt;
    JSValue eventEmitter;
    std::thread readThread;
    std::mutex mutex;
    bool reading;
    
    NetSocketData(JSRuntime* r) : socketFd(-1), connected(false), destroyed(false),
                                   remotePort(0), localPort(0), rt(r), eventEmitter(JS_UNDEFINED), reading(false) {}
    ~NetSocketData() {
        destroy();
        if (!JS_IsUndefined(eventEmitter)) {
            JS_FreeValueRT(rt, eventEmitter);
        }
    }
    
    void destroy() {
        std::lock_guard<std::mutex> lock(mutex);
        if (destroyed) return;
        destroyed = true;
        connected = false;
        reading = false;
        if (socketFd >= 0) {
            ::close(socketFd);
            socketFd = -1;
        }
        if (readThread.joinable()) {
            readThread.join();
        }
    }
};

void NetModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Server class
    JS_NewClassID(&net_server_class_id);
    JSClassDef serverClassDef = {
        "NetServer",
        ServerFinalizer
    };
    JS_NewClass(rt, net_server_class_id, &serverClassDef);
    
    JSValue serverProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, serverProto, "listen", JS_NewCFunction(ctx, serverListen, "listen", 1));
    JS_SetPropertyStr(ctx, serverProto, "close", JS_NewCFunction(ctx, serverClose, "close", 0));
    JS_SetPropertyStr(ctx, serverProto, "address", JS_NewCFunction(ctx, serverAddress, "address", 0));
    JS_SetClassProto(ctx, net_server_class_id, serverProto);
    
    // Register Socket class
    JS_NewClassID(&net_socket_class_id);
    JSClassDef socketClassDef = {
        "NetSocket",
        SocketFinalizer
    };
    JS_NewClass(rt, net_socket_class_id, &socketClassDef);
    
    JSValue socketProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, socketProto, "connect", JS_NewCFunction(ctx, socketConnect, "connect", 2));
    JS_SetPropertyStr(ctx, socketProto, "write", JS_NewCFunction(ctx, socketWrite, "write", 1));
    JS_SetPropertyStr(ctx, socketProto, "end", JS_NewCFunction(ctx, socketEnd, "end", 1));
    JS_SetPropertyStr(ctx, socketProto, "destroy", JS_NewCFunction(ctx, socketDestroy, "destroy", 0));
    JS_SetPropertyStr(ctx, socketProto, "address", JS_NewCFunction(ctx, socketAddress, "address", 0));
    JS_SetClassProto(ctx, net_socket_class_id, socketProto);
    
    // Create net module
    JSValue netModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, netModule, "createServer", JS_NewCFunction(ctx, createServer, "createServer", 1));
    JS_SetPropertyStr(ctx, netModule, "createConnection", JS_NewCFunction(ctx, createConnection, "createConnection", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "net", netModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue NetModule::createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue server = JS_NewObjectClass(ctx, net_server_class_id);
    if (JS_IsException(server)) return server;
    
    NetServerData* data = new NetServerData(JS_GetRuntime(ctx));
    
    // Set connection listener if provided
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        data->connectionListener = JS_DupValue(ctx, argv[0]);
    } else if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue listener = JS_GetPropertyStr(ctx, argv[0], "connectionListener");
        if (JS_IsFunction(ctx, listener)) {
            data->connectionListener = JS_DupValue(ctx, listener);
        }
        JS_FreeValue(ctx, listener);
    }
    
    // Create EventEmitter for server
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            JS_SetPropertyStr(ctx, server, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    JS_SetOpaque(server, data);
    return server;
}

JSValue NetModule::serverListen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetServerData* data = static_cast<NetServerData*>(JS_GetOpaque(this_val, net_server_class_id));
    if (!data) {
        return JS_ThrowTypeError(ctx, "Invalid server object");
    }
    
    std::lock_guard<std::mutex> lock(data->mutex);
    if (data->listening) {
        return JS_ThrowTypeError(ctx, "Server already listening");
    }
    
    // Parse arguments
    int port = 0;
    std::string host = "0.0.0.0";
    
    if (argc > 0 && JS_IsNumber(argv[0])) {
        JS_ToInt32(ctx, &port, argv[0]);
    } else if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue portVal = JS_GetPropertyStr(ctx, argv[0], "port");
        if (JS_IsNumber(portVal)) {
            JS_ToInt32(ctx, &port, portVal);
        }
        JS_FreeValue(ctx, portVal);
        
        JSValue hostVal = JS_GetPropertyStr(ctx, argv[0], "host");
        if (JS_IsString(hostVal)) {
            const char* hostStr = JS_ToCString(ctx, hostVal);
            if (hostStr) {
                host = hostStr;
                JS_FreeCString(ctx, hostStr);
            }
        }
        JS_FreeValue(ctx, hostVal);
    }
    
    if (port < 0 || port > 65535) {
        return JS_ThrowTypeError(ctx, "Invalid port number");
    }
    
    data->port = port;
    data->host = host;
    
    // Create socket in IO thread
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([data, port, host]() -> int {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;
        
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (host == "0.0.0.0" || host.empty()) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
                close(sock);
                return -1;
            }
        }
        
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return -1;
        }
        
        // Get actual port if port was 0 (OS assigned)
        if (port == 0) {
            struct sockaddr_in actualAddr;
            socklen_t len = sizeof(actualAddr);
            if (getsockname(sock, (struct sockaddr*)&actualAddr, &len) == 0) {
                data->port = ntohs(actualAddr.sin_port);
            }
        }
        
        if (listen(sock, 128) < 0) {
            close(sock);
            return -1;
        }
        
        return sock;
    });
    
    // Wait for socket creation
    int sock = future.get();
    if (sock < 0) {
        return JS_ThrowTypeError(ctx, "Failed to create server socket");
    }
    
    data->socketFd = sock;
    data->listening = true;
    
    // Start accept thread
    data->acceptThread = std::thread([data, ctx]() {
        while (data->listening && !data->closed) {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            
            int clientFd = accept(data->socketFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd < 0) {
                if (data->listening && !data->closed) {
                    continue;
                }
                break;
            }
            
            // Create socket object for connection
            JSContext* workerCtx = JS_NewContext(JS_GetRuntime(ctx));
            if (workerCtx) {
                JSValue socket = JS_NewObjectClass(workerCtx, net_socket_class_id);
                if (!JS_IsException(socket)) {
                    NetSocketData* socketData = new NetSocketData(JS_GetRuntime(workerCtx));
                    socketData->socketFd = clientFd;
                    socketData->connected = true;
                    socketData->remoteAddress = inet_ntoa(clientAddr.sin_addr);
                    socketData->remotePort = ntohs(clientAddr.sin_port);
                    
                    // Get local address
                    struct sockaddr_in localAddr;
                    socklen_t localLen = sizeof(localAddr);
                    getsockname(clientFd, (struct sockaddr*)&localAddr, &localLen);
                    socketData->localAddress = inet_ntoa(localAddr.sin_addr);
                    socketData->localPort = ntohs(localAddr.sin_port);
                    
                    // Create EventEmitter
                    JSValue eventEmitterCtor = JS_GetPropertyStr(workerCtx, JS_GetGlobalObject(workerCtx), "EventEmitter");
                    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(workerCtx, eventEmitterCtor)) {
                        JSValue emitter = JS_CallConstructor(workerCtx, eventEmitterCtor, 0, nullptr);
                        if (!JS_IsException(emitter)) {
                            socketData->eventEmitter = JS_DupValue(workerCtx, emitter);
                            JS_SetPropertyStr(workerCtx, socket, "_events", emitter);
                        }
                        JS_FreeValue(workerCtx, emitter);
                    }
                    JS_FreeValue(workerCtx, eventEmitterCtor);
                    
                    JS_SetOpaque(socket, socketData);
                    
                    // Start read thread for this socket
                    socketData->readThread = std::thread([socketData, workerCtx]() {
                        char buffer[4096];
                        while (socketData->connected && !socketData->destroyed) {
                            ssize_t n = recv(socketData->socketFd, buffer, sizeof(buffer), 0);
                            if (n <= 0) {
                                socketData->connected = false;
                                break;
                            }
                            
                            // Emit data event
                            if (!JS_IsUndefined(socketData->eventEmitter)) {
                                JSValue dataEvent = JS_NewString(workerCtx, "data");
                                JSValue bufferObj = JS_NewArrayBufferCopy(workerCtx, (uint8_t*)buffer, n);
                                
                                EventLoop::getInstance().enqueueCallback([workerCtx, socketData, dataEvent, bufferObj]() {
                                    JSValue emit = JS_GetPropertyStr(workerCtx, socketData->eventEmitter, "emit");
                                    if (JS_IsFunction(workerCtx, emit)) {
                                        JSValue args[] = {dataEvent, bufferObj};
                                        JS_Call(workerCtx, emit, socketData->eventEmitter, 2, args);
                                    }
                                    JS_FreeValue(workerCtx, emit);
                                    JS_FreeValue(workerCtx, dataEvent);
                                    JS_FreeValue(workerCtx, bufferObj);
                                });
                            }
                        }
                        
                        // Emit end event
                        if (!JS_IsUndefined(socketData->eventEmitter)) {
                            EventLoop::getInstance().enqueueCallback([workerCtx, socketData]() {
                                    JSValue emit = JS_GetPropertyStr(workerCtx, socketData->eventEmitter, "emit");
                                    if (JS_IsFunction(workerCtx, emit)) {
                                        JSValue endEvent = JS_NewString(workerCtx, "end");
                                        JSValue args[] = {endEvent};
                                        JS_Call(workerCtx, emit, socketData->eventEmitter, 1, args);
                                        JS_FreeValue(workerCtx, endEvent);
                                    }
                                    JS_FreeValue(workerCtx, emit);
                            });
                        }
                    });
                    
                    // Call connection listener
                    if (!JS_IsUndefined(data->connectionListener)) {
                        JSValue socketDup = JS_DupValue(workerCtx, socket);
                        EventLoop::getInstance().enqueueCallback([ctx, data, socketDup]() {
                            JSValueConst args[] = {socketDup};
                            JSValue result = JS_Call(ctx, data->connectionListener, JS_UNDEFINED, 1, const_cast<JSValue*>(args));
                            if (JS_IsException(result)) {
                                JSValue exception = JS_GetException(ctx);
                                // Log error
                                JS_FreeValue(ctx, exception);
                            }
                            JS_FreeValue(ctx, result);
                            JS_FreeValue(ctx, socketDup);
                        });
                    }
                }
                JS_FreeContext(workerCtx);
            }
        }
    });
    
    return JS_UNDEFINED;
}

JSValue NetModule::serverClose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetServerData* data = static_cast<NetServerData*>(JS_GetOpaque(this_val, net_server_class_id));
    if (data) {
        data->close();
    }
    return JS_UNDEFINED;
}

JSValue NetModule::serverAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetServerData* data = static_cast<NetServerData*>(JS_GetOpaque(this_val, net_server_class_id));
    if (!data || !data->listening) {
        return JS_NULL;
    }
    
    JSValue addr = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, addr, "port", JS_NewInt32(ctx, data->port));
    JS_SetPropertyStr(ctx, addr, "family", JS_NewString(ctx, "IPv4"));
    JS_SetPropertyStr(ctx, addr, "address", JS_NewString(ctx, data->host.c_str()));
    
    return addr;
}

void NetModule::ServerFinalizer(JSRuntime* rt, JSValue val) {
    NetServerData* data = static_cast<NetServerData*>(JS_GetOpaque(val, net_server_class_id));
    if (data) {
        delete data;
    }
}

JSValue NetModule::createConnection(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue socket = JS_NewObjectClass(ctx, net_socket_class_id);
    if (JS_IsException(socket)) return socket;
    
    NetSocketData* data = new NetSocketData(JS_GetRuntime(ctx));
    
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
    
    // Auto-connect if options provided
    if (argc > 0) {
        socketConnect(ctx, socket, argc, argv);
    }
    
    return socket;
}

JSValue NetModule::socketConnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetSocketData* data = static_cast<NetSocketData*>(JS_GetOpaque(this_val, net_socket_class_id));
    if (!data) {
        return JS_ThrowTypeError(ctx, "Invalid socket object");
    }
    
    if (data->connected) {
        return JS_ThrowTypeError(ctx, "Socket already connected");
    }
    
    int port = 0;
    std::string host = "localhost";
    
    if (argc > 0 && JS_IsNumber(argv[0])) {
        JS_ToInt32(ctx, &port, argv[0]);
        if (argc > 1 && JS_IsString(argv[1])) {
            const char* hostStr = JS_ToCString(ctx, argv[1]);
            if (hostStr) {
                host = hostStr;
                JS_FreeCString(ctx, hostStr);
            }
        }
    } else if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue portVal = JS_GetPropertyStr(ctx, argv[0], "port");
        if (JS_IsNumber(portVal)) {
            JS_ToInt32(ctx, &port, portVal);
        }
        JS_FreeValue(ctx, portVal);
        
        JSValue hostVal = JS_GetPropertyStr(ctx, argv[0], "host");
        if (JS_IsString(hostVal)) {
            const char* hostStr = JS_ToCString(ctx, hostVal);
            if (hostStr) {
                host = hostStr;
                JS_FreeCString(ctx, hostStr);
            }
        }
        JS_FreeValue(ctx, hostVal);
    }
    
    if (port < 0 || port > 65535) {
        return JS_ThrowTypeError(ctx, "Invalid port number");
    }
    
    // Connect in IO thread
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([data, port, host]() -> int {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return -1;
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return -1;
        }
        
        // Get local address
        struct sockaddr_in localAddr;
        socklen_t localLen = sizeof(localAddr);
        getsockname(sock, (struct sockaddr*)&localAddr, &localLen);
        data->localAddress = inet_ntoa(localAddr.sin_addr);
        data->localPort = ntohs(localAddr.sin_port);
        
        // Get remote address
        struct sockaddr_in remoteAddr;
        socklen_t remoteLen = sizeof(remoteAddr);
        getpeername(sock, (struct sockaddr*)&remoteAddr, &remoteLen);
        data->remoteAddress = inet_ntoa(remoteAddr.sin_addr);
        data->remotePort = ntohs(remoteAddr.sin_port);
        
        return sock;
    });
    
    int sock = future.get();
    if (sock < 0) {
        return JS_ThrowTypeError(ctx, "Connection failed");
    }
    
    data->socketFd = sock;
    data->connected = true;
    
    // Emit connect event
    if (!JS_IsUndefined(data->eventEmitter)) {
        EventLoop::getInstance().enqueueCallback([ctx, data]() {
            JSValue emit = JS_GetPropertyStr(ctx, data->eventEmitter, "emit");
            if (JS_IsFunction(ctx, emit)) {
                JSValue connectEvent = JS_NewString(ctx, "connect");
                JSValueConst args[] = {connectEvent};
                JS_Call(ctx, emit, data->eventEmitter, 1, const_cast<JSValue*>(args));
                JS_FreeValue(ctx, connectEvent);
            }
            JS_FreeValue(ctx, emit);
        });
    }
    
    // Start read thread
    data->readThread = std::thread([data, ctx]() {
        char buffer[4096];
        while (data->connected && !data->destroyed) {
            ssize_t n = recv(data->socketFd, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                data->connected = false;
                break;
            }
            
            // Emit data event
            if (!JS_IsUndefined(data->eventEmitter)) {
                JSValue dataEvent = JS_NewString(ctx, "data");
                JSValue bufferObj = JS_NewArrayBufferCopy(ctx, (uint8_t*)buffer, n);
                
                EventLoop::getInstance().enqueueCallback([ctx, data, dataEvent, bufferObj]() {
                    JSValue emit = JS_GetPropertyStr(ctx, data->eventEmitter, "emit");
                    if (JS_IsFunction(ctx, emit)) {
                        JSValueConst args[] = {dataEvent, bufferObj};
                        JS_Call(ctx, emit, data->eventEmitter, 2, const_cast<JSValue*>(args));
                    }
                    JS_FreeValue(ctx, emit);
                    JS_FreeValue(ctx, dataEvent);
                    JS_FreeValue(ctx, bufferObj);
                });
            }
        }
        
        // Emit end event
        if (!JS_IsUndefined(data->eventEmitter)) {
            EventLoop::getInstance().enqueueCallback([ctx, data]() {
                JSValue emit = JS_GetPropertyStr(ctx, data->eventEmitter, "emit");
                if (JS_IsFunction(ctx, emit)) {
                    JSValue endEvent = JS_NewString(ctx, "end");
                    JSValueConst args[] = {endEvent};
                    JS_Call(ctx, emit, data->eventEmitter, 1, const_cast<JSValue*>(args));
                    JS_FreeValue(ctx, endEvent);
                }
                JS_FreeValue(ctx, emit);
            });
        }
    });
    
    return JS_UNDEFINED;
}

JSValue NetModule::socketWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetSocketData* data = static_cast<NetSocketData*>(JS_GetOpaque(this_val, net_socket_class_id));
    if (!data || !data->connected) {
        return JS_ThrowTypeError(ctx, "Socket not connected");
    }
    
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "write requires data");
    }
    
    std::vector<uint8_t> bytes = getDataFromJSValue(ctx, argv[0]);
    if (bytes.empty()) {
        return JS_NewBool(ctx, false);
    }
    
    // Write in IO thread
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([data, bytes]() -> ssize_t {
        return send(data->socketFd, bytes.data(), bytes.size(), 0);
    });
    
    ssize_t sent = future.get();
    return JS_NewBool(ctx, sent > 0);
}

JSValue NetModule::socketEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetSocketData* data = static_cast<NetSocketData*>(JS_GetOpaque(this_val, net_socket_class_id));
    if (!data) {
        return JS_UNDEFINED;
    }
    
    // Write final data if provided
    if (argc > 0) {
        socketWrite(ctx, this_val, argc, argv);
    }
    
    // Shutdown write side
    if (data->connected && data->socketFd >= 0) {
        shutdown(data->socketFd, SHUT_WR);
    }
    
    return JS_UNDEFINED;
}

JSValue NetModule::socketDestroy(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetSocketData* data = static_cast<NetSocketData*>(JS_GetOpaque(this_val, net_socket_class_id));
    if (data) {
        data->destroy();
    }
    return JS_UNDEFINED;
}

JSValue NetModule::socketAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    NetSocketData* data = static_cast<NetSocketData*>(JS_GetOpaque(this_val, net_socket_class_id));
    if (!data || !data->connected) {
        return JS_NULL;
    }
    
    JSValue addr = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, addr, "port", JS_NewInt32(ctx, data->localPort));
    JS_SetPropertyStr(ctx, addr, "family", JS_NewString(ctx, "IPv4"));
    JS_SetPropertyStr(ctx, addr, "address", JS_NewString(ctx, data->localAddress.c_str()));
    
    return addr;
}

void NetModule::SocketFinalizer(JSRuntime* rt, JSValue val) {
    NetSocketData* data = static_cast<NetSocketData*>(JS_GetOpaque(val, net_socket_class_id));
    if (data) {
        delete data;
    }
}

std::vector<uint8_t> NetModule::getDataFromJSValue(JSContext* ctx, JSValueConst val, const char* encoding) {
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
                // Extract buffer data
                JSValue length = JS_GetPropertyStr(ctx, val, "length");
                uint32_t len;
                if (JS_ToUint32(ctx, &len, length) >= 0) {
                    result.resize(len);
                    // For now, convert to string and back (simplified)
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
