# protoCore Unified Module Discovery — protoJS Integration

**Last Updated:** January 2026

## Overview

protoCore provides a **Unified Module Discovery and Provider System** (resolution chain, `ProviderRegistry`, `getImportModule`, thread-safe `SharedModuleCache`). protoJS uses protoCore's `ProtoSpace` per context; that space carries the resolution chain and module roots, enabling future alignment of `require`/ESM with protoCore's discovery.

This document describes how protoJS integrates with protoCore's module discovery and how to use or extend it. For the full specification of the system, see **protoCore**: `protoCore/docs/MODULE_DISCOVERY.md`.

---

## Relationship to protoJS Module System

- **protoJS** implements its own **file-based** module resolution for `require()` and ES Modules: `ModuleResolver` (paths, extensions `.node`/`.so`/`.dll`/`.dylib`/`.protojs`/`.js`/`.mjs`), `CommonJSLoader`, `ESModuleLoader`, and a JS-level `ModuleCache`.
- **protoCore** provides a **provider-based** discovery: configurable resolution chain (paths and `provider:alias`/`provider:GUID`), `ModuleProvider` implementations (e.g. `FileSystemProvider`), and `getImportModule(ProtoSpace*, logicalPath, attrName2create)` with a global `SharedModuleCache` and GC roots for loaded modules.

**Integration points:**

1. **ProtoSpace per context**  
   Each `JSContextWrapper` holds a `proto::ProtoSpace` (`pSpace`). That space is created by protoCore and includes:
   - **Resolution chain** — platform-dependent default (e.g. `[".", "/usr/lib/proto", ...]`) or a custom chain set via `setResolutionChain`.
   - **Module roots** — modules loaded via `getImportModule` are registered as GC roots in this space so they are not collected.

2. **Optional use of getImportModule**  
   When loading **native addons** or **custom backends** (e.g. DB, remote), protoJS can call protoCore's `getImportModule(&pSpace, logicalPath, "exports")` **before** or **after** file-based resolution. If the result is not `PROTO_NONE`, the host can convert the returned ProtoObject (wrapper with attribute `exports`) to a JS value and use it, keeping caching and GC roots in protoCore.

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

## When to Use protoCore getImportModule from protoJS

- **Native addons** loaded from a non-file source (e.g. from a plugin registry keyed by logical path).
- **Custom providers** (e.g. `provider:odoo_db`) registered in `ProviderRegistry` that resolve modules by logical path.
- **Unified caching** — one global `SharedModuleCache` in protoCore so that the same logical path returns the same module across contexts that share the same semantics.

protoJS's current `require()` and ESM loaders do **not** call `getImportModule` by default; they use `ModuleResolver` + `CommonJSLoader` / `ESModuleLoader`. Adding an optional first step that calls `getImportModule` for a given logical path is a natural extension and keeps caching and GC in protoCore.

---

## References

- **protoCore**  
  - [MODULE_DISCOVERY.md](../protoCore/docs/MODULE_DISCOVERY.md) — full specification of resolution chain, providers, cache, and `getImportModule`.
  - `getImportModule(ProtoSpace* space, const char* logicalPath, const char* attrName2create)`
  - `ProtoSpace::getResolutionChain()` / `setResolutionChain(const ProtoObject* newChain)`
  - `ProviderRegistry::instance()`, `registerProvider`, `findByAlias`, `findByGUID`, `getProviderForSpec("provider:alias")`
- **protoJS**  
  - [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) — protoCore collections and utilities exposed to JS.  
  - [NATIVE_MODULES.md](NATIVE_MODULES.md) — native addon loading (.node, .so, .dll, .dylib, .protojs).
