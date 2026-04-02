#ifndef PROTOJS_PROTOJSSTRINGCACHE_H
#define PROTOJS_PROTOJSSTRINGCACHE_H

#include "headers/protoCore.h"
#include <cstdint>

namespace protojs {

/**
 * Cache for interned ProtoString keys. All strings in protoCore are interned;
 * fromUTF8String returns a stable pointer per logical string, so we cache
 * the result (as ProtoString*) per context to avoid repeated lookups.
 * Cached pointers are valid for the lifetime of the ProtoContext's space.
 */
class ProtoJSStringCache {
public:
    /**
     * Return the ProtoString for a constant key (e.g. "length", "elements").
     * Result is cached per context; safe to store and reuse.
     */
    static const proto::ProtoString* getKey(proto::ProtoContext* ctx, const char* utf8);

    /**
     * Return the ProtoString for a numeric index as attribute name ("0", "1", ...).
     * Small indices (0..255) are cached; larger indices use getKey with stringified number.
     */
    static const proto::ProtoString* getIndexKey(proto::ProtoContext* ctx, uint32_t index);

    /**
     * Clear cache for a context (e.g. on context cleanup). Optional.
     */
    static void clearForContext(proto::ProtoContext* ctx);

    /**
     * Get the original string name from a hash (reverse lookup).
     * Only works for strings that were previously registered via getKey or getIndexKey.
     */
    static std::string getNameFromHash(unsigned long hash);
};

} // namespace protojs

#endif // PROTOJS_PROTOJSSTRINGCACHE_H
