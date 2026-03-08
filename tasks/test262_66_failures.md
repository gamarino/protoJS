# Test262 66 Failures Classification (Phase 1)

Generated with `PROTOJS_NO_FALLBACK=1` and `PROTOJS_USE_PROTO_EVAL=1`. No unsupported opcodes were observed.

## Summary

| Failure type        | Count | Fix direction |
|---------------------|-------|----------------|
| **syntax_error**    | 43    | Compile fails as script (import/export/using). Run as **module** (Phase 4). |
| **passed_or_unknown** | 23  | Ran on protoCore, exit 0. May still fail in runner (negative tests expect error; assertion). Verify with runner. |

## syntax_error (43) — run as module

These tests contain module syntax (import/export) or `using` and fail to compile as script. Fix: runner must run them with `--module` (compile with `JS_EVAL_TYPE_MODULE`).

- language/import/import-attributes/json-invalid.js
- language/import/import-attributes/json-named-bindings.js
- language/import/import-defer/errors/syntax-error/import-defer-of-syntax-error-fails.js
- language/module-code/* (36 tests: ambiguous-export-bindings, eval-export-*, eval-rqstd-abrupt, import-attributes/*, instn-*, top-level-await/*)
- language/statements/using/global-use-before-initialization-in-declaration-statement.js
- language/statements/using/global-use-before-initialization-in-prior-statement.js

## passed_or_unknown (23) — ran on protoCore

These compiled and ran on protoCore and exited 0. The runner may still mark some as failed (e.g. negative tests that expect ReferenceError for TDZ, or assertion failures).

- **eval-code / global-code / identifier-resolution (5):** language/eval-code/direct/strict-caller-global.js, var-env-global-lex-non-strict.js; language/eval-code/indirect/parse-failure-2.js; language/global-code/decl-lex-restricted-global.js; language/identifier-resolution/assign-to-global-undefined.js
- **line-terminators (7):** comment-multi-lf, comment-multi-ls, comment-multi-ps, comment-single-cr, comment-single-lf, comment-single-ls, comment-single-ps
- **module-code (1):** language/module-code/eval-self-abrupt.js (compiles as script)
- **statements (10):** const (2), let (2), switch/scope-lex-* (6)

## Opcodes

No `[ProtoInterpreter] unsupported opcode 0xNN` was seen. Phase 2 will implement OP_throw (and related) if TDZ tests require it when run through the runner (negative tests expect ReferenceError).

## Next steps

- **Phase 2:** Run the 23 through the runner; if any fail (e.g. TDZ negative), implement OP_throw and exception propagation.
- **Phase 3:** Preserve line endings in runner for line-terminator tests if needed.
- **Phase 4:** Add runner module mode and `protojs --module` for the 43.
- **Phase 5:** Fix eval/global/identifier semantics for the 5 script tests if runner fails them.
