#ifndef PROTOJS_LOGGER_H
#define PROTOJS_LOGGER_H

#include "headers/protoCore.h"
#include <mutex>
#include <ostream>

// Forward declaration
struct JSContext;

namespace protojs {

/**
 * @brief Logger provides structured logging for protoJS using only protoCore objects
 */
class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };
    
    /**
     * @brief Log a debug message
     * @param pContext ProtoContext for creating protoCore objects
     */
    static void debug(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context = nullptr);
    
    /**
     * @brief Log an info message
     */
    static void info(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context = nullptr);
    
    /**
     * @brief Log a warning message
     */
    static void warn(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context = nullptr);
    
    /**
     * @brief Log an error message
     */
    static void error(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context = nullptr);
    
    /**
     * @brief Set log level
     */
    static void setLevel(proto::ProtoContext* pContext, Level level);
    
    /**
     * @brief Set output stream (C++ stream for now, but could be wrapped)
     */
    static void setOutput(std::ostream* output);
    
    /**
     * @brief Get current log level as ProtoObject
     */
    static const proto::ProtoObject* getLevel(proto::ProtoContext* pContext);

private:
    static void log(proto::ProtoContext* pContext, Level level, const proto::ProtoString* message, const proto::ProtoSparseList* context);
    static const proto::ProtoString* formatJSON(proto::ProtoContext* pContext, Level level, const proto::ProtoString* message, const proto::ProtoSparseList* context);
    static const proto::ProtoString* formatText(proto::ProtoContext* pContext, Level level, const proto::ProtoString* message, const proto::ProtoSparseList* context);
    static const proto::ProtoString* levelToString(proto::ProtoContext* pContext, Level level);
    
    // Store level as ProtoObject (integer) in a global ProtoSparseList
    // Key: "level" as ProtoString hash
    // Value: Level as integer ProtoObject
    static const proto::ProtoSparseList* levelStorage;
    static std::ostream* outputStream;
    static std::mutex logMutex;
    
    /**
     * @brief Get or create level storage
     */
    static const proto::ProtoSparseList* getLevelStorage(proto::ProtoContext* pContext);
    
    /**
     * @brief Set level storage
     */
    static void setLevelStorage(proto::ProtoContext* pContext, const proto::ProtoSparseList* storage);
};

} // namespace protojs

#endif // PROTOJS_LOGGER_H
