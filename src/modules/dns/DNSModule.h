#ifndef PROTOJS_DNSMODULE_H
#define PROTOJS_DNSMODULE_H

#include "quickjs.h"
#include <string>

namespace protojs {

class DNSModule {
public:
    static void init(JSContext* ctx);

private:
    // DNS lookup methods
    static JSValue lookup(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue resolve4(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue resolve6(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue reverse(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue lookupService(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper functions
    static void lookupAsync(JSContext* ctx, const std::string& hostname, int family, JSValue callback);
};

} // namespace protojs

#endif // PROTOJS_DNSMODULE_H
