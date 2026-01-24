#include "ModuleCache.h"

namespace protojs {

std::map<std::string, std::unique_ptr<CachedModule>> ModuleCache::cache;
std::mutex ModuleCache::cacheMutex;

CachedModule* ModuleCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ModuleCache::put(const std::string& key, std::unique_ptr<CachedModule> module) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache[key] = std::move(module);
}

void ModuleCache::invalidate(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache.erase(key);
}

void ModuleCache::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache.clear();
}

bool ModuleCache::has(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return cache.find(key) != cache.end();
}

size_t ModuleCache::size() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return cache.size();
}

} // namespace protojs
