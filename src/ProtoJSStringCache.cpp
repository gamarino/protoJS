#include "ProtoJSStringCache.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <array>
#include <sstream>

namespace protojs {

namespace {

std::mutex s_cacheMutex;
using KeyMap = std::unordered_map<std::string, const proto::ProtoString*>;
std::unordered_map<proto::ProtoContext*, KeyMap> s_contextKeys;

// Cache for small numeric index keys ("0" .. "255")
constexpr unsigned kMaxCachedIndex = 256;
using IndexCache = std::array<const proto::ProtoString*, kMaxCachedIndex>;
std::unordered_map<proto::ProtoContext*, IndexCache> s_indexKeys;

const proto::ProtoString* getKeyImpl(proto::ProtoContext* ctx, const char* utf8) {
    if (!ctx || !utf8) return nullptr;
    const proto::ProtoObject* obj = ctx->fromUTF8String(utf8);
    if (!obj) return nullptr;
    return obj->asString(ctx);
}

} // namespace

const proto::ProtoString* ProtoJSStringCache::getKey(proto::ProtoContext* ctx, const char* utf8) {
    if (!ctx || !utf8) return nullptr;
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    KeyMap& map = s_contextKeys[ctx];
    std::string key(utf8);
    auto it = map.find(key);
    if (it != map.end())
        return it->second;
    const proto::ProtoString* str = getKeyImpl(ctx, utf8);
    if (str)
        map[key] = str;
    return str;
}

const proto::ProtoString* ProtoJSStringCache::getIndexKey(proto::ProtoContext* ctx, uint32_t index) {
    if (!ctx) return nullptr;
    if (index < kMaxCachedIndex) {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        IndexCache& arr = s_indexKeys[ctx];
        if (arr[index])
            return arr[index];
        std::ostringstream oss;
        oss << index;
        std::string key = oss.str();
        const proto::ProtoString* str = getKeyImpl(ctx, key.c_str());
        if (str)
            arr[index] = str;
        return str;
    }
    std::ostringstream oss;
    oss << index;
    return getKey(ctx, oss.str().c_str());
}

void ProtoJSStringCache::clearForContext(proto::ProtoContext* ctx) {
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_contextKeys.erase(ctx);
    s_indexKeys.erase(ctx);
}

} // namespace protojs
