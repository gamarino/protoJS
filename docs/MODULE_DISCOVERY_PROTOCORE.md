# protoCore Unified Module Discovery — protoJS Integration

**Last Updated:** January 2026

## Overview

protoCore provides a **Unified Module Discovery and Provider System** (resolution chain, `ProviderRegistry`, `ProtoSpace::getImportModule`, thread-safe `SharedModuleCache`). protoJS uses protoCore's `ProtoSpace` per context; that space carries the resolution chain and module roots, enabling future alignment of `require`/ESM with protoCore's discovery.

This document describes how protoJS integrates with protoCore's module discovery and how to use or extend it. For the full specification of the system, see **protoCore**: `protoCore/docs/MODULE_DISCOVERY.md`.

---

## Relationship to protoJS Module System

- **protoJS** implements its own **file-based** module resolution for `require()` and ES Modules: `ModuleResolver` (paths, extensions `.node`/`.so`/`.dll`/`.dylib`/`.protojs`/`.js`/`.mjs`), `CommonJSLoader`, `ESModuleLoader`, and a JS-level `ModuleCache`.
- **protoCore** provides a **provider-based** discovery: configurable resolution chain (paths and `provider:alias`/`provider:GUID`), `ModuleProvider` implementations (e.g. `FileSystemProvider`), and `ProtoSpace::getImportModule(logicalPath, attrName2create)` with a global `SharedModuleCache` and GC roots for loaded modules.

**Integration points:**

1. **ProtoSpace per context**  
   Each `JSContextWrapper` holds a `proto::ProtoSpace` (`pSpace`). That space is created by protoCore and includes:
   - **Resolution chain** — platform-dependent default (e.g. `[".", "/usr/lib/proto", ...]`) or a custom chain set via `setResolutionChain`.
   - **Module roots** — modules loaded via `ProtoSpace::getImportModule` are registered as GC roots in this space so they are not collected.

2. **require() uses ProtoSpace::getImportModule for bare specifiers**  
   For **bare specifiers** (e.g. `require("mymodule")` or `require("fs")` — not starting with `./`, `../`, or `/`), protoJS calls protoCore's `space->getImportModule(logicalPath, "exports")` **first**. If a provider or the default chain resolves the logical path, the returned module (wrapper attribute `exports`) is converted to a JS value via `TypeBridge::toJS`, cached under `umd:<specifier>`, and returned. If `getImportModule` returns nothing, resolution **falls back** to file-based `ModuleResolver` (built-ins like `path`, `fs`, and relative paths are resolved as before).

3. **ProviderRegistry**  
   Custom `ModuleProvider` implementations can be registered in protoCore's `ProviderRegistry::instance()`. The resolution chain can reference them with `provider:alias` or `provider:GUID`. protoJS does not register providers by default; host code or extensions can do so.

---

## Default Resolution Chain in protoJS

Because protoJS creates a default `ProtoSpace` (in `JSContextWrapper`), the resolution chain is the **platform default** from protoCore:

| Platform | Default chain (example) |
|----------|--------------------------|
| Linux    | `[".", "/usr/lib/proto", "/usr/local/lib/proto"]` |
| Windows  | `[".", "C:\\Program Files\\proto\\lib"]` (or equivalent) |
| macOS    | `[".", "/usr/local/lib/proto", "~/Library/Application Support/proto/lib"]` |

To customize the chain (e.g. add a custom path or a provider), use protoCore's API on the same `ProtoSpace` that protoJS uses: get it via `JSContextWrapper::getProtoSpace()` (or equivalent host API) and call `setResolutionChain(...)` with a `ProtoList` of `ProtoString*` entries.

---

## When to Use protoCore ProtoSpace::getImportModule from protoJS

- **Native addons** loaded from a non-file source (e.g. from a plugin registry keyed by logical path).
- **Custom providers** (e.g. `provider:odoo_db`) registered in `ProviderRegistry` that resolve modules by logical path.
- **Unified caching** — one global `SharedModuleCache` in protoCore so that the same logical path returns the same module across contexts that share the same semantics.

protoJS's **require()** calls `getImportModule` for every **bare** specifier first; ESM loaders still use `ModuleResolver` + `ESModuleLoader` only. Custom providers registered in protoCore's `ProviderRegistry` (and the default `FileSystemProvider` chain) can thus supply modules for bare IDs; otherwise resolution falls back to file-based lookup.

---

## References

- **protoCore**  
  - [MODULE_DISCOVERY.md](../protoCore/docs/MODULE_DISCOVERY.md) — full specification of resolution chain, providers, cache, and `ProtoSpace::getImportModule`.
  - `ProtoSpace::getImportModule(const char* logicalPath, const char* attrName2create)`
  - `ProtoSpace::getResolutionChain()` / `setResolutionChain(const ProtoObject* newChain)`
  - `ProviderRegistry::instance()`, `registerProvider`, `findByAlias`, `findByGUID`, `getProviderForSpec("provider:alias")`
- **protoJS**  
  - [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) — protoCore collections and utilities exposed to JS.  
  - [NATIVE_MODULES.md](NATIVE_MODULES.md) — native addon loading (.node, .so, .dll, .dylib, .protojs).
