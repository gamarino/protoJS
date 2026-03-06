/**
 * QuickJS array bridge stubs (legacy path removed).
 * quickjs.c still references protojs_array_mirror_after_set_*; they are no-ops
 * since all script execution uses the protoCore interpreter path.
 */

#include "quickjs.h"

extern "C" {

void protojs_array_mirror_after_set_index(JSContext* ctx,
                                          JSValueConst this_obj,
                                          uint32_t idx) {
    (void)ctx;
    (void)this_obj;
    (void)idx;
}

void protojs_array_mirror_after_set_length(JSContext* ctx,
                                           JSValueConst this_obj) {
    (void)ctx;
    (void)this_obj;
}

} /* extern "C" */
