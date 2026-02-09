# Native Addon Modules (C++ Shared Libraries)

**Last Updated:** January 24, 2026

## Overview

protoJS supports loading **native addons** (shared libraries written in C++) via the same `require()` API used for JavaScript modules. There is no syntax change: `require('./my_module')` loads either a JavaScript file or a native addon depending on **resolution order**.

## Resolution Order

**Built-in modules:** Core modules (e.g. `fs`, `path`, `stream`, `crypto`, `buffer`, `net`, `http`, `events`, `util`, `url`, `dgram`, `cluster`, `worker_threads`, `child_process`, `dns`, `protoCore`) are resolved first by name. For example, `require('fs')` returns the same object as the global `fs`.

**File-based resolution:** When you `require('./mi_modulo')` or a bare specifier that is not a built-in, the loader looks for a module in this order:

1. **Native addon:** `mi_modulo.node`, then `mi_modulo.so` (or `.dll` / `.dylib` on Windows/macOS), then `mi_modulo.protojs`
2. **JavaScript:** `mi_modulo.js`, then `mi_modulo.mjs`
3. **Directory:** `mi_modulo/index.node`, `mi_modulo/index.so`, `mi_modulo/index.protojs`, `mi_modulo/index.js`, `mi_modulo/index.mjs`

The first file that exists is used. So if both `mi_modulo.so` and `mi_modulo.js` exist, the native addon is loaded.

## Writing a Native Addon

### ABI (Application Binary Interface)

Native addons must:

1. Export the symbol **`protojs_native_module_info`** (type `protojs::ProtoJSNativeModuleInfo`).
2. Implement an **init** function with signature:
   - `int init(JSContext* ctx, proto::ProtoContext* pContext, JSValue moduleObject);`
   - Return `0` on success, non-zero on error.

The **moduleObject** has CommonJS shape: `{ id, filename, exports, loaded, children, parent }`. The loader creates it with an empty `exports` object. Your init must register all exported values on **moduleObject.exports** (e.g. via `JS_SetPropertyStr(ctx, exports, "key", value)`).

### Minimal Example (C++)

```cpp
#include "native/NativeModuleABI.h"
#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

static JSValue sum_impl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    int32_t a = 0, b = 0;
    JS_ToInt32(ctx, &a, argv[0]);
    JS_ToInt32(ctx, &b, argv[1]);
    return JS_NewInt32(ctx, a + b);
}

static int init_impl(JSContext* ctx, proto::ProtoContext* pContext, JSValue moduleObject) {
    (void)pContext;
    JSValue exports = JS_GetPropertyStr(ctx, moduleObject, "exports");
    if (JS_IsException(exports)) return -1;
    JS_SetPropertyStr(ctx, exports, "version", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, exports, "sum", JS_NewCFunction(ctx, sum_impl, "sum", 2));
    return 0;
}

extern "C" {

ProtoJSNativeModuleInfo protojs_native_module_info(
    PROTOJS_ABI_VERSION,
    "my_addon",
    "1.0.0",
    init_impl,
    nullptr
);

} // extern "C"
} // namespace protojs
```

### Using from JavaScript

```javascript
const m = require('./my_addon');
console.log(m.version);  // 1
console.log(m.sum(2, 3)); // 5
```

### Build Requirements

- **Headers:** Same QuickJS and protoCore headers as the protoJS build (include paths: protoJS `src/`, `deps/quickjs`, protoCore and its `headers/`).
- **Linking:** Build as a **shared library** (e.g. `libmy_addon.so`). Do **not** link QuickJS or protoCore into the addon; symbols are resolved at load time from the protoJS executable.
- **protoJS executable:** Must be built with **`-rdynamic`** (or equivalent) so that its symbols are exported to `dlopen`-loaded addons. The project CMake already adds `target_link_options(protojs PRIVATE -rdynamic)`.

### File Extensions

- **`.node`** – Node.js-style native addon (same resolution priority as other native extensions).
- **`.so`** (Linux), **`.dll`** (Windows), **`.dylib`** (macOS) – platform shared library.
- **`.protojs`** – protoJS native addon extension.

## Transparency

Users write the same `require()` call regardless of whether the module is JavaScript or native. This allows:

- Replacing a JS module with a native one (or vice versa) without changing call sites.
- Heavy workloads (e.g. image processing, tensors) in addons without blocking the event loop, using protoCore’s GIL-free design.

## Reference

- **ABI:** `src/native/NativeModuleABI.h`
- **Loader:** `src/native/DynamicLibraryLoader.cpp`, `src/modules/CommonJSLoader.cpp`
- **Resolution:** `src/modules/ModuleResolver.cpp` (native-first extension order)
- **Test addons:** `tests/native_addons/simple/`, `tests/native_addons/fixture/`
