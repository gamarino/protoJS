#include "FSModule.h"
#include "../IOModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../JSContext.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../Deferred.h"
#include "../../JSContext.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>

namespace protojs {
namespace fs = std::filesystem;

void FSModule::init(JSContext* ctx) {
    JSValue fsModule = JS_NewObject(ctx);
    JSValue promises = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, promises, "readFile", JS_NewCFunction(ctx, promisesReadFile, "readFile", 1));
    JS_SetPropertyStr(ctx, promises, "writeFile", JS_NewCFunction(ctx, promisesWriteFile, "writeFile", 2));
    JS_SetPropertyStr(ctx, promises, "readdir", JS_NewCFunction(ctx, promisesReaddir, "readdir", 1));
    JS_SetPropertyStr(ctx, promises, "mkdir", JS_NewCFunction(ctx, promisesMkdir, "mkdir", 1));
    JS_SetPropertyStr(ctx, promises, "stat", JS_NewCFunction(ctx, promisesStat, "stat", 1));
    
    JS_SetPropertyStr(ctx, fsModule, "promises", promises);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "fs", fsModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue FSModule::promisesReadFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readFile expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filePath);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    EventLoop::getInstance().enqueueCallback([future, resolve, ctx]() {
        try {
            std::string content = future.get();
            JSValue contentVal = JS_NewString(ctx, content.c_str());
            JSValue args[] = { contentVal };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, contentVal);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesWriteFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFile expects a file path and content");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    const char* contentStr = JS_ToCString(ctx, argv[1]);
    if (!pathStr || !contentStr) {
        if (pathStr) JS_FreeCString(ctx, pathStr);
        if (contentStr) JS_FreeCString(ctx, contentStr);
        return JS_EXCEPTION;
    }
    
    std::string filePath(pathStr);
    std::string fileContent(contentStr);
    JS_FreeCString(ctx, pathStr);
    JS_FreeCString(ctx, contentStr);
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath, fileContent]() {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + filePath);
        }
        file << fileContent;
        file.close();
        return file.good();
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    EventLoop::getInstance().enqueueCallback([future, resolve, ctx]() {
        try {
            bool success = future.get();
            JSValue resultVal = JS_NewBool(ctx, success);
            JSValue args[] = { resultVal };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, resultVal);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesReaddir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readdir expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string dirPath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    // Create Deferred for async operation
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([dirPath]() {
        std::vector<std::string> entries;
        try {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                entries.push_back(entry.path().filename().string());
            }
        } catch (const std::exception& e) {
            throw;
        }
        return entries;
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    EventLoop::getInstance().enqueueCallback([future, resolve, ctx]() {
        try {
            auto entries = future.get();
            JSValue arr = JS_NewArray(ctx);
            for (size_t i = 0; i < entries.size(); i++) {
                JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, entries[i].c_str()));
            }
            JSValue args[] = { arr };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, arr);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesMkdir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "mkdir expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string dirPath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([dirPath]() {
        try {
            fs::create_directories(dirPath);
            return true;
        } catch (...) {
            return false;
        }
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    EventLoop::getInstance().enqueueCallback([future, resolve, ctx]() {
        bool success = future.get();
        JSValue resultVal = JS_NewBool(ctx, success);
        JSValue args[] = { resultVal };
        JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, resultVal);
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesStat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "stat expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        struct stat st;
        if (stat(filePath.c_str(), &st) != 0) {
            throw std::runtime_error("Cannot stat file");
        }
        return st;
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    EventLoop::getInstance().enqueueCallback([future, resolve, ctx]() {
        try {
            struct stat st = future.get();
            JSValue statsObj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, statsObj, "size", JS_NewInt64(ctx, st.st_size));
            JS_SetPropertyStr(ctx, statsObj, "isFile", JS_NewBool(ctx, S_ISREG(st.st_mode)));
            JS_SetPropertyStr(ctx, statsObj, "isDirectory", JS_NewBool(ctx, S_ISDIR(st.st_mode)));
            JS_SetPropertyStr(ctx, statsObj, "mtime", JS_NewInt64(ctx, st.st_mtime * 1000)); // Convert to milliseconds
            JSValue args[] = { statsObj };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, statsObj);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

} // namespace protojs
