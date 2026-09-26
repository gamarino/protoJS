#!/usr/bin/env bash
#
# CLI check: a native method cell is never bound to the object that holds it.
#
# `ProtoContext::fromMethod(self, fn)` takes a STRONG, TRACED reference to
# `self`: ProtoMethodCell::processReferences reports it unconditionally, so the
# cell keeps `self` alive for as long as the cell is reachable
# (protoCore/headers/protoCore.h, the fromMethod comment). Installing that cell
# as an attribute OF `self` therefore closes a reference cycle through a MUTABLE
# object, and a cycle among mutables is never collected: the mutables table
# originates marking, and an entry is released only once its handle has been
# finalized (protoCore/docs/MemoryModel.md S7).
#
# protoJS did exactly that in installNonEnumerableMethod -- `fromMethod(methodObj,
# fn)` stored under `__native_fn__` on methodObj -- for about 140 of the 177
# cycles a bare `protojs -e 1` process carried. The binding was provably dead:
# protoJS has zero asMethodSelf call sites and all 24 asMethod sites pass the real
# receiver, so nothing could ever read it. FunctionPrototype.cpp already did the
# identical job with nullptr.
#
# THE MEMORY SAVED IS ROUGHLY NOTHING. Those method objects hang off globalThis
# and are perennial anyway. That is not the reason for this check. The reason is
# that a detector reporting 140 known-benign cycles per process is noise, and a
# noisy detector gets switched off -- so the harmful cycles it would have caught
# go unseen. Clearing the benign is what makes the harmful visible, and this
# fixture is what stops the benign coming back.
#
# THE CHECK VERIFIES ITS OWN PREMISE. A scan that saw nothing reports an empty
# cycle list and looks exactly like a clean graph -- protoCore's
# MutableGraphReport carries `handles`, `handleReferences` and a `truncated` flag
# for precisely that reason, and its own comment says a report whose numbers are
# all zero is a broken scan that looks like a clean graph. So this fixture asserts
# a non-zero handle count, a non-zero handle-reference count and a COMPLETE (not
# truncated) scan before it believes the zero it is looking for.
#
# The 37 remaining cycles are NOT asserted to be zero. They are the
# prototype/constructor back-references the JS object model requires
# (`X.prototype.constructor === X`), and declaring them is conformance rule 13's
# job, not this fixture's. What is asserted is that the count of the one kind that
# is pure retention is zero.
#
# PROTOCORE_MUTABLE_CYCLE_CHECK is pointed at a FILE, not left to default to
# stderr: a census on stderr breaks every suite that diffs its output.
#
# Usage: no-method-self-cycles.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: no-method-self-cycles.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: no-method-self-cycles.sh <protojs> <scratch-dir>}"

mkdir -p "$SCRATCH" || exit 1
CENSUS="$SCRATCH/cycles.txt"
rm -f "$CENSUS"

# The smallest program that still builds the whole global object: every builtin
# prototype is installed before the first expression is evaluated, which is where
# the cells under test are created.
if ! PROTOCORE_MUTABLE_CYCLE_CHECK="$CENSUS" "$PROTOJS" -e '1' > "$SCRATCH/out.txt" 2>&1; then
    echo "FAIL: protojs -e 1 exited non-zero"
    tail -5 "$SCRATCH/out.txt"
    exit 1
fi

if [ ! -s "$CENSUS" ]; then
    echo "FAIL: no census written to $CENSUS."
    echo "      Either PROTOCORE_MUTABLE_CYCLE_CHECK is not honoured by the"
    echo "      protoCore this binary loaded (it needs 2.5.0 or later), or the"
    echo "      ProtoSpace was never torn down. Without a census this check"
    echo "      proves nothing, so it fails rather than passing vacuously."
    exit 1
fi

HEADER=$(grep -m1 'mutable-cycle scan' "$CENSUS")
if [ -z "$HEADER" ]; then
    echo "FAIL: census file has no scan header; cannot read its premise"
    head -5 "$CENSUS"
    exit 1
fi

HANDLES=$(printf '%s\n' "$HEADER" | sed -n 's/.*scan: \([0-9]*\) handles.*/\1/p')
REFS=$(printf '%s\n' "$HEADER" | sed -n 's/.*, \([0-9]*\) references to a mutable handle.*/\1/p')

# Premise 1: the scan looked at a real object graph.
if [ -z "$HANDLES" ] || [ -z "$REFS" ] || [ "$HANDLES" -lt 100 ] || [ "$REFS" -lt 100 ]; then
    echo "FAIL: premise not met -- ${HANDLES:-?} handles, ${REFS:-?} handle references."
    echo "      A protojs process builds hundreds of mutable prototypes, so these"
    echo "      numbers being small means the scan saw nothing and its empty"
    echo "      cycle list would be meaningless."
    echo "      $HEADER"
    exit 1
fi

# Premise 2: the scan finished. protoCore's own comment: a cycle found by a
# truncated scan is still real, but the ABSENCE of cycles is not established.
if printf '%s\n' "$HEADER" | grep -q 'truncated'; then
    echo "FAIL: premise not met -- the scan was truncated by its cell budget, so"
    echo "      an absence of cycles is not established."
    echo "      $HEADER"
    exit 1
fi

TOTAL=$(grep -c '^  CYCLE' "$CENSUS" || true)
SELF=$(grep -c '__native_fn__ > MethodCell' "$CENSUS" || true)

echo "  census: $HEADER"
echo "  cycles: $TOTAL total, $SELF of them a native method cell bound to its own holder"

if [ "$SELF" -ne 0 ]; then
    echo "FAIL: $SELF native method cell(s) are bound to the object that holds them."
    echo "      Each one is a permanent reference cycle through a mutable, and each"
    echo "      one is pure retention: protoJS never reads the binding (zero"
    echo "      asMethodSelf call sites). Pass nullptr as fromMethod's self."
    grep -m3 '__native_fn__ > MethodCell' "$CENSUS" | sed 's/^/      /'
    exit 1
fi

echo "OK"
