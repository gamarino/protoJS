# protoJS Debugger — VS Code extension

Debug JavaScript programs with protoJS in VS Code: breakpoints (including
conditional ones, hit counts and logpoints), exception breakpoints (All
Exceptions, Uncaught Exceptions, with conditions on the exception `error`),
stepping, the call stack, variables and the Debug Console.
The debug adapter is the `protojs` binary itself, invoked as `protojs --dap`,
which speaks the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
over stdin/stdout. What the program prints shows up in the Debug Console.

JavaScript syntax highlighting and the `javascript` language are VS Code's
own; this extension only adds a **protoJS** debugger next to the built-in
Node.js one, which keeps working as before.

There is no TypeScript build step and no bundler.

## Requirements

`protojs` must be installed or built (see `docs/INSTALLATION.md` in the
repository root). The extension looks for it in this order:

1. the `protojs.path` setting (Settings → Extensions → protoJS), if set;
2. the default install location — `%LOCALAPPDATA%\Programs\proto\bin\protojs.exe`
   on Windows, `~/.local/bin/protojs` elsewhere — if it exists;
3. `protojs` on your `PATH`.

## Install

Package it with `vsce` and install the `.vsix` (works the same on every
platform):

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protojs-debug-0.4.1.vsix
```

Or, on Linux and macOS, symlink the folder (picks up edits after a
`Developer: Reload Window`):

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protojs-debug-0.4.1
```

## Use

1. Open a `.js` file and click in the gutter to set breakpoints.
2. Press **F5** and choose **protoJS** (VS Code lists the debuggers for
   JavaScript: protoJS and Node.js). Without a `launch.json` the active file
   is debugged; **Run → Add Configuration…** → **protoJS** writes a
   configuration, and `examples/.vscode/launch.json` in the repository has
   ready-made ones.

```json
{
  "type": "protojs",
  "request": "launch",
  "name": "Debug current file with protoJS",
  "program": "${file}",
  "stopOnEntry": false
}
```

| Attribute     | Type     | Required | Meaning                                         |
|---------------|----------|----------|-------------------------------------------------|
| `program`     | string   | yes      | The JavaScript file to debug.                   |
| `args`        | string[] | no       | Arguments for the script.                       |
| `cwd`         | string   | no       | Working directory of the program.               |
| `env`         | object   | no       | Environment variables added for the program.    |
| `stopOnEntry` | boolean  | no       | Stop at the first statement (default off).      |

| `pathMappings`| array    | no       | `[{localRoot, remoteRoot}]`: path translation for a remote adapter. |
| `remote`      | object   | no       | `{command: [...], path}`: run `<command...> <path> --dap` (ssh, wsl, docker exec). Needs `pathMappings`. |

## Remote debugging

- **VS Code Remote (SSH / WSL / Dev Containers):** install protojs and this
  `.vsix` on the remote side while connected; the extension is a
  `workspace` extension, so it runs there. Nothing else to set.
- **`remote` + `pathMappings`:** keep the editor here and run the adapter
  elsewhere. Snippets **protoJS: Launch over SSH** and **protoJS: Launch in
  WSL** (Run → Add Configuration…) write:

```json
"remote": { "command": ["ssh", "-T", "-o", "BatchMode=yes", "user@host"], "path": "protojs" },
"pathMappings": [{ "localRoot": "${workspaceFolder}", "remoteRoot": "/home/user/project" }]
```

The remote shell must print nothing on a non-interactive start (no banner,
no `echo` in `.bashrc`): the protocol runs over that stream. Files outside
the mappings, code embedded in protojs and code built with `new Function`
open read-only, served by the adapter.

See `docs/DEBUGGING.md` for the full debugging guide (also: JetBrains IDEs
through LSP4IJ) and its limitations
(only the main thread is debugged; ES modules are not debuggable; an
assignment to a local in the Debug Console does not reach the program;
Uncaught Exceptions does not stop at a throw that rejects a promise).

The exception filters (**All Exceptions**, **Uncaught Exceptions**) appear
in the Breakpoints view once a session starts: `protojs --dap` announces
them, so the extension needs no setting for them.

## Testing the extension

`test/` holds integration tests that run this extension inside a real VS Code
(an Extension Development Host, through `@vscode/test-electron` and mocha)
and debug `test/fixture/fixture.js` with `protojs`, driving VS Code's own
debug API the way F5 does:

```bash
cd editor-integration/vscode
npm install
npm test
```

They check: a local launch (breakpoint hit on the right line, `stackTrace`,
a local's value, the program's stdout as `output` events, `terminated`); F5
without a `launch.json` on the open fixture; the executable lookup (a
`protojs.path` that does not exist is refused with a message naming it; with
the setting empty the default install is the process that runs); a `remote`
launch through WSL with `pathMappings` (the breakpoint is set by the Windows
path and the frames come back with Windows paths); a `remote` launch without
`pathMappings` refused with its explanation; the `raised` exception filter;
and that the launch schema, snippets and initial configurations agree.

- **Which VS Code.** `$VSCODE_EXECUTABLE` if set; on Windows the per-user
  install when present (unless it is halfway through an update, which makes
  every new instance refuse to start); otherwise a pinned VS Code is
  downloaded (`$VSCODE_VERSION` overrides the version) into
  `$VSCODE_TEST_CACHE`.
- **Isolation.** A throwaway `--user-data-dir` and `--extensions-dir` under
  `$PROTO_VSCODE_E2E_DIR` (default `C:\tmp\protowin\vscode-e2e` on Windows,
  `$TMPDIR/proto-vscode-e2e` elsewhere), recreated on every run, and every
  other extension disabled: your own VS Code may stay open. VS Code's own
  JavaScript debugger (`ms-vscode.js-debug`) is disabled in the test
  instance, so that F5 has a single candidate debugger for `.js` instead of
  asking which one.
- **Which runtime.** `$PROTO_E2E_EXE` (put into `protojs.path`), else the
  extension's own lookup: default install, then `PATH`. The default-install
  test is skipped when there is no default install.
- **Remote.** Runs only on Windows with WSL (`$PROTO_E2E_WSL_DISTRO`, default
  `Ubuntu-24.04`) and the Linux build named in `test/lang.js`; it is skipped
  elsewhere.
- `$PROTO_E2E_GREP` runs only the tests whose title matches.

`test/runTest.js`, `test/suite/` are the same in the five proto* runtimes'
extensions; `test/lang.js` and the fixture hold the per-language part.
`test/` and `node_modules/` are left out of the `.vsix` (`.vscodeignore`).
CI runs the local tests on Linux under `xvfb-run` (`.github/workflows/ci.yml`).
