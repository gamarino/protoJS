# protoCore Module Discovery in protoJS

protoCore provides a unified module discovery system: a per-`ProtoSpace` resolution chain, a `ProviderRegistry` of `ModuleProvider` implementations, and `ProtoSpace::getImportModule`, backed by a shared, thread-safe module cache. The full specification is protoCore's [MODULE_DISCOVERY.md](https://github.com/numaes/protoCore/blob/master/docs/MODULE_DISCOVERY.md).

This document describes where protoJS uses that system and how host code can extend it.

---

## Relationship to the protoJS module system

- **protoJS** resolves modules itself for `require()` and ES modules: `ModuleResolver` (file-based lookup with the extensions `.node`, the platform shared-library extension, `.protojs`, `.js`, `.mjs`), `CommonJSLoader`, `ESModuleLoader` and `ModuleCache`, all under `src/modules/`.
- **protoCore** resolves *logical paths* through its resolution chain and registered providers. `ProtoSpace::getImportModule(ProtoContext* context, const char* logicalPath, const char* attrName2create)` returns a wrapper object whose attribute `attrName2create` holds the module, and adds loaded modules to the space's module roots so the garbage collector keeps them alive.

## Integration points

1. **One `ProtoSpace` per runtime instance.** Each `JSContextWrapper` owns a `proto::ProtoSpace`, available through `JSContextWrapper::getProtoSpace()` (`src/JSContext.h`). The resolution chain and module roots belong to that space.

2. **`require()` consults protoCore first for bare specifiers.** The `require` global calls `CommonJSLoader::require` (`src/modules/CommonJSLoader.cpp`), which handles a bare specifier (one that does not start with `./`, `../` or `/`) in this order:
   1. `space->getImportModule(pContext, specifier, "exports")`. If protoCore resolves the logical path, the `exports` attribute is converted to a JavaScript value, cached under the key `umd:<specifier>`, and returned.
   2. A property of the same name on the QuickJS-side global object (`require('buffer')` is wrapped as `{ Buffer }`). The standard modules (`fs`, `path`, `http`, ...) are registered on the protoCore-native global instead, so this step does not find them; use their globals directly.
   3. File-based resolution through `ModuleResolver`, including `node_modules` package lookup.

   Relative and absolute specifiers skip steps 1 and 2.

3. **ES modules do not use protoCore discovery.** Imports in module-mode code (`--input-type=module`) are resolved by the QuickJS module-loader hooks installed in `src/JSContext.cpp`.

4. **Providers.** Custom `ModuleProvider` implementations can be registered with `ProviderRegistry::instance().registerProvider(...)` and referenced from the resolution chain as `provider:<alias>` or `provider:<GUID>`. protoJS registers no providers of its own.

---

## Default resolution chain

protoJS creates its `ProtoSpace` with protoCore's defaults, so the resolution chain is the platform default defined in protoCore's `core/ProtoSpace.cpp`:

| Platform | Default chain |
|----------|---------------|
| Linux and other non-Apple POSIX systems | `[".", "/usr/lib/proto", "/usr/local/lib/proto"]` |
| macOS | `[".", "/usr/local/lib/proto"]` |
| Windows | `[".", "C:\\Program Files\\proto\\lib"]` |

To change it, call `setResolutionChain(const ProtoObject* newChain)` on the space returned by `JSContextWrapper::getProtoSpace()`, passing a `ProtoList` of `ProtoString` entries. `getResolutionChain()` returns the current chain.

---

## When to use protoCore discovery from protoJS

- Loading modules from a source other than the file system, keyed by logical path (for example a plugin registry exposed through a provider).
- Sharing one module cache across runtime instances: protoCore's cache is global and thread-safe, so the same logical path yields the same module.

---

## References

- **protoCore**
  - [MODULE_DISCOVERY.md](https://github.com/numaes/protoCore/blob/master/docs/MODULE_DISCOVERY.md) — resolution chain, providers, cache and `getImportModule`.
  - `ProtoSpace::getImportModule`, `ProtoSpace::getResolutionChain`, `ProtoSpace::setResolutionChain` (`headers/protoCore.h`).
  - `ProviderRegistry::instance()`, `registerProvider`, `findByAlias`, `findByGUID`, `getProviderForSpec` (`headers/protoCore.h`).
- **protoJS**
  - [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) — the `protoCore` global.
  - [NATIVE_MODULES.md](NATIVE_MODULES.md) — native addon loading (`.node`, `.so`, `.dll`, `.dylib`, `.protojs`).
