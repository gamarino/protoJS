#!/usr/bin/env node
/**
 * Command-line interface tests for protojs.
 *
 * Covers the flags whose behaviour is documented in README.md and
 * docs/API_REFERENCE.md:
 *   -c / --check   syntax check only; the input must NOT be executed
 *   --proto-eval   accepted for compatibility, no effect
 *   usage text     the I/O thread default must match the implemented factor
 *
 * Exits 0 when every case passes, 1 otherwise.
 *
 * Usage (from the repository root):
 *   node tests/integration/cli/test_cli_flags.js
 *   PROTOJS=/path/to/protojs node tests/integration/cli/test_cli_flags.js
 */

const path = require("path");
const fs = require("fs");
const { spawnSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../../..");
const FIXTURES = path.join(__dirname, "fixtures");

const proto =
  process.env.PROTOJS ||
  [
    path.join(REPO_ROOT, "build_release", "protojs"),
    path.join(REPO_ROOT, "build", "protojs"),
    path.join(REPO_ROOT, "protojs"),
  ].find((p) => fs.existsSync(p));

if (!proto || !fs.existsSync(proto)) {
  console.error("test_cli_flags: protojs binary not found. Set PROTOJS or build build_release/protojs");
  process.exit(1);
}

let failed = 0;

function run(args) {
  return spawnSync(proto, args, { encoding: "utf8", timeout: 5000 });
}

function check(name, condition, detail) {
  if (condition) return;
  failed++;
  console.error(`test_cli_flags FAIL: ${name}${detail ? " — " + detail : ""}`);
}

// -c on a valid file: exit 0 and the script's side effect must not happen.
{
  const r = run(["-c", path.join(FIXTURES, "check_valid.js")]);
  check("--check valid: exit status 0", r.status === 0, `status ${r.status}`);
  check(
    "--check valid: script is not executed",
    !String(r.stdout).includes("SIDE-EFFECT-RAN"),
    `stdout: ${JSON.stringify(String(r.stdout))}`
  );
}

// --check on a syntactically invalid file: exit 1 and report a SyntaxError.
{
  const r = run(["--check", path.join(FIXTURES, "check_invalid.js")]);
  check("--check invalid: exit status 1", r.status === 1, `status ${r.status}`);
  check(
    "--check invalid: reports SyntaxError",
    String(r.stderr).includes("SyntaxError"),
    `stderr: ${JSON.stringify(String(r.stderr))}`
  );
}

// --check and -e are mutually exclusive (Node reports this with status 9).
{
  const r = run(["-c", "-e", "1"]);
  check("--check with -e: exit status 9", r.status === 9, `status ${r.status}`);
}

// A module parses and its body is not executed. The fixture has no imports:
// in module mode the check cannot resolve them (documented in README.md).
{
  const r = run(["--input-type=module", "-c", path.join(FIXTURES, "check_module.mjs")]);
  check("--check module: exit status 0", r.status === 0, `status ${r.status}`);
  check(
    "--check module: module body is not executed",
    !String(r.stdout).includes("MODULE-SIDE-EFFECT"),
    `stdout: ${JSON.stringify(String(r.stdout))}`
  );
}

// -c without any input: usage error.
{
  const r = run(["-c"]);
  check("--check without input: exit status 1", r.status === 1, `status ${r.status}`);
}

// --proto-eval is a deprecated no-op and must still be accepted.
{
  const r = run(["--proto-eval", "-e", "1"]);
  check("--proto-eval accepted: exit status 0", r.status === 0, `status ${r.status}`);
}

// The usage text must describe the I/O thread default that the code implements.
{
  const r = run([]);
  const usage = String(r.stderr);
  check("usage without arguments: exit status 1", r.status === 1, `status ${r.status}`);
  check(
    "usage: no stale '3-4x' I/O thread claim",
    !usage.includes("3-4x"),
    "usage text still claims '3-4x CPU cores'"
  );
  check(
    "usage: states the implemented I/O factor default 3.0",
    usage.includes("3.0"),
    `usage: ${JSON.stringify(usage)}`
  );
}

if (failed) {
  console.error(`test_cli_flags: ${failed} check(s) failed`);
  process.exit(1);
}
console.log("test_cli_flags: all checks passed");
