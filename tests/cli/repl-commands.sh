#!/usr/bin/env bash
#
# CLI check: the REPL evaluates input and obeys its dot-commands whatever the
# line terminator, and it ends at end of input.
#
# On Windows the standard streams are binary (src/main.cpp,
# prepareStandardStreams), so a line typed at a console or piped from a Windows
# program ends in "\r\n" and std::getline leaves the '\r' in the line. Before the
# fix ".exit" arrived as ".exit\r", matched no command, and the REPL printed
# "Unknown command" and kept reading; a blank line ("\r") was not blank either.
# The input below uses "\r\n" on every platform, so the check runs, and failed,
# on Linux too.
#
# Three cases:
#   crlf   "1+1\r\n\r\n.exit\r\n"  -> prints 2, says "Exiting REPL", exits 0
#   lf     "6*7\n.exit\n"          -> prints 42, exits 0
#   eof    "2+3\n" then end of input (no .exit) -> prints 5, exits 0. Before the
#          fix the loop read empty lines forever at end of input.
#
# Every run is bounded by `timeout` and by an output cap: a REPL that ignores
# .exit would otherwise hang the whole ctest invocation.
#
# Usage: repl-commands.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: repl-commands.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: repl-commands.sh <protojs> <scratch-dir>}"
mkdir -p "$SCRATCH" || exit 1

FAILED=0

# run_case <name> <printf-format-of-stdin> <expected-result-line>
run_case() {
    local name="$1" input="$2" expected="$3"
    local out="$SCRATCH/$name.out"
    # Options without a script start the REPL (README, "Command line").
    # The output is capped: a REPL that loops at end of input prints prompts
    # as fast as it can, and the cap ends it (SIGPIPE) instead of filling the
    # disk for 30 s.
    printf "$input" | timeout 30 "$PROTOJS" --cpu-threads 1 2>&1 | head -c 100000 > "$out"
    local status=${PIPESTATUS[1]}
    # Normalise any carriage return before matching, so the check is about
    # the REPL's behaviour, not about how a platform prints a newline.
    tr -d '\r' < "$out" > "$out.lf"
    if [ "$status" -eq 124 ]; then
        echo "FAIL [$name]: the REPL did not exit within 30 s"
        FAILED=1
    elif [ "$status" -ne 0 ]; then
        echo "FAIL [$name]: exit status $status"
        FAILED=1
    elif ! grep -qx "\(> \)*$expected" "$out.lf"; then
        echo "FAIL [$name]: no result line '$expected'"
        FAILED=1
    elif grep -q "Unknown command" "$out.lf"; then
        echo "FAIL [$name]: a dot-command was not recognised"
        FAILED=1
    else
        echo "ok   [$name]"
    fi
    if [ "$FAILED" -ne 0 ]; then
        echo "---- output of [$name] ----"
        head -c 2000 "$out" | cat -A
    fi
}

run_case crlf '1+1\r\n\r\n.exit\r\n' '2'
if [ "$FAILED" -eq 0 ] && ! grep -q "Exiting REPL" "$SCRATCH/crlf.out.lf"; then
    echo "FAIL [crlf]: .exit did not report 'Exiting REPL'"
    FAILED=1
fi
run_case lf '6*7\n.exit\n' '42'
run_case eof '2+3\n' '5'

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "PASS: the REPL honours .exit with CRLF and LF input and ends at end of input"
