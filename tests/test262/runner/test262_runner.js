#!/usr/bin/env node
/**
 * Test262 runner for protoJS.
 *
 * - Reads configuration from tests/test262/config/test262_paths.json
 * - Discovers tests under the configured patterns (e.g. language/expressions)
 * - Parses YAML front-matter to understand negative expectations and includes
 * - Builds a temporary JS file that prepends harness/assert.js, harness/sta.js,
 *   and any required includes before the test body
 * - Executes each test with ./build/protojs (or PROTOJS env override)
 * - Writes a JSON snapshot with per-test results under tests/test262/reports/
 */

const fs = require("fs");
const path = require("path");
const { execFile } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../../..");
const CONFIG_PATH = path.join(REPO_ROOT, "tests", "test262", "config", "test262_paths.json");
const REPORT_DIR = path.join(REPO_ROOT, "tests", "test262", "reports");
const TMP_DIR = path.join(REPO_ROOT, "tests", "test262", ".tmp");

function readJSON(p) {
  return JSON.parse(fs.readFileSync(p, "utf8"));
}

function getConfig() {
  const cfg = readJSON(CONFIG_PATH);
  const root = process.env.TEST262_ROOT || cfg.test262_root;
  if (!root || !fs.existsSync(root)) {
    throw new Error(
      "Test262 root not found. Set TEST262_ROOT env or update tests/test262/config/test262_paths.json"
    );
  }
  const harnessDir = cfg.harness_dir || path.join(root, "harness");
  return {
    root,
    harnessDir,
    defaultTimeoutMs: cfg.default_timeout_ms || 10000,
    patterns: cfg.patterns && cfg.patterns.length ? cfg.patterns : ["language/expressions"]
  };
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

function walkTests(root, patterns) {
  const tests = [];
  const base = path.join(root, "tests");
  function walk(dir) {
    const entries = fs.readdirSync(dir, { withFileTypes: true });
    for (const e of entries) {
      const full = path.join(dir, e.name);
      if (e.isDirectory()) {
        walk(full);
      } else if (e.isFile() && e.name.endsWith(".js")) {
        const rel = path.relative(base, full).replace(/\\/g, "/");
        if (patterns.some((p) => rel.startsWith(p + "/") || rel === p)) {
          tests.push({ rel, full });
        }
      }
    }
  }
  if (fs.existsSync(base)) {
    walk(base);
  }
  return tests;
}

function parseFrontMatter(source) {
  const result = { negative: null, includes: [] };
  const start = source.indexOf("/*---");
  if (start === -1) return result;
  const end = source.indexOf("---*/", start + 5);
  if (end === -1) return result;
  const yaml = source.slice(start + 5, end).split("\n").map((l) => l.trimEnd());

  let inNegative = false;
  let inIncludes = false;
  for (let line of yaml) {
    if (!line) continue;
    if (line.startsWith("negative:")) {
      inNegative = true;
      inIncludes = false;
      result.negative = { phase: null, type: null };
      continue;
    }
    if (line.startsWith("includes:")) {
      inIncludes = true;
      inNegative = false;
      continue;
    }
    if (line[0] !== "-" && !line.startsWith("phase:") && !line.startsWith("type:")) {
      // Reset simple blocks when another top-level key appears
      inNegative = false;
      inIncludes = false;
    }
    if (inNegative) {
      const mPhase = line.match(/phase:\s*(\w+)/);
      if (mPhase) result.negative.phase = mPhase[1];
      const mType = line.match(/type:\s*(\w+)/);
      if (mType) result.negative.type = mType[1];
      continue;
    }
    if (inIncludes) {
      const mInc = line.match(/-+\s*([\w.-]+\.js)/);
      if (mInc) result.includes.push(mInc[1]);
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
    if (fs.existsSync(p)) {
      parts.push(fs.readFileSync(p, "utf8"));
    }
  }
  parts.push(src);

  const tmpName = test.rel.replace(/[\\/]/g, "__");
  const tmpPath = path.join(TMP_DIR, tmpName);
  fs.writeFileSync(tmpPath, parts.join("\n\n"), "utf8");
  return { tmpPath, meta };
}

function classifyResult(meta, err, stdout, stderr) {
  if (!meta.negative) {
    // Positive test
    if (!err) return "passed";
    const msg = (stderr || "") + (stdout || "");
    if (/SyntaxError/i.test(msg)) return "failed_syntax";
    return "failed_semantics";
  }
  // Negative test: must fail in the expected phase
  if (!err) {
    return "failed_semantics";
  }
  const msg = (stderr || "") + (stdout || "");
  if (meta.negative.type && !new RegExp(meta.negative.type, "i").test(msg)) {
    return "failed_semantics";
  }
  // We do not distinguish parse vs runtime strictly yet
  return "passed";
}

function runOne(proto, cfg, test) {
  const { tmpPath, meta } = buildTestFile(cfg, test);
  const start = Date.now();
  return new Promise((resolve) => {
    const child = execFile(
      proto,
      [tmpPath],
      {
        cwd: REPO_ROOT,
        timeout: cfg.defaultTimeoutMs,
        maxBuffer: 512 * 1024
      },
      (error, stdout, stderr) => {
        const durationMs = Date.now() - start;
        let result;
        if (error && error.killed) {
          result = "timeout";
        } else {
          result = classifyResult(meta, error, stdout, stderr);
        }
        resolve({
          path: test.rel,
          result,
          durationMs,
          negative: meta.negative,
          errorSummary: error ? String(error.message || "").slice(0, 200) : null
        });
      }
    );
    // In case timeout in execFile fails to kill on some platforms
    child.on("error", () => {});
  });
}

async function main() {
  const cfg = getConfig();
  const proto = getProtoJSBinary();
  if (!fs.existsSync(REPORT_DIR)) fs.mkdirSync(REPORT_DIR, { recursive: true });

  const tests = walkTests(cfg.root, cfg.patterns);
  console.log(`Discovered ${tests.length} Test262 files for patterns: ${cfg.patterns.join(", ")}`);

  const results = [];
  for (const t of tests) {
    // Simple sequential execution to start; can be parallelised later
    const r = await runOne(proto, cfg, t);
    console.log(`${r.result.toUpperCase()}: ${r.path} (${r.durationMs} ms)`);
    results.push(r);
  }

  const summary = { passed: 0, failed_syntax: 0, failed_semantics: 0, timeout: 0 };
  for (const r of results) {
    if (summary[r.result] !== undefined) summary[r.result]++;
  }

  const snapshot = {
    generatedAt: new Date().toISOString(),
    patterns: cfg.patterns,
    total: results.length,
    summary,
    results
  };
  const outPath = path.join(
    REPORT_DIR,
    `snapshot-${cfg.patterns.join("_").replace(/[\\/]/g, "-")}-${Date.now()}.json`
  );
  fs.writeFileSync(outPath, JSON.stringify(snapshot, null, 2), "utf8");
  console.log(`Snapshot written to ${path.relative(REPO_ROOT, outPath)}`);

  if (summary.failed_semantics || summary.failed_syntax || summary.timeout) {
    process.exit(1);
  }
}

main().catch((e) => {
  console.error("Test262 runner failed:", e && e.stack ? e.stack : e);
  process.exit(2);
});

