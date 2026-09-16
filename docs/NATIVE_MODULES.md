# Native Addon Modules (C++ Shared Libraries)

protoJS can load **native addons** — shared libraries written in C++ — through the same `require()` call used for JavaScript modules. Whether `require('./my_module')` loads JavaScript or a native addon depends only on which files exist.

## Resolution order

`require()` is implemented in `src/modules/CommonJSLoader.cpp`; file lookup is in `src/modules/ModuleResolver.cpp`.

**Bare specifiers** (not starting with `./`, `../` or `/`) are first matched against the built-in module names on the protoCore-native global, then offered to protoCore's module discovery; see [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md). If neither step succeeds, bare specifiers are searched in `node_modules` directories.

**File-based resolution.** For a specifier such as `require('./my_module')`, the loader tries, in order:

1. `my_module.node`, then `my_module` plus the platform library extension (`.so` on Linux, `.dylib` on macOS, `.dll` on Windows), then `my_module.protojs`
2. `my_module.js`, then `my_module.mjs`
3. For a directory: `my_module/index.node`, `my_module/index.<platform extension>`, `my_module/index.protojs`, `my_module/index.js`, `my_module/index.mjs`

The first existing file is used, so if both `my_module.so` and `my_module.js` exist, the native addon is loaded. Files ending in `.node`, `.so`, `.dll`, `.dylib` or `.protojs` are loaded as native addons.

## Writing a native addon

### ABI

The ABI is defined in `src/native/NativeModuleABI.h` (`PROTOJS_ABI_VERSION` is 1). An addon must:

1. Export the symbol **`protojs_native_module_info`**, of type `protojs::ProtoJSNativeModuleInfo` (`abiVersion`, `name`, `version`, `init`, `cleanup`).
2. Provide an **init** function with the signature
   `int init(JSContext* ctx, proto::ProtoContext* pContext, JSValue moduleObject);`
   returning `0` on success and non-zero on error. `cleanup` is optional (`nullptr`).

`moduleObject` has the CommonJS shape `{ id, filename, exports, loaded, children, parent }`, created with an empty `exports` object. The init function registers every exported value on `moduleObject.exports`, for example with `JS_SetPropertyStr(ctx, exports, "key", value)`. After init returns, `require` converts the exports to protoCore objects (`TypeBridge::fromJS`) and caches them by file path.

### Minimal example (C++)

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

### Using it from JavaScript

```javascript
const m = require('./my_addon');
console.log(m.version);
console.log(m.sum(2, 3));
```

### Build requirements

- **Headers:** the same include directories as the protoJS build: protoJS `src/`, `deps/quickjs`, and the protoCore source directory with its `headers/` subdirectory.
- **Linking:** build a shared library and do not link QuickJS or protoCore into it; their symbols are resolved at load time from the `protojs` executable, which is linked with `-rdynamic` (`CMakeLists.txt`).
- **File name:** the resolver looks for `<name>.so` (or `.node`, `.protojs`), not `lib<name>.so`. The test addons set `PREFIX ""` in CMake for this reason.

## Reference

- **ABI:** `src/native/NativeModuleABI.h`
- **Loader:** `src/native/DynamicLibraryLoader.cpp`, `src/modules/CommonJSLoader.cpp`
- **Resolution:** `src/modules/ModuleResolver.cpp`
- **Test addons:** `tests/native_addons/simple/` (built as `simple.so` under `<build>/tests/native_addons/simple/`) and `tests/native_addons/fixture/` (built as `tests/integration/native_addons/fixture.so`), used by the scripts in `tests/integration/native_addons/`
