# Debugging protoJS programs in VS Code

`protojs --dap` is a [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
server built into the `protojs` executable: it speaks the protocol over its
stdin and stdout and runs the program it is asked to launch, exactly as
`protojs <program> <args...>` would. The VS Code extension in
[`editor-integration/vscode`](../editor-integration/vscode) registers a
`protojs` debugger that starts it. Any other DAP client (Neovim's nvim-dap,
Emacs dap-mode, ...) can use `protojs --dap` the same way.

VS Code ships JavaScript support (syntax, the `javascript` language id) and a
Node.js debugger; the extension adds protoJS as a second debugger for the same
files and does not change the Node.js one.

## Install the extension

The extension is plain JavaScript with no build step. Package it and install
the `.vsix` (the same on Windows, Linux and macOS; needs Node.js for `npx`):

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protojs-debug-0.3.0.vsix
```

On Linux and macOS you can instead symlink the folder, which picks up edits
after **Developer: Reload Window**:

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protojs-debug-0.3.0
```

The extension runs `protojs` from, in this order:

1. the `protojs.path` setting (Settings → Extensions → protoJS), if set;
2. the default install location — `%LOCALAPPDATA%\Programs\proto\bin\protojs.exe`
   on Windows, `~/.local/bin/protojs` elsewhere — if it exists;
3. `protojs` on the `PATH`.

## Start a session

- **Without a launch.json:** open a `.js` file, press **F5** and pick
  **protoJS** in the list of debuggers (VS Code also offers Node.js there).
  The active file is debugged.
- **With a launch.json:** **Run → Add Configuration… → protoJS** writes one;
  [`examples/.vscode/launch.json`](../examples/.vscode/launch.json) has
  ready-made configurations for [`examples/debug_demo.js`](../examples/debug_demo.js)
  (open the `examples` folder in VS Code to use them).

```json
{
  "type": "protojs",
  "request": "launch",
  "name": "Debug current file with protoJS",
  "program": "${file}",
  "stopOnEntry": false
}
```

| Attribute     | Type     | Meaning |
|---------------|----------|---------|
| `program`     | string   | The script to debug (required). |
| `args`        | string[] | The script's arguments (`process.argv` after the script). |
| `cwd`         | string   | Working directory of the program. |
| `env`         | object   | Environment variables added for the program. |
| `stopOnEntry` | boolean  | Stop at the program's first statement. |

## What works

- **Line breakpoints** in the main script and in the files it `require`s. A
  breakpoint in a file that has not been loaded yet shows as unverified and
  becomes verified when the file loads; a breakpoint on a line without code
  (a comment, a blank line) moves to the next line that has code.
- **Conditional breakpoints, hit counts and logpoints** (see
  [Breakpoint conditions, hit counts and logpoints](#breakpoint-conditions-hit-counts-and-logpoints)).
- **Continue, step over, step into, step out, pause**, and `stopOnEntry`.
  Steps stop at statements. Step into enters a JavaScript function called on
  the line (a native function such as `Array.prototype.map` is stepped over,
  but a JavaScript callback it calls is entered); step out stops at the
  caller's next statement after the call returns. Pause stops at the next
  statement the program runs: a program idle in the event loop stops when its
  next callback runs.
- **Call stack**: every JavaScript frame with its function name, file and line
  (`<toplevel>` is a script's or module's own code). Callbacks run by the
  event loop (`setImmediate`, promise reactions, the rest of an async
  function after an `await`) appear as their own stack.
- **Variables**: for each frame, **Locals** (arguments and local variables),
  **Closure** (the variables of enclosing functions the frame uses) and
  **Script** (the top-level `let` / `const` / `var` / function bindings of
  the script). Objects and arrays expand into their own properties and
  elements; a `let` or `const` read before its declaration ran shows as
  `<uninitialized>`.
- **Evaluate** (Debug Console, Watch, hover) in the selected frame: any
  expression, with the frame's variables, `this`, the script's bindings and
  the globals in scope. It runs in the stopped program, so a call or a getter
  in the expression runs too.
- **Output**: what the program writes to stdout and stderr (`console.log`,
  `console.error`, uncaught-exception reports, the output of child processes
  that inherit them) appears in the Debug Console. The program's stdin is
  empty (end of file): it is not the debugger's input stream.
- **Exception breakpoints**: **All Exceptions** and **Uncaught Exceptions**
  in the Breakpoints view (see below).
- **process.exit** and an uncaught exception end the session with the
  program's exit status.

## Breakpoint conditions, hit counts and logpoints

Right-click a breakpoint → **Edit Condition** / **Edit Hit Count** /
**Edit Log Message** (or **Add Conditional Breakpoint…** / **Add Logpoint…**
in the gutter):

- **Condition**: a JavaScript expression, evaluated in the frame at the
  breakpoint's line each time the line is reached (its arguments, locals,
  closure variables, the script's bindings and the globals). The program
  stops when it is truthy in JavaScript's sense.
- **Hit count**: when the breakpoint stops, counting only the hits whose
  condition held. `N` or `== N` stops at the Nth hit, `>= N`, `> N`, `< N`
  and `<= N` at the hits that compare so, and `% N` at every Nth hit (the
  Nth, the 2Nth...). Anything else shows the breakpoint as unverified, with
  the reason. The count starts again when the file's breakpoints are set
  again (an edit of any breakpoint in the file).
- **Log message**: a logpoint does not stop; it prints the message to the
  Debug Console. `{expression}` parts are evaluated in the frame (`{i}`,
  `{obj.name}`, `{({a: i}).a}`); a string prints without quotes, other values
  as the Variables view shows them; an expression that fails prints
  `<error: ReferenceError: x is not defined>`. `{{` and `}}` print a literal
  brace. A logpoint with a condition or a hit count logs only when they pass.

A condition that fails (a syntax error, or it throws) **stops** the program,
with `breakpoint condition `...` failed: <message>` in the stop's
description and on the Debug Console's stderr; a logpoint whose condition
fails stops too, instead of logging.

Conditions, logpoint expressions and exception conditions run with the
debugger switched off on the program's thread: a breakpoint in a function
the expression calls does not stop, and an exception it throws is not an
exception stop. Each one is compiled the first time it runs in a frame with
a given set of variables, and reused from then on.

## Exception breakpoints

The Breakpoints view lists two exception filters, both off by default:

- **All Exceptions** (`raised`) stops at every exception thrown in your code,
  caught or not.
- **Uncaught Exceptions** (`uncaught`) stops at an exception that nothing on
  the stack will catch.

Either stops **where the exception is thrown, before it unwinds**: the call
stack, the frames' variables and evaluate are those of the throwing
statement, and the stop's reason is *exception*, with the exception's class
and message (`TypeError: x is not a function`). **Continue** lets it go on:
to its `catch`, or, uncaught, to the end of the program. The exception
widget (and the `exceptionInfo` request) shows the class, the message, the
break mode and a stack trace.

**Conditions on the filters.** Each filter takes a condition (the pencil
next to it in the Breakpoints view): a JavaScript expression evaluated in the
throwing frame, with the frame's variables and the globals, and with the
exception bound to **`error`** — which shadows a variable of the frame named
`error`. The program stops only when it is truthy:

- `error instanceof TypeError`
- `error.message.includes("timeout")`
- `!(error instanceof RangeError) && count > 3` (`count` a local of the frame)

A condition that fails (a syntax error, or it throws: `error.code.length` on
an error without `code`) stops at the exception, with `exception breakpoint
condition `...` failed: <message>` in the stop's description and on the
Debug Console's stderr. A syntax error is found when the condition first runs
(at the first exception), not when it is set: the program's thread is the
only one that can compile JavaScript. With both filters on, Uncaught's
condition decides for an uncaught exception, and All Exceptions' for the
others (and for an uncaught one whose Uncaught condition is false).

What counts as a throw:

- a `throw` statement;
- an error the runtime raises: a `TypeError` or `ReferenceError` from an
  operation, a `RangeError` (the call stack exhausted too), an error thrown by
  a built-in function (`JSON.parse`, ...), an error protoCore raises (a
  BigInt division by zero). The stop is at the statement that ran the
  operation or called the built-in, and its description says the runtime
  raised it;
- an `await` of a rejected promise, which throws the rejection at the
  `await`;
- `generator.throw(value)`.

A promise that is rejected (`Promise.reject(e)`, the `reject` function of
an executor) is not a throw, and does not stop by itself: it stops where it is
awaited, or never, if its rejection is only handled with `.catch()`.

**Uncaught** is decided when the exception is thrown, from the stack: it is
caught when an activation on the stack has an open `try` with a `catch`
clause (a `try`/`finally` alone rethrows, so it does not count), and also when
the exception turns into something else before it leaves the stack:

- thrown in an **async function**, it rejects the function's promise;
- thrown in a **promise executor**, a `then`/`catch`/`finally` callback, a
  thenable's `then`, `Promise.try` or the iterables of `Promise.all` & co., it
  rejects a promise;
- thrown by an iterator's `return()` while a loop closes it on an exception,
  it is discarded.

None of those stops the **Uncaught** filter (use **All Exceptions**), even
when the rejected promise ends up unhandled: protoJS reports an unhandled
rejection after the fact, when the stack is gone. Every other built-in that
calls your functions (`Array.prototype.map`, `forEach`, `sort`, getters,
iterators...) passes their exceptions on, so the prediction looks through
it to the caller's `try`. An exception thrown inside a callback that the
event loop runs (`setImmediate`, `queueMicrotask`, a `fs` callback) is
uncaught when that callback does not catch it.

What never stops:

- **The engine's own control flow.** protoJS uses no exceptions for
  iteration, generators, `break`/`continue`/`return` through `finally`, or
  `async`/`await`: `for...of`, spread, destructuring, closing an iterator,
  `generator.return()` and resolved awaits throw nothing. The exceptions the
  engine throws and catches itself are filtered out: protoCore's failure to
  parse a string as a BigInt in `10n == "abc"`, and `Reflect.defineProperty`'s
  failed definition (which answers `false`).
- **A rethrow of the exception just reported**: the rethrow at the end of a
  `finally` block, or the `await` of the promise that the exception rejected,
  does not stop a second time. Once a `catch` clause takes the exception, a
  new `throw` of it (`catch (e) { throw e; }`) is a new throw and stops again.
- **Code that is not one of your files** (it has no source): All Exceptions
  stops only when the innermost frame is in one of your files; Uncaught
  stops in your innermost frame and says so.
- **The debugger's own code**: an expression in the Debug Console, a Watch
  or a breakpoint condition that throws reports its error there.
- **Other threads** (below).

## Limitations

- **Only the main thread is debugged.** Code running on other threads — a
  `Deferred`'s function, `worker_threads` workers, `protoCore.runInThread` —
  runs normally and does not stop at breakpoints or exception breakpoints.
  Each of those runs JavaScript in a context of its own (its own global
  object, module cache and job queue), so debugging it would take a child
  debug session per thread (one `startDebugging` per worker), which the
  adapter does not implement; the **Call Stack** view shows one thread,
  `main`.
- **ES modules are not debuggable.** A program run with
  `--input-type=module` runs in QuickJS's own interpreter, not in protoJS's,
  and its breakpoints never hit. CommonJS (`require`) works.
- **An assignment in the Debug Console does not reach a local variable.** The
  expression sees the frame's arguments, locals and closure variables as
  copies: `x = 5` changes the copy, not the program's `x`. Assignments to
  globals, to the script's top-level bindings and to object properties
  (`obj.count = 0`) do take effect. There is no **Set Value** in the
  Variables view.
- **Variables show own properties only**: no prototype chain, no getters run,
  no internal state of `Map`, `Set`, `Date` or `Promise`; arrays show their
  first 1000 elements.
- **Uncaught Exceptions is a prediction** made at the throw (see above): an
  exception swallowed by a built-in other than the ones listed there would
  still stop, and an exception turned into a promise rejection never does.
  There are no exception options (filter by class with a condition such as
  `error instanceof TypeError` instead).
- No column breakpoints, no `debugger;`
  statement (it is ignored), no attach to a running process.
- **Slower execution under the debugger**: the bytecode specialiser is off,
  and while a breakpoint is set (or a step is in progress) every instruction
  is checked. In a compute-bound benchmark that came to about 1.3-1.5 times
  the normal run time with a breakpoint set, and close to normal with none.

## How it works

- `src/dap/DapSession.cpp` is the server. The program runs on the main thread
  through the same code as `protojs <program>`; a reader thread (never
  registered with protoCore, never touching a JavaScript value) reads the
  client's requests. While the program is stopped, the main thread waits in
  an unmanaged region inside the interpreter, with the stopped frames intact,
  and runs the requests that need the program's values (stack trace, scopes,
  variables, evaluate) as tasks on its own context, where those values are
  rooted. An object a variables handle names is pinned in the wrapper's root
  set until the program resumes.
- `src/dap/DapStdio.cpp` keeps the protocol apart from the program's standard
  streams: the protocol uses private duplicates of the original stdin and
  stdout; fds 1 and 2 become pipes drained into `output` events (never
  splitting a UTF-8 sequence), and fd 0 becomes the null device.
- `src/debugger/DebugHooks.h` is the engine side. Off unless `--dap` turned it
  on, it costs one predictable branch per instruction and per call. On, the
  loader keeps each function's pc-to-line table, slot names and file name,
  QuickJS records a line for every statement (by default it records one only
  for the operations that can throw), the interpreter reports every
  activation and every instruction, and the bytecode specialiser is off so
  the byte offsets run are the ones the line tables describe.
- Exceptions go through three places, each reported to the debugger before
  the C++ exception that carries the value is thrown: `throwJS` (every error
  the runtime and the built-ins raise), the `throw` opcode, and the error
  factory protoCore calls for its own errors. A JavaScript `try` is a C++
  `try` around the protected range, and each activation keeps its open ranges
  (`catch_stack`); the prediction reads them, telling a `catch` handler from
  a `finally` one by its first opcode (QuickJS compiles a `finally` handler
  as `gosub finally; throw`). The built-ins that catch what the code they call
  throws mark themselves (`debughooks::CatchingNative`), and the engine's own
  probes are excluded (`debughooks::InternalTry`).
- The test `cli/dap-session` (`tests/cli/dap_session.py`) drives sessions the
  way VS Code does — absolute paths, on Windows with a lower-case drive letter
  — and checks breakpoints, the stack, variables, evaluate, stepping,
  stopOnEntry, conditions, a breakpoint in a required module, process.exit,
  an uncaught exception, more than 64 KB of non-ASCII output, a child
  process reading stdin, and the exception filters: All Exceptions at a throw
  in a function (line, locals, evaluate, exceptionInfo, then the catch runs),
  no stop on iteration, generators, `finally`, promises and the engine's
  probes, no stop with the filters off, Uncaught Exceptions at a throw that
  only a `finally` covers (and not at caught ones, nor at those that reject a
  promise), an awaited rejection; conditions on the filters (true, false,
  failing, calling a function with a breakpoint), hit counts (`>= 5`, `3`,
  `% 2`, an invalid one), line conditions (true, false, failing) and
  logpoints (`{i}`, `{{literal}}`, an expression error, with a hit count).

The `debugger` global (`globalThis["debugger"]`, `startCDPServer`) is an older
Chrome DevTools-flavoured module that records breakpoints but never stops the
program; use `protojs --dap` to debug.
