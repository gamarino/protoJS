// tests/test262/runner/rerun_conformance_all_rows.js
// Re-run all Test262 batches referenced in CONFORMANCE_JS.md with 5s timeout
// and refresh their rows in the markdown.

const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../..", ".."); // protoJS/
const CONFIG_PATH = path.join(REPO_ROOT, "tests", "test262", "config", "test262_paths.json");
const RUNNER_PATH = path.join(REPO_ROOT, "tests", "test262", "runner", "test262_runner.js");
const REPORT_DIR = path.join(REPO_ROOT, "tests", "test262", "reports");
const DOC_PATH = path.join(REPO_ROOT, "CONFORMANCE_JS.md");

// Ajusta si tu checkout de Test262 está en otro sitio
const TEST262_ROOT = process.env.TEST262_ROOT || "/home/gamarino/Documentos/proyectos/test262";
const PROTOJS_BIN = process.env.PROTOJS || path.join(REPO_ROOT, "build", "protojs");

function readJSON(p) {
  return JSON.parse(fs.readFileSync(p, "utf8"));
}

function writeJSON(p, obj) {
  fs.writeFileSync(p, JSON.stringify(obj, null, 2), "utf8");
}

function extractPatternsFromDoc(docText) {
  const patterns = new Set();
  const lines = docText.split("\n");

  // Filas de tabla tipo: | `pattern` | ... | ... | ... | ... | ... | Notes |
  const rowRe = /^\|\s*`([^`]+)`\s*\|/;

  for (const line of lines) {
    // Sólo considerar filas cuya columna Notes contenga "Official Test262"
    if (!line.includes("Official Test262")) continue;

    const m = rowRe.exec(line);
    if (!m) continue;
    const pattern = m[1].trim();
    if (!pattern) continue;

    // Rutas reales de Test262 que nos interesan
    if (pattern.startsWith("language/") || pattern.startsWith("built-ins/")) {
      patterns.add(pattern);
    }
  }

  return Array.from(patterns);
}

function listSnapshotsForPattern(pattern) {
  const prefix = "snapshot-" + pattern.replace(/[\\/]/g, "-") + "-";
  return fs
    .readdirSync(REPORT_DIR)
    .filter((f) => f.startsWith(prefix) && f.endsWith(".json"))
    .map((f) => path.join(REPORT_DIR, f));
}

function newestFile(paths) {
  if (paths.length === 0) return null;
  let best = paths[0];
  let bestMtime = fs.statSync(best).mtimeMs;
  for (const p of paths.slice(1)) {
    const m = fs.statSync(p).mtimeMs;
    if (m > bestMtime) {
      bestMtime = m;
      best = p;
    }
  }
  return best;
}

function runPattern(pattern) {
  console.log(`\n=== Running pattern: ${pattern} ===`);

  // 1) Snapshot “antes” para poder comparar
  const before = listSnapshotsForPattern(pattern);

  // 2) Update config: timeout=5000ms y patterns=[pattern]
  const cfg = readJSON(CONFIG_PATH);
  cfg.default_timeout_ms = 5000;
  cfg.patterns = [pattern];
  writeJSON(CONFIG_PATH, cfg);

  // 3) Ejecutar el runner sin capturar toda la salida
  try {
    execFileSync("node", [RUNNER_PATH], {
      cwd: REPO_ROOT,
      env: {
        ...process.env,
        TEST262_ROOT,
        PROTOJS: PROTOJS_BIN
      },
      stdio: "inherit"
    });
  } catch (e) {
    console.warn(`Runner exited with error for pattern ${pattern}: ${e.message}`);
  }

  // 4) Localizar el snapshot “después”
  const after = listSnapshotsForPattern(pattern);
  const newOnes = after.filter((p) => !before.includes(p));
  const snapshotPath = newestFile(newOnes.length ? newOnes : after);

  if (!snapshotPath) {
    throw new Error(`Could not find snapshot file for pattern ${pattern}`);
  }

  console.log(`  Using snapshot: ${path.relative(REPO_ROOT, snapshotPath)}`);

  const snap = readJSON(snapshotPath);
  const { total } = snap;
  const { passed, failed_syntax, failed_semantics, timeout } = snap.summary || {};

  return { total, passed, failed_syntax, failed_semantics, timeout };
}

function updateConformanceRow(docText, pattern, summary) {
  const lineMarker = `| \`${pattern}\``;
  const lines = docText.split("\n");
  let updated = false;

  const { total, passed, failed_syntax, failed_semantics, timeout } = summary;

  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].includes(lineMarker)) continue;

    const parts = lines[i].split("|");
    if (parts.length < 8) continue; // fila no estándar

    // Índices:
    // 0: ""           (antes del primer |)
    // 1: " `pattern` "
    // 2: " Total "
    // 3: " Passed "
    // 4: " Failed (syntax) "
    // 5: " Failed (semantics) "
    // 6: " Timeouts "
    // 7: " Notes "
    // 8: ""           (tras el último |), opcional

    parts[2] = ` ${total} `;
    parts[3] = ` ${passed} `;
    parts[4] = ` ${failed_syntax} `;
    parts[5] = ` ${failed_semantics} `;
    parts[6] = ` ${timeout} `;

    lines[i] = parts.join("|");
    updated = true;
    break;
  }

  if (!updated) {
    console.warn(`WARNING: could not find table row for pattern ${pattern} in CONFORMANCE_JS.md`);
  }

  return lines.join("\n");
}

function main() {
  console.log("protoJS Test262 batch re-run helper (all CONFORMANCE_JS.md rows)");
  console.log("Repo root:", REPO_ROOT);
  console.log("Test262 root:", TEST262_ROOT);
  console.log("protojs binary:", PROTOJS_BIN);

  let doc = fs.readFileSync(DOC_PATH, "utf8");
  let patterns = extractPatternsFromDoc(doc);

  // Focus on the high-value batches we care about now:
  // - built-ins/Object/defineProperty
  // - All built-ins/Array/prototype/* subsets
  patterns = patterns.filter(
    (p) =>
      p === "built-ins/Object/defineProperty" ||
      p.startsWith("built-ins/Array/prototype/")
  );

  console.log(`Found ${patterns.length} patterns in CONFORMANCE_JS.md`);

  for (const pattern of patterns) {
    const summary = runPattern(pattern);
    console.log(
      `  Summary for ${pattern}: total=${summary.total}, ` +
        `passed=${summary.passed}, failed_syntax=${summary.failed_syntax}, ` +
        `failed_semantics=${summary.failed_semantics}, timeout=${summary.timeout}`
    );
    doc = updateConformanceRow(doc, pattern, summary);
  }

  fs.writeFileSync(DOC_PATH, doc, "utf8");
  console.log("\nDone. CONFORMANCE_JS.md updated. Remember to:");
  console.log("  git add CONFORMANCE_JS.md tests/test262/config/test262_paths.json tests/test262/reports/snapshot-*.json");
  console.log('  git commit -m "test(test262): rerun all documented batches with 5s timeout"');
}

main();
