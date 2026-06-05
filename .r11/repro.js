#!/usr/bin/env node
// Reproduce a single test262 test using the same wrapping the runner does.
// Usage: node .r10/repro.js <relative-test-path>
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "..");
const TEST262_ROOT = process.env.TEST262_ROOT || path.resolve(REPO_ROOT, "..", "test262");
const HARNESS = path.join(TEST262_ROOT, "harness");
const PROTOJS = process.env.PROTOJS || path.join(REPO_ROOT, "build_release", "protojs");

function parseFM(src) {
  const r = { negative: null, includes: [], flags: {} };
  const a = src.indexOf("/*---");
  if (a < 0) return r;
  const b = src.indexOf("---*/", a + 5);
  if (b < 0) return r;
  const yaml = src.slice(a + 5, b).split("\n").map(l => l.trimEnd());
  let inI = false, inN = false, inF = false;
  for (const line of yaml) {
    if (!line) continue;
    const t = line.trimStart();
    if (t.startsWith("negative:")) { inN = true; inI = inF = false; r.negative = {phase:null,type:null}; continue; }
    if (t.startsWith("includes:")) {
      inI = true; inN = inF = false;
      const inline = t.match(/includes:\s*\[([^\]]+)\]/);
      if (inline) for (const p of inline[1].split(",")) {
        const n = p.trim().replace(/^["']|["']$/g, "");
        if (/\.js$/.test(n)) r.includes.push(n);
      }
      continue;
    }
    if (t.startsWith("flags:")) {
      inF = true; inI = inN = false;
      const inline = t.match(/flags:\s*\[([^\]]+)\]/);
      if (inline) for (const p of inline[1].split(",")) {
        const n = p.trim().replace(/^["']|["']$/g, "");
        if (n) r.flags[n] = true;
      }
      continue;
    }
    if (t[0] !== "-" && !t.startsWith("phase:") && !t.startsWith("type:") && !t.startsWith("flags:")) {
      inN = inI = inF = false;
    }
    if (inN) {
      const mp = t.match(/phase:\s*(\w+)/); if (mp) r.negative.phase = mp[1];
      const mt = t.match(/type:\s*(\w+)/);  if (mt) r.negative.type = mt[1];
      continue;
    }
    if (inI) { const m = t.match(/-+\s*([\w.-]+\.js)/); if (m) r.includes.push(m[1]); continue; }
    if (inF) { const m = t.match(/-+\s*(\w+)/); if (m) r.flags[m[1]] = true; }
  }
  return r;
}

function build(testPath) {
  const full = path.join(TEST262_ROOT, "test", testPath);
  const src = fs.readFileSync(full, "utf8");
  const meta = parseFM(src);
  const isModule = !!meta.flags.module;
  const parts = [];
  if (!isModule) {
    parts.push(fs.readFileSync(path.join(HARNESS, "assert.js"), "utf8"));
    parts.push(fs.readFileSync(path.join(HARNESS, "sta.js"), "utf8"));
    for (const inc of meta.includes) {
      const p = path.join(HARNESS, inc);
      if (fs.existsSync(p)) parts.push(fs.readFileSync(p, "utf8"));
    }
    if (meta.flags.async) {
      const dp = path.join(HARNESS, "doneprintHandle.js");
      if (fs.existsSync(dp)) parts.push(fs.readFileSync(dp, "utf8"));
    }
  }
  if (meta.flags.onlyStrict) parts.unshift('"use strict";');
  parts.push(src);
  const tmp = path.join("/tmp", "repro_" + testPath.replace(/[\\/]/g, "__"));
  fs.writeFileSync(tmp, parts.join("\n\n"));
  return { tmp, meta, isModule };
}

const arg = process.argv[2];
if (!arg) { console.error("usage: repro.js <test/path>"); process.exit(2); }
const { tmp, meta, isModule } = build(arg);
const env = { ...process.env };
if (!isModule) {
  env.PROTOJS_USE_PROTO_EVAL = "1";
  env.PROTOJS_NO_FALLBACK = "1";
}
try {
  const out = execFileSync(PROTOJS, [tmp], { env, timeout: 8000, stdio: "pipe" });
  process.stdout.write(out);
  process.exit(0);
} catch (e) {
  if (e.stdout) process.stdout.write(e.stdout);
  if (e.stderr) process.stderr.write(e.stderr);
  process.exit(e.status || 1);
}
