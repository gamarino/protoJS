# protoJS documentation

Index of the documentation in this repository. Start with the [project README](../README.md).

## User documentation

- [README.md](../README.md) — project overview, build instructions, basic usage and roadmap.
- [INSTALLATION.md](INSTALLATION.md) — installing protoJS and its protoCore dependency, including building from source.
- [API_REFERENCE.md](API_REFERENCE.md) — reference for the globals and built-in modules exposed by protoJS.
- [EXAMPLES.md](EXAMPLES.md) — advanced usage examples.
- [DEFERRED_USAGE.md](DEFERRED_USAGE.md) — using `Deferred`, protoJS's asynchronous execution on worker threads from the CPU pool.
- [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) — the `protoCore` module: protoCore collections and utilities with no direct JavaScript equivalent.
- [NATIVE_MODULES.md](NATIVE_MODULES.md) — loading native C++ addon modules through `require()`.
- [THREAD_POOLS.md](THREAD_POOLS.md) — configuring the CPU and I/O thread pools.
- [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md) — how protoJS integrates with protoCore's unified module discovery system.
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — solutions to common build, runtime and module problems.

## Contributor documentation

- [ARCHITECTURE.md](../ARCHITECTURE.md) — technical architecture: main components (JSContextWrapper, TypeBridge, execution engine, Deferred, GC bridge, protoCore module), execution flow and key design decisions.
- [TESTING_STRATEGY.md](../TESTING_STRATEGY.md) — testing strategy: C++ unit tests, JavaScript integration tests, benchmarks, demonstration scripts and success criteria.
- [MIGRATION_QUICKJS_TO_PROTOCORE.md](MIGRATION_QUICKJS_TO_PROTOCORE.md) — migration plan for moving QuickJS-side bindings onto the protoCore-native global object.
- [src/runtime/README.md](../src/runtime/README.md) — the protoCore-native bytecode interpreter: compile, load and run flow, components, and rules for execution state and threads.
- [CONFORMANCE_JS.md](../CONFORMANCE_JS.md) — JavaScript conformance report against Test262, with per-category results and instructions to regenerate the data.
- [TEST262_STATUS.md](TEST262_STATUS.md) — results of the latest full Test262 run (`language` and `built-ins`), by family, with run instructions.
- [tests/README.md](../tests/README.md) — how to run each test layer (C++ unit, smoke, Test262, integration, conformity).
- [tests/conformity/README.md](../tests/conformity/README.md) — layout of the conformity test suite and how to run it.
- [tests/benchmarks/standard/README.md](../tests/benchmarks/standard/README.md) — the standard in-process benchmark suite used to compare protoJS with other engines.
- [tests/benchmarks/results/](../tests/benchmarks/results/) — dated benchmark reports and raw results.
- [packaging/PROCEDURES.md](../packaging/PROCEDURES.md) — building distribution packages and release procedures.
- [packaging/DOCUMENTATION.md](../packaging/DOCUMENTATION.md) — packager notes: user-facing dependency error messages and the release checklist.
- [CHANGELOG.md](../CHANGELOG.md) — notable changes.

## Archive

- [archive/README.md](archive/README.md) — historical documents (plans, phase reports, Test262 and performance history, design specifications). They are not maintained and may not match the current code.
