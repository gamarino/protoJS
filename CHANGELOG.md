# Changelog

All notable changes to protoJS are documented in this file.

## [Unreleased]

### Fixed

- **GCBridge null-pointer handling** (2026-02-07): Fixed `-Wnonnull` compiler warnings and potential undefined behavior in `GCBridge::detectLeaks()` and `GCBridge::getMemoryStats()` when `ProtoContext` is null. Both functions now return early with null/empty values instead of dereferencing a null pointer. Added null checks in `reportLeaks()` and `getMemoryStats()` for defensive handling of empty reports.

### Build & Test

- Full project recompilation: protoCore + protoJS clean build
- All 33 unit tests passing (ctest)
- Integration tests verified (hello_world, arithmetic, modules)
