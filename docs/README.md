# protoJS documentation

Index of the documentation in this repository. Start with the [project README](../README.md).

## User documentation

- [README.md](../README.md) — project overview, build instructions, basic usage and roadmap.
- [INSTALLATION.md](INSTALLATION.md) — building and installing protoJS and its protoCore dependency from source, and building packages locally (no prebuilt packages are published).
- [API_REFERENCE.md](API_REFERENCE.md) — command-line options, environment variables, and the globals installed for scripts.
- [EXAMPLES.md](EXAMPLES.md) — short example scripts.
- [DEFERRED_USAGE.md](DEFERRED_USAGE.md) — `Deferred`, protoJS's promise-like object, whose function runs on the main thread's event loop.
- [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) — the `protoCore` global: `runInThread` for native work on protoCore threads, and the collection helpers not yet reachable from scripts.
- [NATIVE_MODULES.md](NATIVE_MODULES.md) — loading native C++ addon modules through `require()`.
- [THREAD_POOLS.md](THREAD_POOLS.md) — the CPU and I/O thread pools and their options.
- [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md) — how `require()` uses protoCore's unified module discovery system.
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — solutions to common build, runtime and module problems.

## Contributor documentation

- [ARCHITECTURE.md](../ARCHITECTURE.md) — technical architecture: main components (JSContextWrapper, TypeBridge, execution engine, Deferred, GC bridge, protoCore module), execution flow and key design decisions.
- [TESTING_STRATEGY.md](../TESTING_STRATEGY.md) — testing strategy: C++ unit tests, JavaScript integration tests, benchmarks, demonstration scripts and success criteria.
- [GC_BRIDGING.md](GC_BRIDGING.md) — rules for keeping protoCore objects alive across asynchronous, thread and native-call boundaries.
- [MIGRATION_QUICKJS_TO_PROTOCORE.md](MIGRATION_QUICKJS_TO_PROTOCORE.md) — status of moving bindings from the QuickJS-side global to the protoCore-native global, the migration pattern, and the remaining steps.
- [src/runtime/README.md](../src/runtime/README.md) — the protoCore-native bytecode interpreter: compile, specialise, load and run, execution state, components, and threads.
- [TEST262_STATUS.md](TEST262_STATUS.md) — the latest full Test262 run (`language` and `built-ins`), by family, with run instructions.
- [CONFORMANCE_JS.md](../CONFORMANCE_JS.md) — Test262 subset measurements, runner methodology and the history of conformance fixes.
- [tests/README.md](../tests/README.md) — how to run each test layer (C++ unit, smoke, Test262, integration, conformity).
- [tests/conformity/README.md](../tests/conformity/README.md) — layout of the conformity test suite and how to run it.
- [tests/benchmarks/standard/README.md](../tests/benchmarks/standard/README.md) — the standard benchmark suite and its Node.js and QuickJS comparison runners.
- [tests/benchmarks/results/](../tests/benchmarks/results/) — dated benchmark reports and raw results.
- [packaging/PROCEDURES.md](../packaging/PROCEDURES.md) — building packages locally with CPack or with the hand-maintained installer templates.
- [packaging/DOCUMENTATION.md](../packaging/DOCUMENTATION.md) — packager notes: dependency error messages and the release checklist.
- [CHANGELOG.md](../CHANGELOG.md) — notable changes.

## Archive

- [archive/README.md](archive/README.md) — historical documents (plans, phase reports, Test262 and performance history, design specifications). They are not maintained and may not match the current code.
