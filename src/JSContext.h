#ifndef PROTOJS_JSCONTEXT_H
#define PROTOJS_JSCONTEXT_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include <string>

namespace protojs {

class JSContextWrapper {
public:
    JSContextWrapper();
    ~JSContextWrapper();

    /**
     * @brief Evaluates JavaScript code.
     */
    JSValue eval(const std::string& code, const std::string& filename = "eval");

    /**
     * @brief Returns the QuickJS context.
     */
    JSContext* getJSContext() { return ctx; }

    /**
     * @brief Returns the protoCore context.
     */
    proto::ProtoContext* getProtoContext() { return pContext; }

    /**
     * @brief Returns the protoCore space.
     */
    proto::ProtoSpace* getProtoSpace() { return &pSpace; }

private:
    JSRuntime* rt;
    JSContext* ctx;
    proto::ProtoSpace pSpace;
    proto::ProtoContext* pContext;
};

} // namespace protojs

#endif // PROTOJS_JSCONTEXT_H
