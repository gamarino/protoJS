#include "Logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace protojs {

// Static member definitions
Logger::Level Logger::currentLevel = Logger::Level::INFO;
std::ostream* Logger::outputStream = &std::cout;
std::function<std::string(Level, const std::string&, const std::map<std::string, std::string>&)> Logger::formatterFunc = nullptr;
std::map<std::string, std::string> Logger::contextMap;
std::mutex Logger::logMutex;

void Logger::debug(const std::string& message, const std::map<std::string, std::string>& context) {
    log(Level::DEBUG, message, context);
}

void Logger::info(const std::string& message, const std::map<std::string, std::string>& context) {
    log(Level::INFO, message, context);
}

void Logger::warn(const std::string& message, const std::map<std::string, std::string>& context) {
    log(Level::WARN, message, context);
}

void Logger::error(const std::string& message, const std::map<std::string, std::string>& context) {
    log(Level::ERROR, message, context);
}

void Logger::setLevel(Level level) {
    std::lock_guard<std::mutex> lock(logMutex);
    currentLevel = level;
}

void Logger::setOutput(std::ostream* output) {
    std::lock_guard<std::mutex> lock(logMutex);
    outputStream = output ? output : &std::cout;
}

void Logger::setFormatter(std::function<std::string(Level, const std::string&, const std::map<std::string, std::string>&)> formatter) {
    std::lock_guard<std::mutex> lock(logMutex);
    formatterFunc = formatter;
}

void Logger::pushContext(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(logMutex);
    contextMap[key] = value;
}

void Logger::popContext(const std::string& key) {
    std::lock_guard<std::mutex> lock(logMutex);
    contextMap.erase(key);
}

void Logger::log(Level level, const std::string& message, const std::map<std::string, std::string>& context) {
    if (level < currentLevel) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(logMutex);
    
    // Merge context
    std::map<std::string, std::string> mergedContext = contextMap;
    mergedContext.insert(context.begin(), context.end());
    
    // Format log entry
    std::string formatted;
    if (formatterFunc) {
        formatted = formatterFunc(level, message, mergedContext);
    } else {
        formatted = formatText(level, message, mergedContext);
    }
    
    // Write to output
    *outputStream << formatted << std::endl;
}

std::string Logger::formatJSON(Level level, const std::string& message, const std::map<std::string, std::string>& context) {
    std::ostringstream oss;
    
    // Get timestamp
    auto now = std::time(nullptr);
    auto tm = *std::gmtime(&now);
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    
    oss << "{\n"
        << "  \"timestamp\": \"" << ts.str() << "\",\n"
        << "  \"level\": \"" << levelToString(level) << "\",\n"
        << "  \"message\": \"" << message << "\"";
    
    if (!context.empty()) {
        oss << ",\n  \"context\": {\n";
        bool first = true;
        for (const auto& [key, value] : context) {
            if (!first) oss << ",\n";
            oss << "    \"" << key << "\": \"" << value << "\"";
            first = false;
        }
        oss << "\n  }";
    }
    
    oss << "\n}";
    return oss.str();
}

std::string Logger::formatText(Level level, const std::string& message, const std::map<std::string, std::string>& context) {
    std::ostringstream oss;
    
    // Get timestamp
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] ";
    
    oss << levelToString(level) << " " << message;
    
    if (!context.empty()) {
        for (const auto& [key, value] : context) {
            oss << " " << key << "=" << value;
        }
    }
    
    return oss.str();
}

std::string Logger::levelToString(Level level) {
    switch (level) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO: return "INFO";
        case Level::WARN: return "WARN";
        case Level::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

} // namespace protojs
