#!/usr/bin/env node
/**
 * Run Phase 1 conformity tests for protoJS.
 * Reads bootstrap manifest or runs all tests under tests/conformity/builtins.
 * Set PROTOJS to the protoJS binary path; else uses ./build/protojs or ./protojs.
 */

const fs = require("fs");
const path = require("path");
const { execSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../..");
const CONFORMITY_DIR = path.join(REPO_ROOT, "tests", "conformity");
const BOOTSTRAP = path.join(CONFORMITY_DIR, "bootstrap", "test262_bootstrap.txt");

function getProtoJS() {
  const exe = process.env.PROTOJS;
  if (exe) return exe;
  const candidates = [
    path.join(REPO_ROOT, "build", "protojs"),
    path.join(REPO_ROOT, "protojs"),
    path.join(REPO_ROOT, "build", "protoJS"),
  ];
  for (const p of candidates) {
    try {
      if (fs.existsSync(p)) return p;
    } catch (_) {}
  }
  return null;
}

function runScript(prog, scriptPath) {
  const full = path.isAbsolute(scriptPath) ? scriptPath : path.join(REPO_ROOT, scriptPath);
  if (!fs.existsSync(full)) return { ok: false, out: "file not found: " + scriptPath };
  try {
    execSync(`${JSON.stringify(prog)} ${JSON.stringify(full)}`, {
      cwd: REPO_ROOT,
      encoding: "utf8",
      timeout: 30000,
    });
    return { ok: true };
  } catch (e) {
    return { ok: false, out: (e.stdout || "") + (e.stderr || "") + (e.message || "") };
  }
}

function main() {
  const proto = getProtoJS();
  if (!proto) {
    console.error("ERROR: PROTOJS not set and protojs binary not found");
    process.exit(2);
  }

  let tests = [];
  if (fs.existsSync(BOOTSTRAP)) {
    const content = fs.readFileSync(BOOTSTRAP, "utf8");
    for (const line of content.split("\n")) {
      const t = line.replace(/#.*/, "").trim();
      if (t && (t.endsWith(".js") || t.endsWith(".mjs"))) tests.push(t);
    }
  }
  if (tests.length === 0) {
    const builtins = path.join(CONFORMITY_DIR, "builtins");
    if (fs.existsSync(builtins)) {
      tests = fs.readdirSync(builtins)
        .filter((f) => f.endsWith(".js"))
        .map((f) => path.join("tests", "conformity", "builtins", f));
    }
  }

  const failed = [];
  for (const t of tests) {
    const { ok, out } = runScript(proto, t);
    if (ok) {
      console.log("[PASS]", t);
    } else {
      console.log("[FAIL]", t);
      if (out) console.log(out.slice(0, 500));
      failed.push(t);
    }
  }

  if (failed.length) {
    console.log("Failed:", failed.length, "of", tests.length);
    process.exit(1);
  }
  console.log("All", tests.length, "conformity tests passed.");
}

main();
