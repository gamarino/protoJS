#!/usr/bin/env node
/**
 * Run the 66 skipped tests without skip list and capture failure mode per test.
 * Writes tests/test262/reports/failures_66.log and a JSON summary for Phase 1 classification.
 * Uses same config and buildTestFile as test262_runner.js.
 */

const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../../..");
const CONFIG_PATH = path.join(REPO_ROOT, "tests", "test262", "config", "test262_paths.json");
const SKIP_PATH = path.join(REPO_ROOT, "tests", "test262", "config", "skip_proto_eval.json");
const REPORT_DIR = path.join(REPO_ROOT, "tests", "test262", "reports");
const TMP_DIR = path.join(REPO_ROOT, "tests", "test262", ".tmp");

function readJSON(p) {
  return JSON.parse(fs.readFileSync(p, "utf8"));
}

function getConfig() {
  const cfg = readJSON(CONFIG_PATH);
  const rootRaw = process.env.TEST262_ROOT || cfg.test262_root;
  const root = path.isAbsolute(rootRaw) ? rootRaw : path.join(REPO_ROOT, rootRaw);
  const harnessRaw = cfg.harness_dir || "harness";
  const harnessDir = path.isAbsolute(harnessRaw) ? harnessRaw : path.join(root, harnessRaw);
  return { root, harnessDir };
}

function getProtoJSBinary() {
  if (process.env.PROTOJS) return process.env.PROTOJS;
  const candidates = [
    path.join(REPO_ROOT, "build", "protojs"),
    path.join(REPO_ROOT, "protojs"),
    path.join(REPO_ROOT, "build", "protoJS")
  ];
  for (const p of candidates) {
    if (fs.existsSync(p)) return p;
  }
  throw new Error("protojs binary not found. Set PROTOJS or build ./build/protojs");
}

function parseFrontMatter(source) {
  const result = { negative: null, includes: [], flags: {} };
  const start = source.indexOf("/*---");
  if (start === -1) return result;
  const end = source.indexOf("---*/", start + 5);
  if (end === -1) return result;
  const yaml = source.slice(start + 5, end).split("\n").map((l) => l.trimEnd());
  let inNegative = false;
  let inIncludes = false;
  let inFlags = false;
  for (let line of yaml) {
    if (!line) continue;
    const trimmed = line.trimStart();
    if (trimmed.startsWith("negative:")) {
      inNegative = true;
      inIncludes = false;
      inFlags = false;
      result.negative = { phase: null, type: null };
      continue;
    }
    if (trimmed.startsWith("includes:")) {
      inIncludes = true;
      inNegative = false;
      inFlags = false;
      const inline = trimmed.match(/includes:\s*\[([^\]]+)\]/);
      if (inline) {
        for (const part of inline[1].split(",")) {
          const name = part.trim().replace(/^["']|["']$/g, "");
          if (name && /\.js$/.test(name)) result.includes.push(name);
        }
      }
      continue;
    }
    if (trimmed.startsWith("flags:")) {
      inFlags = true;
      inNegative = false;
      inIncludes = false;
      const inline = trimmed.match(/flags:\s*\[([^\]]+)\]/);
      if (inline) {
        for (const part of inline[1].split(",")) {
          const name = part.trim().replace(/^["']|["']$/g, "");
          if (name) result.flags[name] = true;
        }
      }
      continue;
    }
    if (trimmed[0] !== "-" && !trimmed.startsWith("phase:") && !trimmed.startsWith("type:") && !trimmed.startsWith("flags:")) {
      inNegative = false;
      inIncludes = false;
      inFlags = false;
    }
    if (inNegative) {
      const mPhase = trimmed.match(/phase:\s*(\w+)/);
      if (mPhase) result.negative.phase = mPhase[1];
      const mType = trimmed.match(/type:\s*(\w+)/);
      if (mType) result.negative.type = mType[1];
      continue;
    }
    if (inIncludes) {
      const mInc = trimmed.match(/-+\s*([\w.-]+\.js)/);
      if (mInc) result.includes.push(mInc[1]);
      continue;
    }
    if (inFlags) {
      const mFlag = trimmed.match(/-+\s*(\w+)/);
      if (mFlag) result.flags[mFlag[1]] = true;
    }
  }
  return result;
}

function buildTestFile(cfg, test) {
  if (!fs.existsSync(TMP_DIR)) fs.mkdirSync(TMP_DIR, { recursive: true });
  const src = fs.readFileSync(test.full, "utf8");
  const meta = parseFrontMatter(src);
  const parts = [];
  const harnessAssert = path.join(cfg.harnessDir, "assert.js");
  const harnessSta = path.join(cfg.harnessDir, "sta.js");
  if (fs.existsSync(harnessAssert)) parts.push(fs.readFileSync(harnessAssert, "utf8"));
  if (fs.existsSync(harnessSta)) parts.push(fs.readFileSync(harnessSta, "utf8"));
  for (const inc of meta.includes) {
    const p = path.join(cfg.harnessDir, inc);
    if (fs.existsSync(p)) parts.push(fs.readFileSync(p, "utf8"));
  }
  if (meta.flags && (meta.flags.onlyStrict || meta.flags["onlyStrict"])) {
    parts.push('"use strict";');
  }
  parts.push(src);
  const tmpName = test.rel.replace(/[\\/]/g, "__");
  const tmpPath = path.join(TMP_DIR, tmpName);
  fs.writeFileSync(tmpPath, parts.join("\n\n"), "utf8");
  return { tmpPath, meta };
}

function classifyFailure(stderr, stdout, exitCode, meta) {
  const combined = (stderr || "") + (stdout || "");
  const unsupportedMatch = combined.match(/\[ProtoInterpreter\]\s+unsupported\s+opcode\s+0x([0-9a-fA-F]+)/);
  if (unsupportedMatch) {
    return { type: "unsupported_opcode", opcodeHex: unsupportedMatch[1], opcodeDec: parseInt(unsupportedMatch[1], 16) };
  }
  if (/compile failed, fallback to QuickJS/i.test(combined)) {
    return { type: "compile_fallback", note: "parse/compile failed, ran on QuickJS" };
  }
  if (/SyntaxError/i.test(combined)) {
    return { type: "syntax_error" };
  }
  if (/ReferenceError|TypeError|RangeError/i.test(combined)) {
    return { type: "runtime_error", error: combined.slice(0, 200) };
  }
  if (exitCode !== 0 && combined.trim()) {
    return { type: "other_failure", exitCode, snippet: combined.slice(0, 300) };
  }
  if (exitCode !== 0) {
    return { type: "non_zero_exit", exitCode };
  }
  return { type: "passed_or_unknown" };
}

function main() {
  const cfg = getConfig();
  const testsDirName = fs.existsSync(path.join(cfg.root, "tests")) ? "tests" : "test";
  const base = path.join(cfg.root, testsDirName);
  if (!fs.existsSync(base)) {
    console.error("Test262 tests dir not found:", base);
    process.exit(1);
  }

  const skipRaw = readJSON(SKIP_PATH);
  const paths = Array.isArray(skipRaw.tests) ? skipRaw.tests : [];
  if (paths.length === 0) {
    console.error("No tests in skip list");
    process.exit(1);
  }

  const proto = getProtoJSBinary();
  if (!fs.existsSync(REPORT_DIR)) fs.mkdirSync(REPORT_DIR, { recursive: true });

  const env = { ...process.env, PROTOJS_USE_PROTO_EVAL: "1", PROTOJS_NO_FALLBACK: "1" };
  const logLines = [];
  const results = [];

  for (const rel of paths) {
    const full = path.join(base, rel);
    if (!fs.existsSync(full)) {
      logLines.push(`${rel}\tFILE_NOT_FOUND\t${full}`);
      results.push({ path: rel, failureType: "file_not_found", fullPath: full });
      continue;
    }
    const test = { rel, full };
    let tmpPath;
    try {
      const built = buildTestFile(cfg, test);
      tmpPath = built.tmpPath;
    } catch (e) {
      logLines.push(`${rel}\tBUILD_ERROR\t${e.message}`);
      results.push({ path: rel, failureType: "build_error", message: e.message });
      continue;
    }

    const result = spawnSync(proto, [tmpPath], {
      cwd: REPO_ROOT,
      encoding: "utf8",
      maxBuffer: 512 * 1024,
      env,
      timeout: 15000,
      stdio: ["ignore", "pipe", "pipe"]
    });
    const exitCode = result.status != null ? result.status : (result.signal ? 1 : 0);
    const stdout = (result.stdout || "").toString();
    const stderr = (result.stderr || "").toString();

    const classification = classifyFailure(stderr, stdout, exitCode, null);
    const snippet = (stderr + stdout).slice(0, 500).replace(/\n/g, " ").trim();
    logLines.push(`${rel}\t${exitCode}\t${classification.type}\t${classification.opcodeHex || ""}\t${snippet}`);
    results.push({
      path: rel,
      exitCode: exitCode === -1 ? 0 : exitCode,
      failureType: classification.type,
      opcodeHex: classification.opcodeHex || null,
      opcodeDec: classification.opcodeDec != null ? classification.opcodeDec : null,
      snippet: snippet.slice(0, 300)
    });
  }

  const logPath = path.join(REPORT_DIR, "failures_66.log");
  fs.writeFileSync(logPath, logLines.join("\n") + "\n", "utf8");
  console.log("Wrote", logPath);

  const jsonPath = path.join(REPORT_DIR, "failures_66_classification.json");
  fs.writeFileSync(jsonPath, JSON.stringify({ generatedAt: new Date().toISOString(), results }, null, 2), "utf8");
  console.log("Wrote", jsonPath);

  const opcodeCounts = {};
  for (const r of results) {
    if (r.opcodeHex) {
      opcodeCounts[r.opcodeHex] = (opcodeCounts[r.opcodeHex] || 0) + 1;
    }
  }
  console.log("Unsupported opcodes seen:", Object.keys(opcodeCounts).length ? opcodeCounts : "none");
  const byType = {};
  for (const r of results) {
    byType[r.failureType] = (byType[r.failureType] || 0) + 1;
  }
  console.log("By failure type:", byType);
}

main();
