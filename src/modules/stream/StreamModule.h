#ifndef PROTOJS_STREAMMODULE_H
#define PROTOJS_STREAMMODULE_H
#include "quickjs.h"
namespace protojs {
class StreamModule {
public:
    static void init(JSContext* ctx);
};
} // namespace protojs
#endif
