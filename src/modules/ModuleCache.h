#ifndef PROTOJS_MODULECACHE_H
#define PROTOJS_MODULECACHE_H

#include "ModuleResolver.h"
#include "quickjs.h"
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>

namespace protojs {

struct CachedModule {
    ModuleType type;
    JSValue moduleObject; // Module namespace (ESM) or module.exports (CJS)
    std::string filePath;
    std::chrono::system_clock::time_point loadTime;
    std::vector<std::string> dependencies;
    std::string resolvedSpecifier; // For ESM cache key
    
    CachedModule() : type(ModuleType::CommonJS), moduleObject(JS_UNDEFINED) {}
    ~CachedModule() {
        // JSValue will be freed by caller
    }
};

/**
 * @brief Module cache for loaded modules (ESM and CommonJS).
 * 
 * Prevents duplicate loading and supports cache invalidation.
 */
class ModuleCache {
public:
    /**
     * @brief Get a cached module.
     * @param key Cache key (resolved specifier for ESM, file path for CJS)
     * @return Cached module or nullptr if not found
     */
    static CachedModule* get(const std::string& key);
    
    /**
     * @brief Store a module in cache.
     * @param key Cache key
     * @param module Module to cache (ownership transferred)
     */
    static void put(const std::string& key, std::unique_ptr<CachedModule> module);
    
    /**
     * @brief Invalidate a cached module.
     * @param key Cache key
     */
    static void invalidate(const std::string& key);
    
    /**
     * @brief Clear all cached modules.
     */
    static void clear();
    
    /**
     * @brief Check if a module is cached.
     */
    static bool has(const std::string& key);
    
    /**
     * @brief Get cache size.
     */
    static size_t size();

private:
    static std::map<std::string, std::unique_ptr<CachedModule>> cache;
    static std::mutex cacheMutex;
};

} // namespace protojs

#endif // PROTOJS_MODULECACHE_H
