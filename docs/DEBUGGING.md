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
code --install-extension protojs-debug-0.1.0.vsix
```

On Linux and macOS you can instead symlink the folder, which picks up edits
after **Developer: Reload Window**:

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protojs-debug-0.1.0
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
- **Conditional breakpoints**: the condition is evaluated in the frame, and
  the program stops when it is truthy (a condition that throws stops too, and
  its error is printed to the Debug Console).
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
- **process.exit** and an uncaught exception end the session with the
  program's exit status.

## Limitations

- **Only the main thread is debugged.** Code running on other threads — a
  `Deferred`'s function, `worker_threads` workers, `protoCore.runInThread` —
  runs normally and does not stop at breakpoints.
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
- No exception breakpoints (an uncaught exception ends the program and is
  reported on stderr), no logpoints or hit counts, no column breakpoints, no
  `debugger;` statement (it is ignored), no attach to a running process.
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
- The test `cli/dap-session` (`tests/cli/dap_session.py`) drives sessions the
  way VS Code does — absolute paths, on Windows with a lower-case drive letter
  — and checks breakpoints, the stack, variables, evaluate, stepping,
  stopOnEntry, conditions, a breakpoint in a required module, process.exit,
  an uncaught exception, more than 64 KB of non-ASCII output and a child
  process reading stdin.

The `debugger` global (`globalThis["debugger"]`, `startCDPServer`) is an older
Chrome DevTools-flavoured module that records breakpoints but never stops the
program; use `protojs --dap` to debug.
