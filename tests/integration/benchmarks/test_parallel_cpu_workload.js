#!/usr/bin/env node
/**
 * Regression test for the parallel_cpu benchmark workload.
 *
 * parallel_cpu.js used to run 2e5 iterations per task under protojs and 2e6
 * everywhere else, so the two arms measured different amounts of work and the
 * comparison was meaningless. This test runs the benchmark under protojs and
 * under node and asserts that:
 *
 *   - both report the work they performed (`work_per_task` / `total_work`),
 *   - both performed the SAME work,
 *   - both name the executor that ran the tasks, because the executing code is
 *     not the same in every runtime.
 *
 * Exits 0 when every check passes, 1 otherwise.
 *
 * Usage (from the repository root):
 *   node tests/integration/benchmarks/test_parallel_cpu_workload.js
 *   PROTOJS=/path/to/protojs node tests/integration/benchmarks/test_parallel_cpu_workload.js
 */

const path = require("path");
const fs = require("fs");
const { spawnSync } = require("child_process");

const REPO_ROOT = path.resolve(__dirname, "../../..");
const BENCH = path.join(REPO_ROOT, "tests", "benchmarks", "standard", "parallel_cpu.js");
const PREFIX = "__BENCH_RESULT__";

const proto =
  process.env.PROTOJS ||
  [
    path.join(REPO_ROOT, "build_release", "protojs"),
    path.join(REPO_ROOT, "build", "protojs"),
    path.join(REPO_ROOT, "protojs"),
  ].find((p) => fs.existsSync(p));

if (!proto || !fs.existsSync(proto)) {
  console.error("test_parallel_cpu_workload: protojs binary not found. Set PROTOJS or build build_release/protojs");
  process.exit(1);
}

let failed = 0;

function check(name, condition, detail) {
  if (condition) return;
  failed++;
  console.error(`test_parallel_cpu_workload FAIL: ${name}${detail ? " — " + detail : ""}`);
}

function runBenchmark(command, args, label) {
  const r = spawnSync(command, args, { encoding: "utf8", timeout: 120000 });
  if (r.status !== 0) {
    check(`${label}: benchmark exits 0`, false, `status ${r.status}, stderr: ${String(r.stderr).slice(0, 400)}`);
    return null;
  }
  const lines = String(r.stdout).trim().split("\n");
  for (let i = lines.length - 1; i >= 0; i--) {
    const idx = lines[i].indexOf(PREFIX);
    if (idx !== -1) {
      try {
        return JSON.parse(lines[i].substring(idx + PREFIX.length));
      } catch (e) {
        check(`${label}: result line is valid JSON`, false, lines[i]);
        return null;
      }
    }
  }
  check(`${label}: prints a ${PREFIX} line`, false, `stdout: ${String(r.stdout).slice(0, 400)}`);
  return null;
}

const protoResult = runBenchmark(proto, [BENCH], "protojs");
const nodeResult = runBenchmark(process.execPath, [BENCH], "node");

for (const [label, result] of [["protojs", protoResult], ["node", nodeResult]]) {
  if (!result) continue;
  check(
    `${label}: reports work_per_task`,
    typeof result.work_per_task === "number",
    `work_per_task: ${JSON.stringify(result.work_per_task)}`
  );
  check(
    `${label}: reports total_work consistent with the task count`,
    result.total_work === result.work_per_task * result.tasks,
    `total_work: ${JSON.stringify(result.total_work)}, tasks: ${JSON.stringify(result.tasks)}`
  );
  check(
    `${label}: names the executor that ran the tasks`,
    typeof result.executor === "string" && result.executor.length > 0,
    `executor: ${JSON.stringify(result.executor)}`
  );
}

if (protoResult && nodeResult) {
  check(
    "protojs and node run the same work per task",
    protoResult.work_per_task === nodeResult.work_per_task,
    `protojs: ${protoResult.work_per_task}, node: ${nodeResult.work_per_task}`
  );
  check(
    "protojs and node run the same total work",
    protoResult.total_work === nodeResult.total_work,
    `protojs: ${protoResult.total_work}, node: ${nodeResult.total_work}`
  );
}

if (failed) {
  console.error(`test_parallel_cpu_workload: ${failed} check(s) failed`);
  process.exit(1);
}
console.log("test_parallel_cpu_workload: all checks passed");
