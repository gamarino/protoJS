# protoJS Debugger — VS Code extension

Debug JavaScript programs with protoJS in VS Code: breakpoints (including
conditional ones), exception breakpoints (All Exceptions, Uncaught
Exceptions), stepping, the call stack, variables and the Debug Console.
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
code --install-extension protojs-debug-0.2.0.vsix
```

Or, on Linux and macOS, symlink the folder (picks up edits after a
`Developer: Reload Window`):

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protojs-debug-0.2.0
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

See `docs/DEBUGGING.md` for the full debugging guide and its limitations
(only the main thread is debugged; ES modules are not debuggable; an
assignment to a local in the Debug Console does not reach the program;
Uncaught Exceptions does not stop at a throw that rejects a promise).

The exception filters (**All Exceptions**, **Uncaught Exceptions**) appear
in the Breakpoints view once a session starts: `protojs --dap` announces
them, so the extension needs no setting for them.
