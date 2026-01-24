#ifndef PROTOJS_LOGGER_H
#define PROTOJS_LOGGER_H

#include <string>
#include <map>
#include <ostream>
#include <functional>
#include <mutex>

namespace protojs {

/**
 * @brief Logger provides structured logging for protoJS
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
     */
    static void debug(const std::string& message, const std::map<std::string, std::string>& context = {});
    
    /**
     * @brief Log an info message
     */
    static void info(const std::string& message, const std::map<std::string, std::string>& context = {});
    
    /**
     * @brief Log a warning message
     */
    static void warn(const std::string& message, const std::map<std::string, std::string>& context = {});
    
    /**
     * @brief Log an error message
     */
    static void error(const std::string& message, const std::map<std::string, std::string>& context = {});
    
    /**
     * @brief Set log level
     */
    static void setLevel(Level level);
    
    /**
     * @brief Set output stream
     */
    static void setOutput(std::ostream* output);
    
    /**
     * @brief Set formatter function
     */
    static void setFormatter(std::function<std::string(Level, const std::string&, const std::map<std::string, std::string>&)> formatter);
    
    /**
     * @brief Push context key-value pair
     */
    static void pushContext(const std::string& key, const std::string& value);
    
    /**
     * @brief Pop context key
     */
    static void popContext(const std::string& key);

private:
    static void log(Level level, const std::string& message, const std::map<std::string, std::string>& context);
    static std::string formatJSON(Level level, const std::string& message, const std::map<std::string, std::string>& context);
    static std::string formatText(Level level, const std::string& message, const std::map<std::string, std::string>& context);
    static std::string levelToString(Level level);
    
    static Level currentLevel;
    static std::ostream* outputStream;
    static std::function<std::string(Level, const std::string&, const std::map<std::string, std::string>&)> formatterFunc;
    static std::map<std::string, std::string> contextMap;
    static std::mutex logMutex;
};

} // namespace protojs

#endif // PROTOJS_LOGGER_H
