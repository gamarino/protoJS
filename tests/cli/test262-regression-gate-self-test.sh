#!/usr/bin/env bash
#
# CLI check: the Test262 regression gate can actually fail.
#
# A gate that cannot go red is not a gate.  protoJS has been burned by exactly
# that: docs/CONFORMANCE.md records eight tests in this repository that could not
# fail, one of them a conformance rule that passed unconditionally.  So the gate
# added alongside this fixture gets its own adversarial test, and the test is
# adversarial in every direction the gate claims to cover:
#
#   1. the true snapshot against its own baseline           -> exit 0
#   2. a test that passed at the baseline now failing       -> exit 1, named
#   3. a test that failed at the baseline now passing       -> exit 1, named
#   4. the corpus at a different commit                     -> exit 2 (premise)
#   5. a different denominator                              -> exit 2 (premise)
#
# Cases 2 and 3 are the whole reason the gate is a set diff rather than a
# percentage: mutate one result each way and the pass rate is unchanged to two
# decimal places, so a percentage gate reports both of them green.  This fixture
# asserts that this gate does not.
#
# Case 4 is the fault that was corrected in this repository hours before the gate
# existed: without a pinned corpus the denominator moves with upstream, and a
# diff against a moving denominator is unattributable.
#
# Every case asserts the exit code AND a substring of the verdict, so a gate that
# exits non-zero for the wrong reason -- a crash, a missing file, a traceback --
# cannot read as a pass.
#
# Usage: test262-regression-gate-self-test.sh <repo-root> <scratch-dir>
set -u

REPO="${1:?usage: test262-regression-gate-self-test.sh <repo-root> <scratch-dir>}"
SCRATCH="${2:?usage: test262-regression-gate-self-test.sh <repo-root> <scratch-dir>}"
GATE="$REPO/tests/test262/runner/regression_gate.py"

mkdir -p "$SCRATCH" || exit 1
failures=0

# A miniature corpus: three passes, two failures of different categories and one
# skip.  Small on purpose -- the gate's logic is set arithmetic, and six entries
# exercise every branch of it.  The skip must be excluded from the failure set
# without being counted as a pass, which is its own past bug in this family.
write_snapshot() {   # $1 out, $2 commit, $3 denominator, $4.. "path:result"
    local out="$1" commit="$2" denom="$3"; shift 3
    local first=1
    {
        printf '{\n  "corpusRoot": "../test262",\n'
        printf '  "corpusCommit": "%s",\n' "$commit"
        printf '  "binary": "build_release/protojs",\n  "concurrency": 1,\n'
        printf '  "denominator": %s,\n' "$denom"
        printf '  "summary": {"passed": 3},\n  "results": [\n'
        for spec in "$@"; do
            [ $first -eq 1 ] || printf ',\n'
            first=0
            printf '    {"path": "%s", "result": "%s"}' "${spec%%:*}" "${spec##*:}"
        done
        printf '\n  ]\n}\n'
    } > "$out"
}

PIN=aae8cf6eed6d6c6a203be48c1184bb194880f66b
BASE_ARGS=(
    "a/pass1.js:passed" "a/pass2.js:passed" "a/pass3.js:passed"
    "a/fail_sem.js:failed_semantics" "a/fail_syn.js:failed_syntax"
    "a/skipped.js:skipped"
)

write_snapshot "$SCRATCH/truth.json" "$PIN" 5 "${BASE_ARGS[@]}"
python3 "$GATE" --snapshot "$SCRATCH/truth.json" --baseline "$SCRATCH/baseline.json" \
        --update > "$SCRATCH/update.log" 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL [setup]: --update did not succeed"; cat "$SCRATCH/update.log"; exit 1
fi
# The baseline must list exactly the two failures and NOT the skip.
if ! grep -q 'a/fail_sem.js' "$SCRATCH/baseline.json" \
   || ! grep -q 'a/fail_syn.js' "$SCRATCH/baseline.json"; then
    echo "FAIL [setup]: baseline does not list both failures"; exit 1
fi
if grep -q 'a/skipped.js' "$SCRATCH/baseline.json"; then
    echo "FAIL [setup]: a skipped test was recorded as an expected failure"
    exit 1
fi

# $1 case name, $2 snapshot, $3 expected exit, $4 expected substring
check() {
    local name="$1" snap="$2" want_rc="$3" want_text="$4" out rc
    out=$(python3 "$GATE" --snapshot "$snap" --baseline "$SCRATCH/baseline.json" 2>&1)
    rc=$?
    if [ "$rc" -ne "$want_rc" ]; then
        echo "FAIL [$name]: exit $rc, expected $want_rc"
        printf '%s\n' "$out" | tail -8
        failures=$((failures + 1)); return
    fi
    if ! printf '%s\n' "$out" | grep -qF -- "$want_text"; then
        echo "FAIL [$name]: verdict did not mention '$want_text'"
        printf '%s\n' "$out" | tail -8
        failures=$((failures + 1)); return
    fi
    echo "  ok [$name]: exit $rc, verdict mentions '$want_text'"
}

# 1. Unchanged.
check "unchanged" "$SCRATCH/truth.json" 0 "the failure set is exactly the baseline"

# 2. One test that passed now fails.  Pass count drops by one; a percentage gate
#    would call this a 0.02-point wobble.
write_snapshot "$SCRATCH/regressed.json" "$PIN" 5 \
    "a/pass1.js:failed_semantics" "a/pass2.js:passed" "a/pass3.js:passed" \
    "a/fail_sem.js:failed_semantics" "a/fail_syn.js:failed_syntax" "a/skipped.js:skipped"
check "regression" "$SCRATCH/regressed.json" 1 "REGRESSION (passed before, fails now): a/pass1.js"

# 3. One test that failed now passes, and one that passed now fails: the pass
#    count, the denominator and therefore the pass rate are all IDENTICAL to the
#    baseline.  This is the case a percentage gate cannot see at all.
write_snapshot "$SCRATCH/swapped.json" "$PIN" 5 \
    "a/pass1.js:failed_semantics" "a/pass2.js:passed" "a/pass3.js:passed" \
    "a/fail_sem.js:passed" "a/fail_syn.js:failed_syntax" "a/skipped.js:skipped"
check "swap-same-rate" "$SCRATCH/swapped.json" 1 "REGRESSION (passed before, fails now): a/pass1.js"

# 4. Pure improvement: still a failure, because a baseline left stale stops
#    detecting case 2.
write_snapshot "$SCRATCH/improved.json" "$PIN" 5 \
    "a/pass1.js:passed" "a/pass2.js:passed" "a/pass3.js:passed" \
    "a/fail_sem.js:passed" "a/fail_syn.js:failed_syntax" "a/skipped.js:skipped"
check "improvement" "$SCRATCH/improved.json" 1 "the baseline is stale"

# 5. Corpus moved: premise broken, a different exit code because it needs a
#    different fix (re-pin the corpus, not fix the engine).
write_snapshot "$SCRATCH/othercommit.json" "0000000000000000000000000000000000000000" 5 "${BASE_ARGS[@]}"
check "corpus-not-pinned" "$SCRATCH/othercommit.json" 2 "The corpus must be pinned"

# 6. Denominator moved with the corpus.
write_snapshot "$SCRATCH/otherdenom.json" "$PIN" 7 "${BASE_ARGS[@]}"
check "denominator-moved" "$SCRATCH/otherdenom.json" 2 "not comparable"

if [ "$failures" -ne 0 ]; then
    echo "FAIL: $failures of 6 regression-gate cases failed"
    exit 1
fi
echo OK
