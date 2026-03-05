#!/usr/bin/env node
/**
 * Directed smoke test for the protoCore interpreter path.
 *
 * Runs protojs with PROTOJS_USE_PROTO_EVAL=1 and a short list of expressions
 * (arithmetic, typeof, comparison, Array.isArray). Exits with code 0 if all
 * pass; non-zero otherwise. Use after interpreter changes to quickly verify
 * the protoCore path.
 *
 * Usage (from protoJS repo root):
 *   node tests/test262/runner/proto_eval_smoke.js
 *   PROTOJS=/path/to/protojs node tests/test262/runner/proto_eval_smoke.js
 */

const path = require("path");
const { execFileSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../../..");
const proto =
  process.env.PROTOJS ||
  [path.join(REPO_ROOT, "build", "protojs"), path.join(REPO_ROOT, "protojs")].find(
    (p) => require("fs").existsSync(p)
  );

if (!proto) {
  console.error("proto_eval_smoke: protojs binary not found. Set PROTOJS or build ./build/protojs");
  process.exit(1);
}

const env = { ...process.env, PROTOJS_USE_PROTO_EVAL: "1" };

const cases = [
  { code: "1 + 2", name: "arithmetic" },
  { code: "typeof 1", name: "typeof number" },
  { code: "1 < 2", name: "comparison" },
  { code: "Array.isArray([])", name: "Array.isArray" },
  { code: "typeof function(){}", name: "typeof function" },
];

let failed = 0;
for (const { code, name } of cases) {
  try {
    execFileSync(proto, ["--proto-eval", "-e", code], {
      env,
      stdio: "pipe",
      timeout: 5000,
    });
  } catch (err) {
    console.error(`proto_eval_smoke FAIL: ${name} (${code}): ${err.message || err}`);
    failed++;
  }
}

if (failed) {
  console.error(`proto_eval_smoke: ${failed}/${cases.length} failed`);
  process.exit(1);
}
console.log(`proto_eval_smoke: ${cases.length} passed`);
