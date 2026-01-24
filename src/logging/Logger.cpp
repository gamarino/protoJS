#include "Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace protojs {

// Static member definitions
const proto::ProtoSparseList* Logger::levelStorage = nullptr;
std::ostream* Logger::outputStream = &std::cout;
std::mutex Logger::logMutex;

void Logger::debug(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    log(pContext, Level::DEBUG, message, context);
}

void Logger::info(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    log(pContext, Level::INFO, message, context);
}

void Logger::warn(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    log(pContext, Level::WARN, message, context);
}

void Logger::error(proto::ProtoContext* pContext, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    log(pContext, Level::ERROR, message, context);
}

void Logger::setLevel(proto::ProtoContext* pContext, Level level) {
    std::lock_guard<std::mutex> lock(logMutex);
    const proto::ProtoSparseList* storage = getLevelStorage(pContext);
    const proto::ProtoString* levelKey = pContext->fromUTF8String("level")->asString(pContext);
    unsigned long levelKeyHash = levelKey->getHash(pContext);
    storage = storage->setAt(pContext, levelKeyHash, pContext->fromInteger(static_cast<long long>(level)));
    setLevelStorage(pContext, storage);
}

void Logger::setOutput(std::ostream* output) {
    std::lock_guard<std::mutex> lock(logMutex);
    outputStream = output ? output : &std::cout;
}

const proto::ProtoObject* Logger::getLevel(proto::ProtoContext* pContext) {
    std::lock_guard<std::mutex> lock(logMutex);
    const proto::ProtoSparseList* storage = getLevelStorage(pContext);
    const proto::ProtoString* levelKey = pContext->fromUTF8String("level")->asString(pContext);
    unsigned long levelKeyHash = levelKey->getHash(pContext);
    
    if (storage->has(pContext, levelKeyHash)) {
        return storage->getAt(pContext, levelKeyHash);
    }
    
    // Default to INFO
    return pContext->fromInteger(static_cast<long long>(Level::INFO));
}

void Logger::log(proto::ProtoContext* pContext, Level level, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    // Check if we should log at this level
    const proto::ProtoObject* currentLevelObj = getLevel(pContext);
    long long currentLevel = currentLevelObj->asLong(pContext);
    
    if (static_cast<Level>(currentLevel) > level) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(logMutex);
    
    // Format log entry using protoCore
    const proto::ProtoString* formatted = formatText(pContext, level, message, context);
    
    // Convert ProtoString to C string for output (necessary for std::ostream)
    // This is the only conversion needed for I/O
    const proto::ProtoList* charList = formatted->asList(pContext);
    std::string outputStr;
    outputStr.reserve(formatted->getSize(pContext) * 4);
    
    unsigned long size = charList->getSize(pContext);
    for (unsigned long i = 0; i < size; i++) {
        const proto::ProtoObject* charObj = charList->getAt(pContext, i);
        unsigned int unicodeChar = charObj->asLong(pContext);
        
        // Convert Unicode to UTF-8
        if (unicodeChar < 0x80) {
            outputStr += static_cast<char>(unicodeChar);
        } else if (unicodeChar < 0x800) {
            outputStr += static_cast<char>(0xC0 | (unicodeChar >> 6));
            outputStr += static_cast<char>(0x80 | (unicodeChar & 0x3F));
        } else if (unicodeChar < 0x10000) {
            outputStr += static_cast<char>(0xE0 | (unicodeChar >> 12));
            outputStr += static_cast<char>(0x80 | ((unicodeChar >> 6) & 0x3F));
            outputStr += static_cast<char>(0x80 | (unicodeChar & 0x3F));
        } else {
            outputStr += static_cast<char>(0xF0 | (unicodeChar >> 18));
            outputStr += static_cast<char>(0x80 | ((unicodeChar >> 12) & 0x3F));
            outputStr += static_cast<char>(0x80 | ((unicodeChar >> 6) & 0x3F));
            outputStr += static_cast<char>(0x80 | (unicodeChar & 0x3F));
        }
    }
    
    // Write to output (std::ostream is external, necessary for I/O)
    *outputStream << outputStr << std::endl;
}

const proto::ProtoString* Logger::formatJSON(proto::ProtoContext* pContext, Level level, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    // Build JSON string using protoCore string operations
    const proto::ProtoString* levelStr = levelToString(pContext, level);
    
    // Get timestamp
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    const proto::ProtoString* timestamp = pContext->fromUTF8String(timeBuf)->asString(pContext);
    
    // Build JSON: {"timestamp":"...","level":"...","message":"..."}
    const proto::ProtoString* json = pContext->fromUTF8String("{")->asString(pContext);
    json = json->appendLast(pContext, pContext->fromUTF8String("\"timestamp\":\"")->asString(pContext));
    json = json->appendLast(pContext, timestamp);
    json = json->appendLast(pContext, pContext->fromUTF8String("\",\"level\":\"")->asString(pContext));
    json = json->appendLast(pContext, levelStr);
    json = json->appendLast(pContext, pContext->fromUTF8String("\",\"message\":\"")->asString(pContext));
    json = json->appendLast(pContext, message);
    json = json->appendLast(pContext, pContext->fromUTF8String("\"}")->asString(pContext));
    
    return json;
}

const proto::ProtoString* Logger::formatText(proto::ProtoContext* pContext, Level level, const proto::ProtoString* message, const proto::ProtoSparseList* context) {
    // Build text format: [timestamp] LEVEL message context...
    const proto::ProtoString* levelStr = levelToString(pContext, level);
    
    // Get timestamp
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm);
    const proto::ProtoString* timestamp = pContext->fromUTF8String(timeBuf)->asString(pContext);
    
    // Build: [timestamp] LEVEL message
    const proto::ProtoString* formatted = pContext->fromUTF8String("[")->asString(pContext);
    formatted = formatted->appendLast(pContext, timestamp);
    formatted = formatted->appendLast(pContext, pContext->fromUTF8String("] ")->asString(pContext));
    formatted = formatted->appendLast(pContext, levelStr);
    formatted = formatted->appendLast(pContext, pContext->fromUTF8String(" ")->asString(pContext));
    formatted = formatted->appendLast(pContext, message);
    
    // Add context if provided
    if (context && context->getSize(pContext) > 0) {
        formatted = formatted->appendLast(pContext, pContext->fromUTF8String(" ")->asString(pContext));
        // Iterate over context and append key=value pairs
        const proto::ProtoSparseListIterator* iter = context->getIterator(pContext);
        while (iter && iter->hasNext(pContext)) {
            unsigned long key = iter->nextKey(pContext);
            const proto::ProtoObject* value = iter->nextValue(pContext);
            // Format as key=value (simplified - would need key lookup)
            // For now, just append value
            if (value->isString(pContext)) {
                formatted = formatted->appendLast(pContext, value->asString(pContext));
            }
            iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
        }
    }
    
    return formatted;
}

const proto::ProtoString* Logger::levelToString(proto::ProtoContext* pContext, Level level) {
    switch (level) {
        case Level::DEBUG: return pContext->fromUTF8String("DEBUG")->asString(pContext);
        case Level::INFO: return pContext->fromUTF8String("INFO")->asString(pContext);
        case Level::WARN: return pContext->fromUTF8String("WARN")->asString(pContext);
        case Level::ERROR: return pContext->fromUTF8String("ERROR")->asString(pContext);
        default: return pContext->fromUTF8String("UNKNOWN")->asString(pContext);
    }
}

const proto::ProtoSparseList* Logger::getLevelStorage(proto::ProtoContext* pContext) {
    if (!levelStorage) {
        levelStorage = pContext->newSparseList();
    }
    return levelStorage;
}

void Logger::setLevelStorage(proto::ProtoContext* pContext, const proto::ProtoSparseList* storage) {
    levelStorage = storage;
}

} // namespace protojs
