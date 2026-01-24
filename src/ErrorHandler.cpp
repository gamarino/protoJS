#include "ErrorHandler.h"
#include <cstring>
#include <sstream>
#include <cerrno>

namespace protojs {

JSValue ErrorHandler::createError(JSContext* ctx, ErrorType type, const std::string& message, int code) {
    JSValue error = JS_NewError(ctx);
    
    // Set error name
    std::string typeName = getErrorTypeName(type);
    JS_SetPropertyStr(ctx, error, "name", JS_NewString(ctx, typeName.c_str()));
    
    // Set error message
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message.c_str()));
    
    // Set error code if provided
    if (code != 0) {
        JS_SetPropertyStr(ctx, error, "code", JS_NewInt32(ctx, code));
    }
    
    // Set error type
    JS_SetPropertyStr(ctx, error, "type", JS_NewString(ctx, typeName.c_str()));
    
    // Generate and attach stack trace
    std::string stack = getStackTrace(ctx);
    if (!stack.empty()) {
        attachStack(ctx, error, stack);
    }
    
    return error;
}

JSValue ErrorHandler::createErrorWithStack(JSContext* ctx, ErrorType type, const std::string& message, const std::string& stack) {
    JSValue error = createError(ctx, type, message);
    attachStack(ctx, error, stack);
    return error;
}

JSValue ErrorHandler::createSystemError(JSContext* ctx, int errnoValue, const std::string& syscall, const std::string& path) {
    std::string message = syscall;
    if (!path.empty()) {
        message += " " + path;
    }
    message += ": " + getSystemErrorMessage(errnoValue);
    
    JSValue error = createError(ctx, ErrorType::SystemError, message, errnoValue);
    JS_SetPropertyStr(ctx, error, "syscall", JS_NewString(ctx, syscall.c_str()));
    JS_SetPropertyStr(ctx, error, "errno", JS_NewInt32(ctx, errnoValue));
    if (!path.empty()) {
        JS_SetPropertyStr(ctx, error, "path", JS_NewString(ctx, path.c_str()));
    }
    
    return error;
}

void ErrorHandler::attachContext(JSContext* ctx, JSValue error, const std::string& file, int line, const std::string& function) {
    if (!file.empty()) {
        JS_SetPropertyStr(ctx, error, "fileName", JS_NewString(ctx, file.c_str()));
    }
    JS_SetPropertyStr(ctx, error, "lineNumber", JS_NewInt32(ctx, line));
    if (!function.empty()) {
        JS_SetPropertyStr(ctx, error, "functionName", JS_NewString(ctx, function.c_str()));
    }
}

void ErrorHandler::attachStack(JSContext* ctx, JSValue error, const std::string& stack) {
    JS_SetPropertyStr(ctx, error, "stack", JS_NewString(ctx, stack.c_str()));
}

std::string ErrorHandler::getStackTrace(JSContext* ctx) {
    // QuickJS doesn't provide direct stack trace access
    // We'll create a basic stack trace format
    // In a full implementation, we'd integrate with QuickJS's error handling
    
    std::string stack;
    
    // Try to get Error.stack if available
    JSValue globalObj = JS_GetGlobalObject(ctx);
    JSValue errorObj = JS_GetPropertyStr(ctx, globalObj, "Error");
    JS_FreeValue(ctx, globalObj);
    
    if (!JS_IsUndefined(errorObj)) {
        // If Error constructor is available, we can use it
        // For now, return a basic stack trace
        stack = "    at (unknown location)\n";
    }
    
    JS_FreeValue(ctx, errorObj);
    
    return stack;
}

std::string ErrorHandler::formatStackFrame(const std::string& file, int line, const std::string& function) {
    std::ostringstream oss;
    oss << "    at " << (function.empty() ? "(anonymous)" : function) 
        << " (" << file << ":" << line << ")";
    return oss.str();
}

ErrorType ErrorHandler::getErrorType(JSContext* ctx, JSValue error) {
    JSValue nameVal = JS_GetPropertyStr(ctx, error, "name");
    if (JS_IsString(nameVal)) {
        const char* nameStr = JS_ToCString(ctx, nameVal);
        std::string name(nameStr);
        JS_FreeCString(ctx, nameStr);
        JS_FreeValue(ctx, nameVal);
        
        if (name == "SystemError") return ErrorType::SystemError;
        if (name == "TypeError") return ErrorType::TypeError;
        if (name == "RangeError") return ErrorType::RangeError;
        if (name == "ReferenceError") return ErrorType::ReferenceError;
        if (name == "SyntaxError") return ErrorType::SyntaxError;
        if (name == "ModuleNotFoundError") return ErrorType::ModuleNotFoundError;
        if (name == "NetworkError") return ErrorType::NetworkError;
    } else {
        JS_FreeValue(ctx, nameVal);
    }
    
    return ErrorType::Error;
}

std::string ErrorHandler::getErrorMessage(JSContext* ctx, JSValue error) {
    JSValue msgVal = JS_GetPropertyStr(ctx, error, "message");
    if (JS_IsString(msgVal)) {
        const char* msgStr = JS_ToCString(ctx, msgVal);
        std::string msg(msgStr);
        JS_FreeCString(ctx, msgStr);
        JS_FreeValue(ctx, msgVal);
        return msg;
    }
    JS_FreeValue(ctx, msgVal);
    return "";
}

int ErrorHandler::getErrorCode(JSContext* ctx, JSValue error) {
    JSValue codeVal = JS_GetPropertyStr(ctx, error, "code");
    if (JS_IsNumber(codeVal)) {
        int32_t code;
        JS_ToInt32(ctx, &code, codeVal);
        JS_FreeValue(ctx, codeVal);
        return code;
    }
    JS_FreeValue(ctx, codeVal);
    return 0;
}

std::string ErrorHandler::getErrorStack(JSContext* ctx, JSValue error) {
    JSValue stackVal = JS_GetPropertyStr(ctx, error, "stack");
    if (JS_IsString(stackVal)) {
        const char* stackStr = JS_ToCString(ctx, stackVal);
        std::string stack(stackStr);
        JS_FreeCString(ctx, stackStr);
        JS_FreeValue(ctx, stackVal);
        return stack;
    }
    JS_FreeValue(ctx, stackVal);
    return "";
}

bool ErrorHandler::canRecover(JSContext* ctx, JSValue error) {
    // Determine if error is recoverable based on type
    ErrorType type = getErrorType(ctx, error);
    
    // System errors with certain codes might be recoverable
    if (type == ErrorType::SystemError) {
        int code = getErrorCode(ctx, error);
        // EAGAIN, EINTR, etc. might be recoverable
        return (code == EAGAIN || code == EINTR);
    }
    
    // Most other errors are not easily recoverable
    return false;
}

JSValue ErrorHandler::recover(JSContext* ctx, JSValue error, const std::function<JSValue()>& recovery) {
    if (canRecover(ctx, error)) {
        try {
            return recovery();
        } catch (...) {
            // Recovery failed, return original error
            return JS_Throw(ctx, error);
        }
    }
    return JS_Throw(ctx, error);
}

std::string ErrorHandler::getErrorTypeName(ErrorType type) {
    switch (type) {
        case ErrorType::Error: return "Error";
        case ErrorType::SystemError: return "SystemError";
        case ErrorType::TypeError: return "TypeError";
        case ErrorType::RangeError: return "RangeError";
        case ErrorType::ReferenceError: return "ReferenceError";
        case ErrorType::SyntaxError: return "SyntaxError";
        case ErrorType::ModuleNotFoundError: return "ModuleNotFoundError";
        case ErrorType::NetworkError: return "NetworkError";
        default: return "Error";
    }
}

std::string ErrorHandler::getSystemErrorMessage(int errnoValue) {
    // Get system error message
    char buffer[256];
    const char* msg = strerror_r(errnoValue, buffer, sizeof(buffer));
    return std::string(msg ? msg : "Unknown error");
}

} // namespace protojs
