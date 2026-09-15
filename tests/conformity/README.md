# Conformity Test Suite

Small JavaScript checks of built-in semantics and module identity on the protoCore interpreter. They complement the official Test262 runs described in [TEST262_STATUS.md](../../docs/TEST262_STATUS.md).

## Layout

- **`builtins/`** — `test_number_conformity.js`, `test_string_conformity.js`, `test_array_conformity.js`, `test_object_conformity.js`.
- **`import/`** — `test_require_twice.js` and `test_module_identity.js` check that repeated `require()` calls return the same module; `pkg/dummy_module.js` is their fixture.
- **`bootstrap/test262_bootstrap.txt`** — manifest of the tests that `run_conformity.js` runs.

## Running

From the repository root, run one script directly:

```bash
./build/protojs tests/conformity/builtins/test_number_conformity.js
```

or run the manifest (or, without a manifest, every script under `builtins/`) with the runner:

```bash
node tests/conformity/run_conformity.js
```

The runner uses the binary named by the `PROTOJS` environment variable, or else `build/protojs` or `./protojs`. These tests are not part of CTest or `tests/run_all_tests.sh`.
