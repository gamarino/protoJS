/**
 * Phase 6 directed test: ProtoCore-native global object.
 *
 * Asserts that:
 * - Top-level var assignments persist within the same eval (global root update).
 * - Reading a global after writing returns the written value.
 *
 * Run with protoCore path: PROTOJS_USE_PROTO_EVAL=1 ./build/protojs --proto-eval tests/test262/tests/phase6_native_global.js
 * Or via smoke: node tests/test262/runner/proto_eval_smoke.js (includes one Phase 6 case).
 */
var __phase6_a = 1;
var __phase6_b = 2;
if (__phase6_a !== 1 || __phase6_b !== 2) {
  throw new Error('Phase6 global: initial var write/read failed');
}
__phase6_a = 10;
if (__phase6_a !== 10) {
  throw new Error('Phase6 global: reassignment read failed');
}
if (typeof Array !== 'function' || typeof Object !== 'function') {
  throw new Error('Phase6 global: built-ins not on global');
}
