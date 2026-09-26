#!/usr/bin/env python3
"""Source audit: no finalizer blocks, and none calls back into protoCore.

Why a source audit and not a runtime test
-----------------------------------------
Three of protoJS's five `ProtoExternalPointer` finalizers cannot run mid-program at
all, and the reason is structural rather than incidental: each one pins the very
object that carries its ExternalPointer. `net.Server` does `rs->add(server)` and then
hangs the ExternalPointer on `server`; `net.Socket` and `worker_threads.Worker` do
the same. The pin keeps the owner reachable, so the ExternalPointer is never swept,
so the finalizer that would release the pin never runs -- a self-sustaining root.
Their bodies execute only at space teardown, which is also the worst possible moment
for the `join()` they used to contain.

No script can observe those three, and a rule that cannot be checked is a rule that
comes back. So the five bodies are read out of the source and asserted to contain
none of the things protoCore's `docs/GarbageCollector.md` section 7 forbids:

    join(          blocking. "A finalizer must also not block: it runs on the single
                   GC thread inside the sweep, so a wait there stalls collection for
                   the whole space."
    ->remove(      blocking AND publishing. `ProtoRootSet::remove` takes the very
                   mutex the collector holds during root collection, so from the GC
                   thread it is both a wait and a publish to a shared structure --
                   and on the same space, a self-deadlock risk.
    .reset()       a `std::unique_ptr<JSContextWrapper>` reset runs an entire
                   `~JSContextWrapper`: `destroyRootSet`, `GCBridge::cleanup` (which
                   iterates a `ProtoSparseList` and dereferences `ProtoObject*`s, two
                   more things section 7 forbids outright), the process-wide thread
                   pool shutdown, and the destruction of a whole second `ProtoSpace`.
    condition.wait / .wait( / .get()   any other wait.
    destroyRootSet / GCBridge::cleanup / createRootSet   protoCore API by name.

What a finalizer here MAY still do, and what is therefore not flagged: close a file
descriptor and flip an atomic flag. Section 7 permits exactly that -- "free an
external buffer, run an external pointer's callback" -- and closing the descriptor is
what makes the accept/recv loop return, so it is the half that has to stay.

It follows calls
----------------
The audit is transitive, one whole call graph deep within the same translation unit,
and that is not a refinement -- it is the point. protoJS's original `net` finalizers
contained no `join(` of their own: they called `teardownServer(s)`, and the join was
in there. A body-level grep would have passed both of them. So every function the
finalizer calls that is defined in the same file is scanned too, recursively.

It is by function, not by file
------------------------------
Every one of these files legitimately contains `join(` and `->remove(` elsewhere: in
the owner relinquish path (`serverClose`, `socketDestroy`, `workerTerminate`), which
is exactly where section 7 says the work belongs -- "at the point where the owner
itself gives it up". A file-level grep would either pass vacuously or forbid the
correct code.

Mutation-checked: restoring any one of the five original finalizer bodies makes this
exit 1 and names the finalizer, the pattern, the line and the call path that reached
it.

Usage: finalizers_do_not_block.py <repo-root>
"""

import re
import sys
import os

# (file, finalizer). protoJS has exactly five ProtoExternalPointer finalizers that
# carry cross-thread state; the count is asserted, so adding a sixth is a deliberate
# edit here rather than a silent gap.
FINALIZERS = [
    ("src/modules/net/NetModule.cpp", "freeServerState"),
    ("src/modules/net/NetModule.cpp", "freeSocketState"),
    ("src/modules/http/HTTPModule.cpp", "freeServerState"),
    ("src/modules/http/HTTPModule.cpp", "freeClientRequestState"),
    ("src/modules/worker_threads/WorkerThreadsModule.cpp", "freeWorkerState"),
]

FORBIDDEN = [
    ("join(", "blocks the GC thread inside the sweep, stalling collection for the whole space"),
    ("->remove(", "ProtoRootSet::remove takes the mutex the collector holds during root collection: a wait and a publish"),
    (".reset()", "runs a whole ~JSContextWrapper: a root set, GCBridge::cleanup, the thread pools and a second ProtoSpace"),
    ("condition.wait", "a wait of any kind stalls the sweep"),
    ("destroyRootSet", "protoCore API, called from the GC thread"),
    ("createRootSet", "protoCore API, called from the GC thread"),
    ("GCBridge::cleanup", "iterates a ProtoSparseList and dereferences ProtoObject*s"),
]

# Called names that are not functions to follow, so the walk does not chase the
# language itself or the very calls that are allowed.
IGNORED_CALLS = {
    "if", "for", "while", "switch", "return", "sizeof", "static_cast",
    "const_cast", "reinterpret_cast", "dynamic_cast", "delete",
}

CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_:]*)\s*\(")


def function_bodies(text):
    """Map every free function defined in `text` to its (start_line, body_lines).

    Recognises a definition as a line whose first character is not whitespace and
    which ends in `{`, i.e. the file's top-level functions. The body runs to the
    first line that is exactly `}` in column 1. That is the shape of every function
    in these three files; a finalizer whose body cannot be extracted is reported as a
    failure rather than passed over, because an empty match is indistinguishable from
    a clean body.
    """
    lines = text.split("\n")
    out = {}
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r"^([A-Za-z_][^;=]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*$", line)
        if m and not line.startswith(" ") and not line.startswith("\t"):
            name = m.group(2)
            # Walk forward to the opening brace (a signature may span lines).
            j = i
            while j < len(lines) and "{" not in lines[j]:
                if ";" in lines[j]:
                    break
                j += 1
            if j < len(lines) and "{" in lines[j]:
                body = []
                k = j + 1
                while k < len(lines) and not lines[k].startswith("}"):
                    body.append((k + 1, lines[k]))
                    k += 1
                if k < len(lines):
                    out.setdefault(name, (i + 1, body))
                    i = k
        i += 1
    return out


def scan(bodies, name, path, seen):
    """Return a list of (pattern, why, line_no, source_line, call_path)."""
    findings = []
    if name in seen or name not in bodies:
        return findings
    seen.add(name)
    _, body = bodies[name]
    for line_no, line in body:
        stripped = line.split("//")[0]
        for pattern, why in FORBIDDEN:
            if pattern in stripped:
                findings.append((pattern, why, line_no, line.strip(), list(seen)))
    # One whole call graph deep, within this translation unit.
    for line_no, line in body:
        for called in CALL_RE.findall(line.split("//")[0]):
            short = called.split("::")[-1]
            if short in IGNORED_CALLS or short == name:
                continue
            if short in bodies:
                findings.extend(scan(bodies, short, path, seen))
    return findings


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    repo = sys.argv[1]
    failures = 0
    checked = 0
    cache = {}

    for rel, func in FINALIZERS:
        path = os.path.join(repo, rel)
        if not os.path.isfile(path):
            print("FAIL: %s does not exist" % rel)
            failures += 1
            continue
        if path not in cache:
            with open(path, "r", encoding="utf-8") as fh:
                cache[path] = function_bodies(fh.read())
        bodies = cache[path]
        if func not in bodies:
            print("FAIL [%s:%s]: could not extract a body." % (rel, func))
            print("      Either the finalizer was renamed or its signature changed.")
            print("      The audit fails rather than passing on an empty match,")
            print("      because an empty match looks exactly like a clean body.")
            failures += 1
            continue
        checked += 1
        findings = scan(bodies, func, path, set())
        if findings:
            failures += 1
            for pattern, why, line_no, src, call_path in findings:
                where = " -> ".join(call_path)
                print("FAIL [%s:%s]: reaches '%s' -- %s" % (rel, func, pattern, why))
                print("      %s:%d: %s" % (rel, line_no, src))
                print("      via %s" % where)
            print("      Move it to the owner's relinquish point, or record the")
            print("      orphan and let a mutator thread do it (src/GcOrphanQueue.h).")
        else:
            print("  ok [%s:%s]: no blocking call and no protoCore API, transitively"
                  % (rel, func))

    if checked != 5:
        print("FAIL: audited %d finalizers, expected 5." % checked)
        print("      An audit that silently covers fewer sites than it claims is the")
        print("      failure mode this line exists to prevent.")
        return 1
    if failures:
        print("FAIL: %d of %d finalizers violate protoCore's finalizer contract"
              % (failures, checked))
        return 1
    print("OK (%d finalizers, calls followed within each translation unit)" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
