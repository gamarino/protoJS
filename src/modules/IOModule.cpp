#include "IOModule.h"
#include <fstream>
#include <sstream>
#include <future>

namespace protojs {

void IOModule::init(JSContext* ctx) {
    JSValue ioModule = JS_NewObject(ctx);
    
    // Register I/O functions
    JS_SetPropertyStr(ctx, ioModule, "readFile", 
                     JS_NewCFunction(ctx, readFile, "readFile", 1));
    JS_SetPropertyStr(ctx, ioModule, "writeFile", 
                     JS_NewCFunction(ctx, writeFile, "writeFile", 2));
    
    // Add to global scope
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "io", ioModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue IOModule::readFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readFile expects a file path");
    }
    
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(path);
    JS_FreeCString(ctx, path);
    
    // Submit to I/O thread pool
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        return readFileSync(filePath);
    });
    
    // Wait for result (in a full implementation, this would be async with Promise)
    // For now, we'll block - in Phase 2 this should return a Promise
    try {
        std::string content = future.get();
        return JS_NewString(ctx, content.c_str());
    } catch (const std::exception& e) {
        std::string errorMsg = "readFile error: " + std::string(e.what());
        return JS_ThrowTypeError(ctx, errorMsg.c_str());
    }
}

JSValue IOModule::writeFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFile expects a file path and content");
    }
    
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    
    const char* content = JS_ToCString(ctx, argv[1]);
    if (!content) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    
    std::string filePath(path);
    std::string fileContent(content);
    
    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, content);
    
    // Submit to I/O thread pool
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath, fileContent]() {
        return writeFileSync(filePath, fileContent);
    });
    
    // Wait for result
    try {
        bool success = future.get();
        return JS_NewBool(ctx, success);
    } catch (const std::exception& e) {
        std::string errorMsg = "writeFile error: " + std::string(e.what());
        return JS_ThrowTypeError(ctx, errorMsg.c_str());
    }
}

std::string IOModule::readFileSync(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool IOModule::writeFileSync(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + path);
    }
    
    file << content;
    file.close();
    return file.good();
}

} // namespace protojs
