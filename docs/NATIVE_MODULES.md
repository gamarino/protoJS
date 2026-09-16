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

The ABI is defined in `src/native/NativeModuleABI.h` (`PROTOJS_ABI_VERSION` is **2**). An addon works with protoCore objects only; no QuickJS type appears in the interface. An addon must:

1. Export the symbol **`protojs_native_module_info`**, of type `protojs::ProtoJSNativeModuleInfo` (`abiVersion`, `name`, `version`, `init`, `cleanup`).
2. Provide an **init** function with the signature
   `int init(proto::ProtoContext* context, const proto::ProtoObject* module);`
   returning `0` on success and non-zero on error. `cleanup` is optional (`nullptr`).

`module` is a mutable protoCore object with the CommonJS shape `{ id, filename, exports, loaded, parent }`, created with an empty mutable `exports`. The init function registers everything it exports on that object. After init returns, `require` uses `module.exports` **as it is** — there is no conversion — and keeps the module record in `require.cache`, which is reachable from the native global and therefore GC-rooted.

Exported functions use `ProtoJSNativeFunction`, which is exactly `proto::ProtoMethod`, so the interpreter calls them directly.

#### Helpers

These are implemented in `protojs_core` and exported from the `protojs` executable, so an addon resolves them at load time and links against nothing:

| Helper | Purpose |
|---|---|
| `protojs_make_function(ctx, fn, name, length)` | Wrap a native function so JavaScript can call it; it carries `name` and `length` |
| `protojs_set_export(ctx, module, name, value)` | `module.exports.<name> = value` |
| `protojs_get_exports(ctx, module)` | Read `module.exports` |
| `protojs_set_exports_object(ctx, module, exports)` | Replace `module.exports` wholesale |
| `protojs_throw(ctx, type, message)` | Raise a catchable JavaScript exception; return `PROTO_NONE` immediately afterwards |

#### Upgrading from ABI v1

v1 addons received a `JSContext*` and `JSValue`s and built their exports with the QuickJS C API. Scripts run on the protoCore interpreter, which never sees those values: the exports were converted with `TypeBridge::fromJS`, and a QuickJS function became an empty object, so **exported functions were not callable**. A v1 addon is now refused at load time with

```
native addon <path> uses ABI v1 (QuickJS values); rebuild against ABI v2
```

To upgrade, replace the QuickJS calls with the helpers above and change the init signature; the example below is the whole of the `simple` test addon.

### Minimal example (C++)

```cpp
#include "native/NativeModuleABI.h"
#include "headers/protoCore.h"

namespace protojs {
namespace {

const proto::ProtoObject* sum_impl(proto::ProtoContext* ctx,
                                    const proto::ProtoObject* /*self*/,
                                    const proto::ParentLink*,
                                    const proto::ProtoList* args,
                                    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2) {
        protojs_throw(ctx, "TypeError", "sum expects two numbers");
        return PROTO_NONE;
    }
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    const proto::ProtoObject* b = args->getAt(ctx, 1);
    if (!a->isInteger(ctx) || !b->isInteger(ctx)) {
        protojs_throw(ctx, "TypeError", "sum expects two numbers");
        return PROTO_NONE;
    }
    return ctx->fromInteger(a->asLong(ctx) + b->asLong(ctx));
}

int init_impl(proto::ProtoContext* ctx, const proto::ProtoObject* module) {
    if (protojs_set_export(ctx, module, "version", ctx->fromInteger(1)) != 0) return -1;
    if (protojs_set_export(ctx, module, "sum",
                            protojs_make_function(ctx, sum_impl, "sum", 2)) != 0) return -1;
    return 0;
}

} // namespace

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

- **Headers:** protoJS `src/` and the protoCore source directory with its `headers/` subdirectory. QuickJS headers are **not** needed: an ABI v2 addon contains no QuickJS type, and the two test addons are built without that include directory.
- **Linking:** build a shared library and do not link QuickJS or protoCore into it; the protoCore symbols and the `protojs_*` helpers are resolved at load time from the `protojs` executable, which is linked with `-rdynamic` (`CMakeLists.txt`).
- **File name:** the resolver looks for `<name>.so` (or `.node`, `.protojs`), not `lib<name>.so`. The test addons set `PREFIX ""` in CMake for this reason.

## Reference

- **ABI:** `src/native/NativeModuleABI.h`
- **Loader:** `src/native/DynamicLibraryLoader.cpp`, `src/modules/CommonJSLoader.cpp`
- **Resolution:** `src/modules/ModuleResolver.cpp`
- **Test addons:** `tests/native_addons/simple/` (built as `simple.so` under `<build>/tests/native_addons/simple/`) and `tests/native_addons/fixture/` (built as `tests/integration/native_addons/fixture.so`), used by the scripts in `tests/integration/native_addons/`
