#include "HTTPModule.h"
#include "../events/EventsModule.h"
#include "../stream/StreamModule.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <sstream>
#include <map>
#include <string>

namespace protojs {

static JSClassID http_server_class_id;
static JSClassID http_request_class_id;
static JSClassID http_response_class_id;
static JSClassID http_incoming_message_class_id;

struct HTTPServerData {
    int socketFd;
    int port;
    bool listening;
    JSValue requestListener;
    JSRuntime* rt;
    std::thread serverThread;
    
    HTTPServerData(JSRuntime* r) : socketFd(-1), port(0), listening(false), requestListener(JS_UNDEFINED), rt(r) {}
    ~HTTPServerData() {
        if (socketFd >= 0) {
            close(socketFd);
        }
        if (!JS_IsUndefined(requestListener)) {
            JS_FreeValueRT(rt, requestListener);
        }
        if (serverThread.joinable()) {
            listening = false;
            serverThread.join();
        }
    }
};

struct HTTPRequestData {
    std::string method;
    std::string url;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
    JSRuntime* rt;
    JSValue eventEmitter;
    
    HTTPRequestData(JSRuntime* r) : rt(r), eventEmitter(JS_UNDEFINED) {}
    ~HTTPRequestData() {
        if (!JS_IsUndefined(eventEmitter)) {
            JS_FreeValueRT(rt, eventEmitter);
        }
    }
};

struct HTTPResponseData {
    int statusCode;
    std::map<std::string, std::string> headers;
    bool headersSent;
    std::string body;
    JSRuntime* rt;
    JSValue eventEmitter;
    int clientFd;
    
    HTTPResponseData(JSRuntime* r, int fd) : statusCode(200), headersSent(false), rt(r), eventEmitter(JS_UNDEFINED), clientFd(fd) {}
    ~HTTPResponseData() {
        if (!JS_IsUndefined(eventEmitter)) {
            JS_FreeValueRT(rt, eventEmitter);
        }
        if (clientFd >= 0) {
            close(clientFd);
        }
    }
};

void HTTPModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Server class
    JS_NewClassID(&http_server_class_id);
    JSClassDef serverClassDef = {
        "HTTPServer",
        ServerFinalizer
    };
    JS_NewClass(rt, http_server_class_id, &serverClassDef);
    
    JSValue serverProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, serverProto, "listen", JS_NewCFunction(ctx, serverListen, "listen", 1));
    JS_SetPropertyStr(ctx, serverProto, "close", JS_NewCFunction(ctx, serverClose, "close", 0));
    JS_SetClassProto(ctx, http_server_class_id, serverProto);
    
    // Register IncomingMessage class
    JS_NewClassID(&http_incoming_message_class_id);
    JSClassDef incomingClassDef = {
        "IncomingMessage",
        IncomingMessageFinalizer
    };
    JS_NewClass(rt, http_incoming_message_class_id, &incomingClassDef);
    
    JSValue incomingProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, incomingProto, "getHeader", JS_NewCFunction(ctx, incomingMessageGetHeader, "getHeader", 1));
    JS_SetClassProto(ctx, http_incoming_message_class_id, incomingProto);
    
    // Register ServerResponse class
    JS_NewClassID(&http_response_class_id);
    JSClassDef responseClassDef = {
        "ServerResponse",
        ResponseFinalizer
    };
    JS_NewClass(rt, http_response_class_id, &responseClassDef);
    
    JSValue responseProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, responseProto, "writeHead", JS_NewCFunction(ctx, responseWriteHead, "writeHead", 2));
    JS_SetPropertyStr(ctx, responseProto, "write", JS_NewCFunction(ctx, responseWrite, "write", 1));
    JS_SetPropertyStr(ctx, responseProto, "end", JS_NewCFunction(ctx, responseEnd, "end", 1));
    JS_SetClassProto(ctx, http_response_class_id, responseProto);
    
    // Register ClientRequest class
    JS_NewClassID(&http_request_class_id);
    JSClassDef requestClassDef = {
        "ClientRequest",
        RequestFinalizer
    };
    JS_NewClass(rt, http_request_class_id, &requestClassDef);
    
    JSValue requestProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, requestProto, "write", JS_NewCFunction(ctx, requestWrite, "write", 1));
    JS_SetPropertyStr(ctx, requestProto, "end", JS_NewCFunction(ctx, requestEnd, "end", 1));
    JS_SetClassProto(ctx, http_request_class_id, requestProto);
    
    // Create http module
    JSValue httpModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, httpModule, "createServer", JS_NewCFunction(ctx, createServer, "createServer", 1));
    JS_SetPropertyStr(ctx, httpModule, "request", JS_NewCFunction(ctx, request, "request", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "http", httpModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue HTTPModule::createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue server = JS_NewObjectClass(ctx, http_server_class_id);
    if (JS_IsException(server)) return server;
    
    HTTPServerData* data = new HTTPServerData(JS_GetRuntime(ctx));
    
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
    
    // Store request listener if provided
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        data->requestListener = JS_DupValue(ctx, argv[0]);
        JS_SetPropertyStr(ctx, server, "on", JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "on"));
    }
    
    JS_SetOpaque(server, data);
    return server;
}

void HTTPModule::ServerFinalizer(JSRuntime* rt, JSValue val) {
    HTTPServerData* data = static_cast<HTTPServerData*>(JS_GetOpaque(val, http_server_class_id));
    if (data) delete data;
}

JSValue HTTPModule::serverListen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "listen requires a port number");
    }
    
    int32_t port;
    if (JS_ToInt32(ctx, &port, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    
    HTTPServerData* data = static_cast<HTTPServerData*>(JS_GetOpaque(this_val, http_server_class_id));
    if (!data) {
        return JS_ThrowTypeError(ctx, "Invalid HTTP server");
    }
    
    data->port = port;
    
    // Create socket
    data->socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (data->socketFd < 0) {
        return JS_ThrowTypeError(ctx, "Failed to create socket");
    }
    
    int opt = 1;
    setsockopt(data->socketFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(data->socketFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(data->socketFd);
        data->socketFd = -1;
        return JS_ThrowTypeError(ctx, "Failed to bind to port");
    }
    
    if (listen(data->socketFd, 10) < 0) {
        close(data->socketFd);
        data->socketFd = -1;
        return JS_ThrowTypeError(ctx, "Failed to listen on socket");
    }
    
    data->listening = true;
    
    // Start server thread
    data->serverThread = std::thread([data, ctx]() {
        JSContext* workerCtx = JS_NewContext(JS_GetRuntime(ctx));
        if (!workerCtx) return;
        
        while (data->listening) {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(data->socketFd, (struct sockaddr*)&clientAddr, &clientLen);
            
            if (clientFd < 0) {
                if (data->listening) continue;
                break;
            }
            
            // Read request
            char buffer[4096];
            ssize_t bytesRead = read(clientFd, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string requestStr(buffer);
                
                // Parse request
                std::istringstream iss(requestStr);
                std::string method, url, version;
                iss >> method >> url >> version;
                
                // Find headers
                std::string line;
                std::map<std::string, std::string> headers;
                while (std::getline(iss, line) && line != "\r" && !line.empty()) {
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string key = line.substr(0, colon);
                        std::string value = line.substr(colon + 1);
                        // Trim whitespace
                        while (!value.empty() && (value[0] == ' ' || value[0] == '\r')) {
                            value.erase(0, 1);
                        }
                        headers[key] = value;
                    }
                }
                
                // Create request and response objects
                JSValue req = JS_NewObjectClass(workerCtx, http_incoming_message_class_id);
                HTTPRequestData* reqData = new HTTPRequestData(JS_GetRuntime(workerCtx));
                reqData->method = method;
                reqData->url = url;
                reqData->version = version;
                reqData->headers = headers;
                
                // Create EventEmitter for request
                JSValue eventEmitterCtor = JS_GetPropertyStr(workerCtx, JS_GetGlobalObject(workerCtx), "EventEmitter");
                if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(workerCtx, eventEmitterCtor)) {
                    JSValue emitter = JS_CallConstructor(workerCtx, eventEmitterCtor, 0, nullptr);
                    if (!JS_IsException(emitter)) {
                        reqData->eventEmitter = emitter;
                        JS_SetPropertyStr(workerCtx, req, "_events", emitter);
                    }
                    JS_FreeValue(workerCtx, emitter);
                }
                JS_FreeValue(workerCtx, eventEmitterCtor);
                
                JS_SetPropertyStr(workerCtx, req, "method", JS_NewString(workerCtx, method.c_str()));
                JS_SetPropertyStr(workerCtx, req, "url", JS_NewString(workerCtx, url.c_str()));
                JS_SetOpaque(req, reqData);
                
                JSValue res = JS_NewObjectClass(workerCtx, http_response_class_id);
                HTTPResponseData* resData = new HTTPResponseData(JS_GetRuntime(workerCtx), clientFd);
                
                // Create EventEmitter for response
                eventEmitterCtor = JS_GetPropertyStr(workerCtx, JS_GetGlobalObject(workerCtx), "EventEmitter");
                if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(workerCtx, eventEmitterCtor)) {
                    JSValue emitter = JS_CallConstructor(workerCtx, eventEmitterCtor, 0, nullptr);
                    if (!JS_IsException(emitter)) {
                        resData->eventEmitter = emitter;
                        JS_SetPropertyStr(workerCtx, res, "_events", emitter);
                    }
                    JS_FreeValue(workerCtx, emitter);
                }
                JS_FreeValue(workerCtx, eventEmitterCtor);
                
                JS_SetOpaque(res, resData);
                
                // Call request listener
                if (!JS_IsUndefined(data->requestListener) && JS_IsFunction(workerCtx, data->requestListener)) {
                    JSValue args[] = { req, res };
                    JSValue result = JS_Call(workerCtx, data->requestListener, JS_UNDEFINED, 2, args);
                    JS_FreeValue(workerCtx, result);
                }
                
                JS_FreeValue(workerCtx, req);
                JS_FreeValue(workerCtx, res);
            }
            
            close(clientFd);
        }
        
        JS_FreeContext(workerCtx);
    });
    
    // Call callback if provided
    if (argc > 1 && JS_IsFunction(ctx, argv[1])) {
        JSValue args[] = { JS_UNDEFINED };
        JS_Call(ctx, argv[1], JS_UNDEFINED, 0, args);
    }
    
    return JS_DupValue(ctx, this_val);
}

JSValue HTTPModule::serverClose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    HTTPServerData* data = static_cast<HTTPServerData*>(JS_GetOpaque(this_val, http_server_class_id));
    if (data) {
        data->listening = false;
        if (data->socketFd >= 0) {
            close(data->socketFd);
            data->socketFd = -1;
        }
        if (data->serverThread.joinable()) {
            data->serverThread.join();
        }
    }
    return JS_UNDEFINED;
}

JSValue HTTPModule::request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Basic HTTP client request (simplified for Phase 2)
    JSValue requestObj = JS_NewObjectClass(ctx, http_request_class_id);
    if (JS_IsException(requestObj)) return requestObj;
    
    // Create EventEmitter for request
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            JS_SetPropertyStr(ctx, requestObj, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    return requestObj;
}

void HTTPModule::RequestFinalizer(JSRuntime* rt, JSValue val) {
    // Request data cleanup if needed
}

JSValue HTTPModule::requestWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Write request body
    return JS_NewBool(ctx, true);
}

JSValue HTTPModule::requestEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // End request
    return JS_DupValue(ctx, this_val);
}

JSValue HTTPModule::responseWriteHead(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "writeHead requires statusCode");
    }
    
    int32_t statusCode;
    if (JS_ToInt32(ctx, &statusCode, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    
    HTTPResponseData* data = static_cast<HTTPResponseData*>(JS_GetOpaque(this_val, http_response_class_id));
    if (data && !data->headersSent) {
        data->statusCode = statusCode;
        
        if (argc > 1 && JS_IsObject(ctx, argv[1])) {
            // Parse headers object
            JSPropertyEnum* props;
            uint32_t propCount;
            if (JS_GetOwnPropertyNames(ctx, &props, &propCount, argv[1], JS_GPN_STRING_MASK) >= 0) {
                for (uint32_t i = 0; i < propCount; i++) {
                    JSValue key = JS_AtomToValue(ctx, props[i].atom);
                    JSValue value = JS_GetProperty(ctx, argv[1], props[i].atom);
                    const char* keyStr = JS_ToCString(ctx, key);
                    const char* valueStr = JS_ToCString(ctx, value);
                    if (keyStr && valueStr) {
                        data->headers[keyStr] = valueStr;
                    }
                    if (keyStr) JS_FreeCString(ctx, keyStr);
                    if (valueStr) JS_FreeCString(ctx, valueStr);
                    JS_FreeValue(ctx, key);
                    JS_FreeValue(ctx, value);
                    JS_FreeAtom(ctx, props[i].atom);
                }
                js_free(ctx, props);
            }
        }
    }
    
    return JS_DupValue(ctx, this_val);
}

JSValue HTTPModule::responseWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "write requires data");
    }
    
    HTTPResponseData* data = static_cast<HTTPResponseData*>(JS_GetOpaque(this_val, http_response_class_id));
    if (data) {
        const char* str = JS_ToCString(ctx, argv[0]);
        if (str) {
            data->body += std::string(str);
            JS_FreeCString(ctx, str);
        }
    }
    
    return JS_NewBool(ctx, true);
}

JSValue HTTPModule::responseEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    HTTPResponseData* data = static_cast<HTTPResponseData*>(JS_GetOpaque(this_val, http_response_class_id));
    if (data && data->clientFd >= 0) {
        // Write final chunk if provided
        if (argc > 0) {
            responseWrite(ctx, this_val, argc, argv);
        }
        
        // Send response
        std::ostringstream response;
        response << "HTTP/1.1 " << data->statusCode << " OK\r\n";
        
        if (data->headers.find("Content-Type") == data->headers.end()) {
            response << "Content-Type: text/plain\r\n";
        }
        
        for (const auto& [key, value] : data->headers) {
            response << key << ": " << value << "\r\n";
        }
        
        response << "Content-Length: " << data->body.length() << "\r\n";
        response << "\r\n";
        response << data->body;
        
        std::string responseStr = response.str();
        write(data->clientFd, responseStr.c_str(), responseStr.length());
        
        data->headersSent = true;
    }
    
    return JS_DupValue(ctx, this_val);
}

void HTTPModule::ResponseFinalizer(JSRuntime* rt, JSValue val) {
    HTTPResponseData* data = static_cast<HTTPResponseData*>(JS_GetOpaque(val, http_response_class_id));
    if (data) delete data;
}

JSValue HTTPModule::incomingMessageGetHeader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_UNDEFINED;
    }
    
    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    
    HTTPRequestData* data = static_cast<HTTPRequestData*>(JS_GetOpaque(this_val, http_incoming_message_class_id));
    if (data) {
        auto it = data->headers.find(name);
        if (it != data->headers.end()) {
            JSValue result = JS_NewString(ctx, it->second.c_str());
            JS_FreeCString(ctx, name);
            return result;
        }
    }
    
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

void HTTPModule::IncomingMessageFinalizer(JSRuntime* rt, JSValue val) {
    HTTPRequestData* data = static_cast<HTTPRequestData*>(JS_GetOpaque(val, http_incoming_message_class_id));
    if (data) delete data;
}

void HTTPModule::parseHeaders(const std::string& headerStr, std::map<std::string, std::string>& headers) {
    std::istringstream iss(headerStr);
    std::string line;
    while (std::getline(iss, line)) {
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && (value[0] == ' ' || value[0] == '\r')) {
                value.erase(0, 1);
            }
            headers[key] = value;
        }
    }
}

std::string HTTPModule::formatHeaders(const std::map<std::string, std::string>& headers) {
    std::ostringstream oss;
    for (const auto& [key, value] : headers) {
        oss << key << ": " << value << "\r\n";
    }
    return oss.str();
}

} // namespace protojs
