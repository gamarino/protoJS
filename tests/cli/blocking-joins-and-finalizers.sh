#!/usr/bin/env bash
#
# CLI check: blocking joins leave protoCore's running set, and finalizers do not block.
#
# TWO DEFECTS, ONE MECHANISM.
#
# (1) A registered protoCore thread that blocks in a join without leaving the
# running set is still counted in `runningThreads`, so the stop-the-world quorum
# (`parkedThreads >= runningThreads`) can never be met, no collection cycle can
# start, and every thread that then needs memory waits for a cycle that cannot
# begin -- often including the thread being joined. That is a deadlock, not a slow
# shutdown, and it was read off a live backtrace in protoClojure. protoCore >= 2.3
# brackets its own `ProtoThread::join`, but a direct `std::thread::join` on a
# registered thread is still the embedder's problem, and protoCore's header says so
# in as many words.
#
# (2) A finalizer that blocks is the same failure from the other side. It runs on
# the single GC thread inside the sweep, so a wait there stalls collection for the
# whole space (protoCore/docs/GarbageCollector.md S7). Nothing can then reclaim, and
# every mutator waiting for memory waits forever. protoJS had five finalizers that
# joined OS threads and released `ProtoRootSet` pins -- and `ProtoRootSet::remove`
# takes the very mutex the collector holds during root collection, so it blocks the
# sweep on the collector's own lock.
#
# WHAT THIS FIXTURE DOES AND DOES NOT ESTABLISH, measured rather than assumed.
# protoClojure's equivalent fixture times out on every case against an unguarded
# build. protoJS's does not, and pretending otherwise would be the more comfortable
# lie: run against a build with every guard removed and every old finalizer body
# restored, five of the seven cases below still completed in about two seconds. The
# reason is that the deadlock needs a second thread wanting memory in the SAME space
# at the moment the first one blocks, and these scripts do not reliably arrange that
# coincidence -- the accept loops do not allocate protoCore cells at all, and a
# worker owns a separate space with its own quorum.
#
# So the discriminating evidence for these fixes lives in two places that DO go red
# on the unguarded build, deterministically:
#   * tests/unit/test_blocking_regions.cpp reads ProtoSpace::parkedThreads from
#     inside the blocking region and asserts the calling thread left the quorum.
#     Removing the guard from ThreadPoolExecutor::shutdown makes it report 0 >= 1.
#   * tests/cli/finalizers_do_not_block.py audits all five finalizer bodies, one
#     whole call graph deep. Restoring the original bodies makes it fail 5 of 5, with
#     the line and the call path for each.
# What THIS fixture adds is end-to-end coverage: that the reworked teardown paths
# still do their job in a real process under real memory pressure, that the value the
# program computed is right, and -- for the two deferred cases -- that the finalizer
# really handed its work to a mutator thread instead of doing it itself, which is an
# assertion the unguarded build fails.
#
# A HEAP CEILING IS WHAT MAKES COLLECTION HAPPEN AT ALL, because it is the only thing that
# forces a collection at all -- protoCore defers collection by design. So every case
# runs under PROTOCORE_HEAP_LIMIT_CELLS and every case CHECKS ITS OWN PREMISE: that
# collection cycles really ran, and that the most recently completed one really
# reclaimed cells, while the program was waiting. A case that passes without any of
# that happening did not exercise anything and is reported as a failed premise, not
# as a pass. And every case asserts the value its program computed, so a crash, a
# truncated run or a killed child cannot read as success.
#
# WHAT EACH CASE COVERS, and this mapping is the point of the fixture:
#
#   pool-shutdown-at-exit   ThreadPoolExecutor::shutdown -- its condition wait AND
#                           its join loop, reached from ~JSContextWrapper on the
#                           main thread, which IS registered (it constructed the
#                           space). Every case below also crosses this one; it gets
#                           its own case so a failure here is attributed here.
#   worker-terminate        WorkerState's owner relinquish path: terminate() joins
#                           the worker, releases its pin and destroys the worker's
#                           own ProtoSpace, all on the main thread. Reaches
#                           ThreadPoolExecutor::shutdown a second time, from inside
#                           ~JSContextWrapper of the WORKER's wrapper while running
#                           on the MAIN thread -- the case where parking the
#                           wrapper's own context would have been wrong.
#   worker-to-completion    A worker left to finish on its own, so the main thread
#                           blocks in the process drain loop while the worker
#                           allocates in its own space.
#   net-server-close        net.Server's relinquish path: close() joins the accept
#                           loop under an unmanaged region.
#   net-socket-destroy      net.Socket's relinquish path: destroy() joins a thread
#                           sitting in ::recv.
#   http-server-collected   The DEFERRED path, end to end. An http.Server dropped
#                           without close() is genuinely collectable (its pin holds
#                           the listener callback, not the server), so its finalizer
#                           really runs mid-program: it records the orphan and
#                           returns, and the event loop joins the accept loop and
#                           releases the pin. Asserts orphans-released >= 1, so the
#                           deferred path cannot be silently bypassed.
#   http-request-orphaned   The same deferred path for a finalizer with no thread at
#                           all: a request that never called end() still has a pin,
#                           and releasing a pin from the GC thread is itself a
#                           contract violation. Asserts orphans-released >= 1.
#
# NOT COVERED, and named rather than quietly omitted:
#   * ThreadPoolExecutor::shutdownNow has no caller in protoJS (only shutdown()
#     does), so it cannot be driven from a script. It is guarded for the same reason
#     and by the same mechanism, and that is all this fixture can say about it.
#   * The net.Server, net.Socket and Worker finalizers cannot run mid-program at
#     all: each pins the very object that carries its ExternalPointer, so the pin
#     keeps the owner reachable, the ExternalPointer is never swept, and the
#     finalizer that would release the pin never runs -- a self-sustaining root.
#     Their bodies execute only at space teardown. That is why those three cases
#     above drive the relinquish path (close/destroy/terminate) instead, and why
#     http's two cases carry the deferred half.
#
# The programs are generated into the scratch directory the caller passes, never
# /tmp, and the fixture writes nothing else.
#
# Usage: blocking-joins-and-finalizers.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: blocking-joins-and-finalizers.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: blocking-joins-and-finalizers.sh <protojs> <scratch-dir>}"

mkdir -p "$SCRATCH" || exit 1

LIMIT=500000         # cells; low enough that a 200k-iteration loop must collect
DEADLINE=90          # seconds; each case takes about 2 on a working build
MIN_RECLAIMED=100000

# Ports are fixed rather than ephemeral so a failure names the port. They are in the
# high private range and each case uses its own, so two cases never collide.
#
# EVERY CASE THAT OPENS A LISTENER PROVES IT OPENED, by printing LISTENING and having
# the shell require that line. Without it a bind that failed -- a port already in use on
# a shared runner -- would leave the case computing the right number and passing while
# exercising no accept loop at all: a test that cannot fail. net.Server reports its bound
# port through address(); http.Server has no address(), so its descriptor attribute
# `__fd__` is checked instead (3 when listening, -1 after close).
PORT_NET_SRV=18841
PORT_NET_SOCK=18842
PORT_HTTP=18843
PORT_HTTP_DEAD=18844   # deliberately nothing listening: the request is never sent

# The allocating tail every case shares, appended AFTER the case's own setup.
#
# `("garbage-" + i).length` is 8 plus the digits of i, so 200,000 iterations give
# exactly 2,688,890 -- arithmetic, not an observation.
#
# It is deliberately a TOP-LEVEL `let` loop rather than a loop inside a function,
# and that is measured rather than stylistic: at top level this loop drives 4
# collection cycles under the ceiling below, and the same loop wrapped in a function
# drives 1 and is not always enough to collect the objects cases 6 and 7 depend on.
# A case whose premise holds only sometimes is a flaky gate, so the form that holds
# every time is the one used. Three runs of each of the two deferred cases gave
# cycles=4, reclaimed-last=450584, orphans-released=1 identically.
HEAD_ALLOC='let acc = 0;
for (let i = 0; i < 120000; i++) { acc += ("garbage-" + i).length; }'
TAIL_ALLOC='for (let i = 120000; i < 200000; i++) { acc += ("garbage-" + i).length; }
console.log(acc);'
EXPECTED=2688890

# The worker program. Allocates enough to need collections of its own space.
cat > "$SCRATCH/worker_child.js" <<'EOF'
var acc = 0;
for (var i = 0; i < 200000; i++) { acc += ("garbage-" + i).length; }
EOF

failures=0
cases=0

# Each case is assembled as: setup, allocate 120,000, RELINQUISH, allocate the
# remaining 80,000, print. The relinquish step is deliberately in the middle: by then
# the heap is at its ceiling, so the allocation that follows the blocking call needs a
# collection cycle to proceed. A case that closed its resource only at the very end
# would block at a moment when nothing else wanted memory, and would complete even
# with the guard removed -- proving nothing.
#
# $1 case name, $2 minimum GC cycles (0 = cannot be established, see the worker
#   cases), $3 minimum orphans released (0 = do not assert), $4 setup,
#   $5 relinquish step
check() {
    local name="$1" min_cycles="$2" min_orphans="$3" program="$4" relinquish="${5:-}"
    local expected=$EXPECTED
    local out rc census cycles reclaimed released answer
    cases=$((cases + 1))
    printf '%s\n%s\n%s\n%s\n' \
        "$program" "$HEAD_ALLOC" "$relinquish" "$TAIL_ALLOC" \
        > "$SCRATCH/case_$name.js"
    out=$(timeout $DEADLINE env PROTOJS_GC_STATS=1 \
              PROTOCORE_HEAP_LIMIT_CELLS=$LIMIT \
              "$PROTOJS" "$SCRATCH/case_$name.js" 2>&1)
    rc=$?
    if [ $rc -eq 124 ]; then
        echo "FAIL [$name]: no progress in ${DEADLINE}s."
        echo "      Either a registered thread is blocking inside protoCore's"
        echo "      running set -- so the stop-the-world quorum can never be met and"
        echo "      no collection can start -- or a finalizer is blocking the sweep"
        echo "      on the GC thread. Every thread that needs memory then waits for a"
        echo "      cycle that cannot begin."
        failures=$((failures + 1)); return
    fi
    if [ $rc -ne 0 ]; then
        echo "FAIL [$name]: protojs exited $rc under PROTOCORE_HEAP_LIMIT_CELLS=$LIMIT"
        printf '%s\n' "$out" | tail -5
        failures=$((failures + 1)); return
    fi
    if printf '%s\n' "$out" | grep -qF 'LISTEN-FAILED'; then
        echo "FAIL [$name]: the listener did not bind, so no accept loop ran and the"
        echo "      case exercises nothing. A port in the 18841-18844 range is probably"
        echo "      already in use."
        printf '%s\n' "$out" | grep -F 'LISTEN-FAILED' | sed 's/^/      /'
        failures=$((failures + 1)); return
    fi
    answer=$(printf '%s\n' "$out" | grep -E '^[0-9]+$' | head -1)
    if [ "$answer" != "$expected" ]; then
        echo "FAIL [$name]: computed '$answer', expected $expected"
        printf '%s\n' "$out" | tail -5
        failures=$((failures + 1)); return
    fi
    census=$(printf '%s\n' "$out" | grep -F 'protojs gc:' | tail -1)
    cycles=$(printf '%s\n' "$census" | sed -n 's/.*cycles=\([0-9]*\).*/\1/p')
    reclaimed=$(printf '%s\n' "$census" | sed -n 's/.*reclaimed-last=\([0-9]*\).*/\1/p')
    released=$(printf '%s\n' "$census" | sed -n 's/.*orphans-released=\([0-9]*\).*/\1/p')
    if [ -z "$cycles" ] || [ -z "$reclaimed" ] || [ -z "$released" ]; then
        echo "FAIL [$name]: no usable census line, so there is no way to tell whether"
        echo "      the heap ceiling forced any collection. The case proves nothing."
        printf '%s\n' "$out" | tail -5
        failures=$((failures + 1)); return
    fi
    if [ "$min_cycles" -gt 0 ] && \
       { [ "$cycles" -lt "$min_cycles" ] || [ "$reclaimed" -lt "$MIN_RECLAIMED" ]; }; then
        echo "FAIL [$name]: premise not met -- $cycles cycles, $reclaimed cells"
        echo "      reclaimed by the last one (need $MIN_CYCLES and $MIN_RECLAIMED)."
        echo "      The case completed without any thread ever having to wait for"
        echo "      memory, so it did not exercise the blocking path at all."
        echo "      $census"
        failures=$((failures + 1)); return
    fi
    if [ "$min_orphans" -gt 0 ] && [ "$released" -lt "$min_orphans" ]; then
        echo "FAIL [$name]: premise not met -- $released orphans released, need"
        echo "      $min_orphans. The finalizer never handed anything to the"
        echo "      deferred path, so this case did not exercise it."
        echo "      $census"
        failures=$((failures + 1)); return
    fi
    echo "  ok [$name]: $census"
}

# 1. The pool shutdown every process crosses on its way out. The main thread is
#    registered, and ThreadPoolExecutor::shutdown waits on a condition variable and
#    then joins every worker. No setup and no relinquish step: reaching process exit
#    is the whole case.
check "pool-shutdown-at-exit" 1 0 \
    "// no setup: reaching process exit is the case" \
    "// no relinquish step"

# 2. Worker's owner relinquish path. terminate() joins the worker thread, releases
#    its root-set pin and destroys the worker's entire second ProtoSpace -- work
#    protoCore's S7 worked example names item by item as forbidden in a finalizer.
#    It also reaches ThreadPoolExecutor::shutdown from inside ~JSContextWrapper of
#    the WORKER's wrapper while running on the MAIN thread, which is the case where
#    parking the wrapper's own context would have parked the wrong thread.
#
#    Its cycle premise is 0, and that is measured, not a shrug. Joining the worker in
#    terminate() means g_activeWorkers is already zero when the process reaches its
#    drain loop, so the loop never iterates and the main thread never parks in a 10 ms
#    unmanaged sleep -- and in this configuration those sleeps are what let the main
#    space reach its stop-the-world quorum. Raising the allocation to 800,000
#    iterations still gave cycles=0, so the number cannot be tuned into place. Saying
#    so is better than asserting a premise that does not hold: what this case
#    establishes is that the reworked terminate() completes and computes the right
#    answer, and the parked-thread property it depends on is asserted deterministically
#    in tests/unit/test_blocking_regions.cpp instead.
check "worker-terminate" 0 0 \
    "const wt = require('worker_threads');
const w = new wt.Worker('$SCRATCH/worker_child.js');" \
    "w.terminate();"

# 3. A worker left to run to completion, so the main thread blocks in the process
#    drain loop while the worker is still allocating in its own space.
check "worker-to-completion" 1 0 \
    "const wt = require('worker_threads');
const w = new wt.Worker('$SCRATCH/worker_child.js');" \
    "// no relinquish step: the worker is left to finish on its own"

# 4. net.Server's relinquish path: close() joins the accept loop.
check "net-server-close" 1 0 \
    "const net = require('net');
const nsrv = net.createServer(function (c) { c.destroy(); });
nsrv.listen($PORT_NET_SRV);
if (!nsrv.address() || nsrv.address().port !== $PORT_NET_SRV) console.log('LISTEN-FAILED net $PORT_NET_SRV');
else console.log('LISTENING');" \
    "nsrv.close();"

# 5. net.Socket's relinquish path: destroy() joins a thread sitting in ::recv, which
#    is the least bounded of these joins.
check "net-socket-destroy" 1 0 \
    "const net = require('net');
const ssrv = net.createServer(function (c) { c.end(); });
ssrv.listen($PORT_NET_SOCK);
if (!ssrv.address() || ssrv.address().port !== $PORT_NET_SOCK) console.log('LISTEN-FAILED net $PORT_NET_SOCK');
else console.log('LISTENING');
const sock = net.connect($PORT_NET_SOCK, '127.0.0.1');" \
    "sock.destroy(); ssrv.close();"

# 6. THE DEFERRED PATH, END TO END, and the case with the sharpest teeth. The server
#    is closed and then becomes unreachable, so it really is collected during the run
#    and its finalizer really runs on the GC thread mid-sweep. If that finalizer
#    joins or touches the root set, the sweep stalls -- and the allocation that
#    follows needs the cycle that sweep belongs to. orphans-released >= 1 asserts the
#    finalizer handed the work over instead of doing it itself.
#
#    (Closed first on purpose: an http.Server left listening keeps the process drain
#    loop spinning by design, waiting for a close the script never makes. That is not
#    the defect under test.)
check "http-server-collected" 1 1 \
    "const http = require('http');
function mkServer() {
  const hsrv = http.createServer(function (req, res) { res.end('x'); });
  hsrv.listen($PORT_HTTP);
  // __fd__ is the listening descriptor: >= 0 while listening, -1 after close. http.Server
  // has no address(), so this is how the case proves an accept loop ever existed.
  if (hsrv.__fd__ === undefined || hsrv.__fd__ < 0) console.log('LISTEN-FAILED http $PORT_HTTP');
  else console.log('LISTENING');
  hsrv.close();
}
mkServer();" \
    "// no relinquish step: the point is that the script never releases it"

# 7. The same deferred path for a finalizer with no thread at all. A request that
#    never called end() still holds a root-set pin, and ProtoRootSet::remove takes
#    the very mutex the collector holds during root collection -- so releasing a pin
#    from the GC thread blocks the sweep on the collector's own lock.
check "http-request-orphaned" 1 1 \
    "const http = require('http');
function mkRequest() {
  http.request({ hostname: '127.0.0.1', port: $PORT_HTTP_DEAD, path: '/' },
               function (res) {});
}
mkRequest();" \
    "// no relinquish step: the request is dropped without end()"

if [ "$failures" -ne 0 ]; then
    echo "FAIL: $failures of $cases cases failed"
    exit 1
fi
echo "OK ($cases cases)"
