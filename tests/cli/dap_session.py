#!/usr/bin/env python3
"""`protojs --dap`: debug sessions driven the way VS Code drives them.

Usage: dap_session.py <protojs> <scratch-dir>

Each case starts `protojs --dap`, speaks the Debug Adapter Protocol to it over
its stdin/stdout and checks what comes back: breakpoints (verified once the
file is loaded), stops, the call stack, variables, evaluate, stepping in, over
and out, stopOnEntry, conditional breakpoints, exception breakpoints (All Exceptions,
Uncaught Exceptions, exceptionInfo, no stop on the engine's own control flow),
process.exit, and the
program's output arriving as `output` events -- including more than 64 KB of
non-ASCII text and a child process that reads stdin, neither of which may
disturb the protocol.  Paths are absolute, and on Windows spelled with a lower
case drive letter, as VS Code sends them.

A watchdog bounds every wait: an adapter that stops answering fails the case
instead of hanging the suite.
"""

import json
import os
import subprocess
import sys
import threading
import time
import queue

PROTOJS = sys.argv[1]
SCRATCH = sys.argv[2]
TIMEOUT = 60.0

failures = []


def vscode_path(p):
    p = os.path.abspath(p)
    if os.name == "nt" and len(p) > 1 and p[1] == ":":
        p = p[0].lower() + p[1:]
    return p


def write(name, text):
    path = os.path.join(SCRATCH, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return vscode_path(path)


class Adapter:
    def __init__(self):
        self.p = subprocess.Popen([PROTOJS, "--dap"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.seq = 0
        self.messages = queue.Queue()
        self.output = {"stdout": "", "stderr": ""}
        self.events = []
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._drain_stderr, daemon=True).start()
        self.stderr = b""

    def _drain_stderr(self):
        self.stderr = self.p.stderr.read()

    def _read(self):
        out = self.p.stdout
        while True:
            header = b""
            while not header.endswith(b"\r\n\r\n"):
                c = out.read(1)
                if not c:
                    self.messages.put(None)
                    return
                header += c
            length = 0
            for line in header.decode("ascii").split("\r\n"):
                if line.lower().startswith("content-length:"):
                    length = int(line.split(":", 1)[1])
            body = out.read(length)
            if len(body) != length:
                self.messages.put(None)
                return
            self.messages.put(json.loads(body.decode("utf-8")))

    def send(self, command, arguments=None):
        self.seq += 1
        msg = {"seq": self.seq, "type": "request", "command": command,
               "arguments": arguments or {}}
        data = json.dumps(msg).encode("utf-8")
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
        self.p.stdin.flush()
        return self.seq

    def next_message(self, deadline):
        left = deadline - time.time()
        if left <= 0:
            raise TimeoutError("no answer from the adapter")
        try:
            m = self.messages.get(timeout=left)
        except queue.Empty:
            raise TimeoutError("no answer from the adapter")
        if m is None:
            raise EOFError("the adapter closed its output")
        if m.get("type") == "event":
            self.events.append(m)
            if m["event"] == "output":
                cat = m["body"].get("category", "console")
                self.output[cat] = self.output.get(cat, "") + m["body"]["output"]
        return m

    def request(self, command, arguments=None):
        seq = self.send(command, arguments)
        deadline = time.time() + TIMEOUT
        while True:
            m = self.next_message(deadline)
            if m.get("type") == "response" and m.get("request_seq") == seq:
                return m

    def wait_event(self, name):
        deadline = time.time() + TIMEOUT
        # An event that already arrived (while waiting for a response).
        for i, e in enumerate(self.events):
            if e["event"] == name and not e.get("_seen"):
                e["_seen"] = True
                return e
        while True:
            m = self.next_message(deadline)
            if m.get("type") == "event" and m["event"] == name:
                m["_seen"] = True
                return m

    def start(self, program, breakpoints=None, stop_on_entry=False, conditions=None,
              exception_filters=None):
        r = self.request("initialize", {"adapterID": "protojs", "pathFormat": "path",
                                        "linesStartAt1": True, "columnsStartAt1": True})
        check(r["success"], "initialize succeeds")
        self.capabilities = r.get("body", {})
        self.wait_event("initialized")
        r = self.request("launch", {"program": program, "stopOnEntry": stop_on_entry,
                                    "cwd": os.path.dirname(program)})
        check(r["success"], "launch succeeds")
        bps = []
        for line in breakpoints or []:
            b = {"line": line}
            if conditions and line in conditions:
                b["condition"] = conditions[line]
            bps.append(b)
        r = self.request("setBreakpoints", {"source": {"path": program}, "breakpoints": bps})
        check(r["success"], "setBreakpoints succeeds")
        if exception_filters is not None:
            e = self.request("setExceptionBreakpoints", {"filters": exception_filters})
            check(e["success"] and len(e["body"]["breakpoints"]) == len(exception_filters),
                  "setExceptionBreakpoints %s succeeds" % exception_filters)
        self.request("configurationDone")
        return r

    def finish(self):
        self.wait_event("terminated")
        exited = self.wait_event("exited")
        try:
            self.request("disconnect", {})
        except (EOFError, TimeoutError, OSError):
            pass
        try:
            self.p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.p.kill()
            check(False, "the adapter exits after disconnect")
        return exited["body"]["exitCode"]

    def kill(self):
        if self.p.poll() is None:
            self.p.kill()
        try:
            self.p.stdin.close()
        except OSError:
            pass


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL:", what)
    else:
        print("ok:", what)


def stack(a):
    return a.request("stackTrace", {"threadId": 1})["body"]["stackFrames"]


def variables(a, frame_id, scope="Locals"):
    scopes = a.request("scopes", {"frameId": frame_id})["body"]["scopes"]
    ref = [s for s in scopes if s["name"] == scope]
    if not ref:
        return {}
    vs = a.request("variables", {"variablesReference": ref[0]["variablesReference"]})
    return {v["name"]: v for v in vs["body"]["variables"]}


def evaluate(a, expr, frame_id):
    r = a.request("evaluate", {"expression": expr, "frameId": frame_id, "context": "repl"})
    return r["body"]["result"] if r["success"] else "ERROR: " + r.get("message", "")


def run_case(name, fn):
    print("--- " + name)
    a = Adapter()
    try:
        fn(a)
    except Exception as e:  # noqa: BLE001 -- any failure fails the case
        check(False, "%s: %s: %s" % (name, type(e).__name__, e))
    finally:
        a.kill()
        if a.stderr:
            print("adapter stderr:", a.stderr.decode("utf-8", "replace")[:2000])


BIG_UNIT = "ñandú-€-"   # 9 characters, 13 bytes of UTF-8
BIG_COUNT = 6000                      # 78 KB

MAIN = """const path = require('path');
const big = "%s".repeat(%d);
function add(a, b) {
  const sum = a + b;
  return sum;
}
let total = 0;
console.log("before");
console.log(big);
total = add(2, 3);
console.log("after", total);
const child = require('child_process');
child.exec(path.sep === '\\\\' ? 'findstr "^"' : 'cat', function () {});
const obj = { name: "box", items: [1, 2, 3] };
console.log("done", obj.name);
""" % (BIG_UNIT, BIG_COUNT)


def case_breakpoints(a):
    prog = write("main.js", MAIN)
    r = a.start(prog, breakpoints=[4, 15])
    bps = r["body"]["breakpoints"]
    check(len(bps) == 2, "two breakpoints answered")
    stopped = a.wait_event("stopped")
    check(stopped["body"]["reason"] == "breakpoint", "stops at a breakpoint")
    frames = stack(a)
    check(frames[0]["name"] == "add" and frames[0]["line"] == 4, "top frame is add at line 4")
    check(frames[0].get("source", {}).get("path") == prog, "frame source is the launched path")
    check(len(frames) >= 2 and frames[1]["line"] == 10, "caller frame at line 10")
    check("before" in a.output["stdout"], "output before the stop arrived")
    loc = variables(a, frames[0]["id"])
    check(loc.get("a", {}).get("value") == "2" and loc.get("b", {}).get("value") == "3",
          "locals a=2 b=3")
    check("sum" in loc, "local sum listed")
    check(evaluate(a, "a + b", frames[0]["id"]) == "5", "evaluate a + b in add")
    check(evaluate(a, "total", frames[1]["id"]) == "0", "evaluate total in the caller")
    check(evaluate(a, "big.length", frames[1]["id"]) == str(len(BIG_UNIT) * BIG_COUNT),
          "evaluate big.length")
    check(evaluate(a, "nope(", frames[0]["id"]).startswith("ERROR"), "a syntax error is an error")
    check(evaluate(a, "undefinedThing.x", frames[0]["id"]).startswith("ERROR"),
          "a thrown error is an error")

    a.request("next", {"threadId": 1})
    s = a.wait_event("stopped")
    frames = stack(a)
    check(s["body"]["reason"] == "step" and frames[0]["line"] == 5, "next stops at line 5")
    loc = variables(a, frames[0]["id"])
    check(loc.get("sum", {}).get("value") == "5", "sum is 5 after the step")

    a.request("stepOut", {"threadId": 1})
    a.wait_event("stopped")
    frames = stack(a)
    check(frames[0]["name"] == "<toplevel>" and frames[0]["line"] == 11,
          "stepOut returns to the caller's next line")
    script = variables(a, frames[0]["id"], "Script")
    check(script.get("total", {}).get("value") == "5", "script binding total is 5")

    a.request("continue", {"threadId": 1})
    a.wait_event("stopped")
    frames = stack(a)
    check(frames[0]["line"] == 15, "continue reaches the breakpoint at line 15")
    script = variables(a, frames[0]["id"], "Script")
    o = script.get("obj")
    check(o is not None and o["variablesReference"] > 0, "obj is expandable")
    if o:
        kids = a.request("variables", {"variablesReference": o["variablesReference"]})
        names = {v["name"]: v for v in kids["body"]["variables"]}
        check(names.get("name", {}).get("value") == '"box"', "obj.name is \"box\"")
        check("items" in names and names["items"]["variablesReference"] > 0, "obj.items expands")

    a.request("continue", {"threadId": 1})
    code = a.finish()
    check(code == 0, "exit code 0")
    out = a.output["stdout"]
    check("after 5" in out and "done box" in out, "output after the stops arrived")
    check(out.count(BIG_UNIT) == BIG_COUNT, "more than 64 KB of non-ASCII output intact")
    changed = [e for e in a.events if e["event"] == "breakpoint"]
    check(any(e["body"]["breakpoint"].get("verified") for e in changed) or
          all(b.get("verified") for b in bps), "breakpoints verified once the file loads")


def case_stop_on_entry(a):
    prog = write("entry.js", "let x = 1;\nx = x + 1;\nconsole.log('x is', x);\n")
    a.start(prog, stop_on_entry=True)
    s = a.wait_event("stopped")
    frames = stack(a)
    check(s["body"]["reason"] == "entry" and frames[0]["line"] == 1, "stopOnEntry stops at line 1")
    a.request("next", {"threadId": 1})
    a.wait_event("stopped")
    check(stack(a)[0]["line"] == 2, "next goes to line 2")
    a.request("stepIn", {"threadId": 1})
    a.wait_event("stopped")
    check(stack(a)[0]["line"] == 3, "stepIn goes to line 3")
    a.request("continue", {"threadId": 1})
    check(a.finish() == 0, "exit code 0")
    check("x is 2" in a.output["stdout"], "program output")


def case_condition(a):
    prog = write("loop.js", "let acc = 0;\nfor (let i = 0; i < 6; i++) {\n  acc += i;\n}\nconsole.log(acc);\n")
    a.start(prog, breakpoints=[3], conditions={3: "i === 4"})
    a.wait_event("stopped")
    frames = stack(a)
    check(evaluate(a, "i", frames[0]["id"]) == "4", "conditional breakpoint stops when i === 4")
    a.request("continue", {"threadId": 1})
    check(a.finish() == 0, "exit code 0")
    check("15" in a.output["stdout"], "loop output")


def case_module(a):
    lib = write("lib.js", "function mul(x, y) {\n  const p = x * y;\n  return p;\n}\nmodule.exports = { mul };\n")
    prog = write("usemod.js", "const lib = require('./lib');\nconst r = lib.mul(6, 7);\nconsole.log('r', r);\n")
    a.request("initialize", {})
    a.wait_event("initialized")
    a.request("launch", {"program": prog})
    r = a.request("setBreakpoints", {"source": {"path": lib}, "breakpoints": [{"line": 2}]})
    check(r["body"]["breakpoints"][0]["verified"] is False, "a breakpoint in a file not loaded yet is pending")
    a.request("configurationDone")
    a.wait_event("stopped")
    frames = stack(a)
    check(frames[0]["name"] == "mul" and frames[0]["line"] == 2 and
          frames[0].get("source", {}).get("path") == lib, "stops in the required module at line 2")
    check(frames[1]["line"] == 2 and frames[1].get("source", {}).get("path") == prog,
          "the caller is the main script at line 2")
    loc = variables(a, frames[0]["id"])
    check(loc.get("x", {}).get("value") == "6" and loc.get("y", {}).get("value") == "7",
          "module function arguments")
    a.request("stepOut", {"threadId": 1})
    a.wait_event("stopped")
    check(stack(a)[0]["line"] == 3, "stepOut back in the main script")
    a.request("continue", {"threadId": 1})
    check(a.finish() == 0, "exit code 0")
    check("r 42" in a.output["stdout"], "module output")


def case_exit(a):
    prog = write("exit.js", "console.log('bye');\nprocess.exit(3);\n")
    a.start(prog)
    check(a.finish() == 3, "process.exit(3) reports exit code 3")
    check("bye" in a.output["stdout"], "output before process.exit")


def case_uncaught(a):
    prog = write("throw.js", "console.log('start');\nthrow new Error('boom');\n")
    a.start(prog)
    code = a.finish()
    check(code != 0, "an uncaught error ends with a failure status")
    check("boom" in a.output["stderr"], "the error is reported on stderr")


RAISED = """function risky(n) {
  const local = n * 2;
  throw new TypeError('bad ' + local);
}
let caught = null;
try {
  risky(21);
} catch (e) {
  caught = e.message;
}
console.log('caught', caught);
"""


def no_stop(a):
    """Runs to the end; True when no `stopped` event arrived."""
    a.finish()
    return not [e for e in a.events if e["event"] == "stopped"]


def case_exception_raised(a):
    prog = write("raised.js", RAISED)
    a.start(prog, exception_filters=["raised"])
    filters = {f["filter"]: f for f in a.capabilities.get("exceptionBreakpointFilters", [])}
    check("raised" in filters and "uncaught" in filters, "initialize advertises raised and uncaught")
    check(a.capabilities.get("supportsExceptionInfoRequest") is True, "supportsExceptionInfoRequest")
    s = a.wait_event("stopped")
    check(s["body"]["reason"] == "exception", "raised: stops with reason exception")
    check(s["body"].get("text") == "TypeError: bad 42", "raised: text is the class and message")
    frames = stack(a)
    check(frames[0]["name"] == "risky" and frames[0]["line"] == 3, "raised: stops at the throw, line 3")
    check(len(frames) >= 2 and frames[1]["line"] == 7, "raised: the caller is at line 7")
    loc = variables(a, frames[0]["id"])
    check(loc.get("n", {}).get("value") == "21" and loc.get("local", {}).get("value") == "42",
          "raised: locals n=21 local=42 intact")
    check(evaluate(a, "local + 1", frames[0]["id"]) == "43", "raised: evaluate in the throwing frame")
    info = a.request("exceptionInfo", {"threadId": 1})
    b = info.get("body", {})
    check(info["success"] and b.get("exceptionId") == "TypeError" and b.get("breakMode") == "always",
          "exceptionInfo: TypeError, breakMode always")
    check(b.get("details", {}).get("message") == "bad 42" and
          "risky" in b.get("details", {}).get("stackTrace", ""), "exceptionInfo details")
    a.request("continue", {"threadId": 1})
    check(a.finish() == 0, "raised: the catch runs and the program ends normally")
    check("caught bad 42" in a.output["stdout"], "raised: the catch saw the exception")
    stops = [e for e in a.events if e["event"] == "stopped"]
    check(len(stops) == 1, "raised: one stop only")


INTERNAL = """function* gen() { try { yield 1; yield 2; } finally { } }
let s = 0;
for (const x of gen()) { s += x; }
for (const x of gen()) { break; }
for (const [k, v] of new Map([[1, 2]])) s += k + v;
const [a, b, ...rest] = [1, 2, 3, 4];
s += [...new Set([1, 2])].length + rest.length;
const it = [1, 2, 3][Symbol.iterator]();
for (const x of it) { if (x === 2) break; }
Array.from({ length: 3 }, (_, i) => i).forEach(x => { s += x; });
JSON.parse('{"a": [1, 2]}');
Reflect.defineProperty(Object.freeze({}), 'x', { value: 1 });
console.log('bigint', 10n == 'abc');
outer: for (;;) { try { break outer; } finally { s += 0; } }
function early() { try { return 1; } finally { s += 0; } }
early();
Promise.reject(new Error('rejected, never thrown')).catch(() => {});
async function f() { await null; for await (const x of [1, 2]) s += x; return 5; }
f().then(v => { s += v; console.log('sum', s); });
"""


def case_exception_internal(a):
    prog = write("internal.js", INTERNAL)
    a.start(prog, exception_filters=["raised"])
    check(no_stop(a), "raised: no stop on iteration, generators, finally, break, return, promises")
    check("sum" in a.output["stdout"] and "bigint false" in a.output["stdout"],
          "internal-flow program ran to the end")


THROWS = """function f() { throw new Error('one'); }
try { f(); } catch (e) { console.log('got', e.message); }
throw new RangeError('two');
"""


def case_exception_off(a):
    prog = write("off.js", THROWS)
    a.start(prog, exception_filters=[])
    code = a.finish()
    check(not [e for e in a.events if e["event"] == "stopped"], "filters off: no exception stop")
    check(code != 0 and "two" in a.output["stderr"], "filters off: the uncaught error ends the program")


UNCAUGHT = """function inner() { try { throw new Error('caught one'); } catch (e) { return e; } }
function outer(v) {
  inner();
  const w = v + 1;
  try { null.x; } finally { console.log('finally ran'); }
}
new Promise(() => { throw new Error('rejects a promise'); }).catch(() => {});
async function af() { throw new Error('rejects the async function'); }
af().catch(() => {});
outer(1);
"""


def case_exception_uncaught(a):
    prog = write("uncaught.js", UNCAUGHT)
    a.start(prog, exception_filters=["uncaught"])
    s = a.wait_event("stopped")
    frames = stack(a)
    check(s["body"]["reason"] == "exception" and frames[0]["name"] == "outer" and
          frames[0]["line"] == 5, "uncaught: stops in outer at line 5, not at the caught throw")
    check(s["body"].get("text", "").startswith("TypeError"), "uncaught: a TypeError from the runtime")
    check(len(frames) >= 2 and frames[1]["line"] == 10, "uncaught: the stack is intact (caller line 10)")
    loc = variables(a, frames[0]["id"])
    check(loc.get("w", {}).get("value") == "2" and loc.get("v", {}).get("value") == "1",
          "uncaught: locals v=1 w=2")
    info = a.request("exceptionInfo", {"threadId": 1})["body"]
    check(info.get("breakMode") == "unhandled" and info.get("exceptionId") == "TypeError",
          "exceptionInfo: TypeError, breakMode unhandled")
    a.request("continue", {"threadId": 1})
    code = a.finish()
    stops = [e for e in a.events if e["event"] == "stopped"]
    check(len(stops) == 1, "uncaught: one stop (the finally block's rethrow does not stop again)")
    check(code != 0 and "finally ran" in a.output["stdout"], "uncaught: finally runs, the program fails")


AWAITS = """async function thrower() { throw new Error('first'); }
async function main() {
  try { await thrower(); } catch (e) { console.log('main caught', e.message); }
  try {
    await Promise.reject(new Error('awaited rejection'));
  } catch (e) { console.log('main caught', e.message); }
}
main();
"""


def case_exception_await(a):
    prog = write("awaits.js", AWAITS)
    a.start(prog, exception_filters=["raised"])
    a.wait_event("stopped")
    frames = stack(a)
    check(frames[0]["name"] == "thrower" and frames[0]["line"] == 1, "await: stops at the throw in thrower")
    a.request("continue", {"threadId": 1})
    s = a.wait_event("stopped")
    frames = stack(a)
    check(s["body"].get("text") == "Error: awaited rejection" and frames[0]["line"] == 5,
          "await: an awaited rejected promise stops at the await (line 5)")
    a.request("continue", {"threadId": 1})
    check(a.finish() == 0, "await: the program ends normally")
    stops = [e for e in a.events if e["event"] == "stopped"]
    check(len(stops) == 2, "await: two stops (the await of thrower() does not stop again)")
    check(a.output["stdout"].count("main caught") == 2, "await: both catches ran")


os.makedirs(SCRATCH, exist_ok=True)
run_case("breakpoints, stack, variables, evaluate, stepping, output", case_breakpoints)
run_case("stopOnEntry", case_stop_on_entry)
run_case("conditional breakpoint", case_condition)
run_case("breakpoint in a required module", case_module)
run_case("process.exit", case_exit)
run_case("uncaught exception", case_uncaught)
run_case("exception breakpoints: All Exceptions", case_exception_raised)
run_case("exception breakpoints: no stop on internal control flow", case_exception_internal)
run_case("exception breakpoints: filters off", case_exception_off)
run_case("exception breakpoints: Uncaught Exceptions", case_exception_uncaught)
run_case("exception breakpoints: await and rejected promises", case_exception_await)

if failures:
    print("%d check(s) failed" % len(failures))
    sys.exit(1)
print("all checks passed")
