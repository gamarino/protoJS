#ifndef PROTOJS_ERRORHANDLER_H
#define PROTOJS_ERRORHANDLER_H

#include "quickjs.h"
#include <string>
#include <functional>

namespace protojs {

/**
 * @brief Error types enum
 */
enum class ErrorType {
    Error,              // Base error
    SystemError,        // System errors (errno-based)
    TypeError,          // Type errors
    RangeError,         // Range errors
    ReferenceError,     // Reference errors
    SyntaxError,        // Syntax errors
    ModuleNotFoundError,// Module not found
    NetworkError        // Network errors
};

/**
 * @brief ErrorHandler provides standardized error handling across protoJS
 */
class ErrorHandler {
public:
    /**
     * @brief Create a standard error
     */
    static JSValue createError(JSContext* ctx, ErrorType type, const std::string& message, int code = 0);

    /**
     * @brief Create an error with a pre-generated stack trace
     */
    static JSValue createErrorWithStack(JSContext* ctx, ErrorType type, const std::string& message, const std::string& stack);

    /**
     * @brief Create a system error from errno
     */
    static JSValue createSystemError(JSContext* ctx, int errnoValue, const std::string& syscall, const std::string& path = "");

    /**
     * @brief Attach context information to an error
     */
    static void attachContext(JSContext* ctx, JSValue error, const std::string& file, int line, const std::string& function);

    /**
     * @brief Attach a stack trace to an error
     */
    static void attachStack(JSContext* ctx, JSValue error, const std::string& stack);

    /**
     * @brief Get stack trace from current execution context
     */
    static std::string getStackTrace(JSContext* ctx);

    /**
     * @brief Format a stack frame
     */
    static std::string formatStackFrame(const std::string& file, int line, const std::string& function);

    /**
     * @brief Get error type from error object
     */
    static ErrorType getErrorType(JSContext* ctx, JSValue error);

    /**
     * @brief Get error message from error object
     */
    static std::string getErrorMessage(JSContext* ctx, JSValue error);

    /**
     * @brief Get error code from error object
     */
    static int getErrorCode(JSContext* ctx, JSValue error);

    /**
     * @brief Get error stack trace from error object
     */
    static std::string getErrorStack(JSContext* ctx, JSValue error);

    /**
     * @brief Check if an error can be recovered
     */
    static bool canRecover(JSContext* ctx, JSValue error);

    /**
     * @brief Attempt to recover from an error
     */
    static JSValue recover(JSContext* ctx, JSValue error, const std::function<JSValue()>& recovery);

private:
    /**
     * @brief Get error type name as string
     */
    static std::string getErrorTypeName(ErrorType type);

    /**
     * @brief Get system error message from errno
     */
    static std::string getSystemErrorMessage(int errnoValue);
};

} // namespace protojs

#endif // PROTOJS_ERRORHANDLER_H
