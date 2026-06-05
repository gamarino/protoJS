#!/bin/bash
# Run a sample list of test262 paths and report failures with last-line of output
while read -r p; do
  out=$(node .r10/repro.js "$p" 2>&1)
  rc=$?
  if [ $rc -ne 0 ]; then
    last=$(echo "$out" | tail -1)
    echo "[FAIL $rc] $p | $last"
  fi
done < "$1"
