#!/usr/bin/env python3
"""Test262 regression gate for protoJS: an expected-failures diff, not a percentage.

Why a diff and not a percentage
-------------------------------
A pass rate cannot tell a fixed test from a newly broken one.  3,619 of 3,875 is
still 3,619 of 3,875 after a commit that repairs one test and breaks another, so
a percentage gate would have reported that commit green.  This gate compares the
*set* of non-passing test paths against a recorded baseline and fails on any
difference in either direction:

  * a test that used to pass and now fails  -> a regression;
  * a test that used to fail and now passes -> the baseline is stale.

The second case is also a failure, on purpose.  A baseline that is allowed to
drift silently downwards stops detecting the first case, which is the only thing
this gate exists for.  Recording the improvement is a one-line `--update` in the
same commit that earned it; that is not weakening an expectation, it is banking
one.

Why the premise is checked
--------------------------
Two facts have to hold before the diff means anything:

  * the corpus commit must be the pinned one.  Test262 gains and loses tests
    every week, so an unpinned corpus moves the denominator underneath the
    baseline and turns "3 new failures" into "upstream added 3 tests";
  * the denominator must be the recorded one.  If discovery finds a different
    number of files, the two sets are not comparable regardless of how they
    diff.

Either mismatch is reported as a failure of the gate's own premise, distinct
from a conformance regression, because the two need different fixes.

Usage
-----
    # measure and gate (runs the suite; ~97 s sequentially)
    python3 tests/test262/runner/regression_gate.py

    # gate an existing snapshot without re-running
    python3 tests/test262/runner/regression_gate.py --snapshot <snapshot.json>

    # bank an improvement
    python3 tests/test262/runner/regression_gate.py --update

Exit codes: 0 clean, 1 conformance delta, 2 broken premise, 3 usage/IO error.
"""

import argparse
import json
import os
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_BASELINE = os.path.join(
    REPO_ROOT, "tests", "test262", "config", "regression_gate_baseline.json"
)
REPORT_DIR = os.path.join(REPO_ROOT, "tests", "test262", "reports")

# The three directories of the per-commit gate.  Chosen because they run in 97 s,
# not because they score well: their 93.4 % is a property of these directories.
# protoJS's conformance figure is the whole corpus (docs/TEST262_STATUS.md).
GATE_PATTERNS = "built-ins/Object,built-ins/Reflect,built-ins/Proxy"

# A result the runner classifies as neither a pass nor a skip is a failure for
# this gate's purposes, whatever its category.
PASSING = "passed"
SKIPPED = "skipped"


def failing_set(snapshot):
    """The sorted set of test paths that neither passed nor were skipped."""
    out = set()
    for r in snapshot.get("results", []):
        if r.get("result") not in (PASSING, SKIPPED):
            out.add(r["path"])
    return out


def run_suite(binary, corpus_root):
    """Run the three-directory gate sequentially and return the snapshot it wrote."""
    env = dict(os.environ)
    env["TEST262_ROOT"] = corpus_root
    # Sequential, always.  Parallel Test262 runs hang the development machine
    # (docs/TEST262_STATUS.md); this is a constraint, not a preference, and the
    # gate does not let a caller's stray TEST262_CONCURRENCY override it.
    env["TEST262_CONCURRENCY"] = "1"
    env["TEST262_PATTERNS"] = GATE_PATTERNS
    env["PROTOJS"] = binary
    before = set(os.listdir(REPORT_DIR)) if os.path.isdir(REPORT_DIR) else set()
    runner = os.path.join(REPO_ROOT, "tests", "test262", "runner", "test262_runner.js")
    # The runner exits 1 whenever anything fails, which is every run of a suite
    # at 93 %.  Its exit code is therefore not the verdict and is not consulted;
    # the snapshot is.
    subprocess.run(["node", runner], cwd=REPO_ROOT, env=env, check=False)
    after = set(os.listdir(REPORT_DIR)) if os.path.isdir(REPORT_DIR) else set()
    fresh = sorted(after - before)
    if not fresh:
        print("regression gate: the runner wrote no snapshot", file=sys.stderr)
        sys.exit(3)
    return os.path.join(REPORT_DIR, fresh[-1])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--snapshot", help="gate this snapshot instead of running the suite")
    ap.add_argument("--baseline", default=DEFAULT_BASELINE)
    ap.add_argument("--binary", default=os.path.join(REPO_ROOT, "build_release", "protojs"))
    ap.add_argument("--corpus", default="../test262")
    ap.add_argument("--update", action="store_true",
                    help="rewrite the baseline from this run (bank an improvement)")
    args = ap.parse_args()

    snap_path = args.snapshot or run_suite(args.binary, args.corpus)
    try:
        with open(snap_path, "r", encoding="utf-8") as fh:
            snapshot = json.load(fh)
    except (OSError, ValueError) as exc:
        print("regression gate: cannot read snapshot %s: %s" % (snap_path, exc),
              file=sys.stderr)
        return 3

    observed = failing_set(snapshot)
    denominator = snapshot.get("denominator")
    corpus_commit = snapshot.get("corpusCommit")
    passed = snapshot.get("summary", {}).get("passed")

    if args.update:
        baseline = {
            "//": "Expected Test262 failures for the per-commit regression gate. "
                  "Generated by tests/test262/runner/regression_gate.py --update. "
                  "Never edit by hand to make a red build green.",
            "patterns": GATE_PATTERNS.split(","),
            "corpusCommit": corpus_commit,
            "denominator": denominator,
            "passed": passed,
            "failures": sorted(observed),
        }
        with open(args.baseline, "w", encoding="utf-8") as fh:
            json.dump(baseline, fh, indent=2)
            fh.write("\n")
        print("regression gate: baseline rewritten -- %d passed of %d, %d expected failures"
              % (passed, denominator, len(observed)))
        return 0

    try:
        with open(args.baseline, "r", encoding="utf-8") as fh:
            baseline = json.load(fh)
    except (OSError, ValueError) as exc:
        print("regression gate: cannot read baseline %s: %s" % (args.baseline, exc),
              file=sys.stderr)
        return 3

    # --- the gate's own premise ------------------------------------------------
    premise_broken = []
    if baseline.get("corpusCommit") != corpus_commit:
        premise_broken.append(
            "corpus commit is %s, the baseline was recorded against %s. "
            "The corpus must be pinned: an unpinned Test262 moves the denominator "
            "and every diff below becomes unattributable."
            % (corpus_commit, baseline.get("corpusCommit")))
    if baseline.get("denominator") != denominator:
        premise_broken.append(
            "denominator is %s, the baseline was recorded at %s. The two sets are "
            "not comparable." % (denominator, baseline.get("denominator")))
    if premise_broken:
        print("=== Test262 regression gate: PREMISE BROKEN ===")
        for line in premise_broken:
            print("  " + line)
        return 2

    expected = set(baseline.get("failures", []))
    regressions = sorted(observed - expected)
    improvements = sorted(expected - observed)

    print("=== Test262 regression gate ===")
    print("patterns          : %s" % GATE_PATTERNS)
    print("corpus            : %s @ %s" % (snapshot.get("corpusRoot"), corpus_commit))
    print("binary            : %s" % snapshot.get("binary"))
    print("concurrency       : %s" % snapshot.get("concurrency"))
    print("passed            : %s of %s (baseline %s)"
          % (passed, denominator, baseline.get("passed")))
    print("expected failures : %d" % len(expected))
    print("observed failures : %d" % len(observed))
    print("regressions       : %d" % len(regressions))
    print("improvements      : %d" % len(improvements))

    for p in regressions:
        print("  REGRESSION (passed before, fails now): %s" % p)
    for p in improvements:
        print("  IMPROVEMENT (failed before, passes now): %s" % p)

    if regressions:
        print("")
        print("FAIL: %d test(s) that passed at the baseline now fail. Fix the "
              "regression; do not edit the baseline." % len(regressions))
        return 1
    if improvements:
        print("")
        print("FAIL: the baseline is stale -- %d test(s) now pass that it still "
              "lists as failing. Bank them in the same commit:\n"
              "    python3 tests/test262/runner/regression_gate.py --update\n"
              "A baseline allowed to drift stops detecting regressions, which is "
              "the only thing this gate is for." % len(improvements))
        return 1

    print("")
    print("PASS: the failure set is exactly the baseline (%d passed of %d)."
          % (passed, denominator))
    return 0


if __name__ == "__main__":
    sys.exit(main())
