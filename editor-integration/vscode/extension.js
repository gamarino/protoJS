// Minimal protoJS debug adapter glue for VS Code.
//
// The protojs binary itself IS the debug adapter: `protojs --dap` speaks the
// Debug Adapter Protocol over stdin/stdout. VS Code only needs an extension to
// register the debug `type` and tell it how to spawn that adapter. This factory
// returns a DebugAdapterExecutable that runs `protojs --dap`.
//
// `protojs` is taken from the `protojs.path` setting if set, else from the
// default install location (when present), else from PATH.
//
// Remote debugging: a launch configuration with `remote.command` (an argv
// prefix that pipes stdin/stdout to another machine: ssh, wsl, docker exec)
// runs `<command...> <remote.path> --dap` instead.  Everything about paths
// (pathMappings, files served by reference) is done by protojs itself; the
// extension only chooses the command.
//
// JavaScript itself (syntax, language id `javascript`) is VS Code's own; this
// extension adds a second debugger for it next to the built-in Node.js one,
// and never replaces it.
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const os = require("os");

function defaultInstallPath() {
  if (process.platform === "win32") {
    const base = process.env.LOCALAPPDATA || path.join(os.homedir(), "AppData", "Local");
    return path.join(base, "Programs", "proto", "bin", "protojs.exe");
  }
  return path.join(os.homedir(), ".local", "bin", "protojs");
}

function protojsCommand() {
  const configured = vscode.workspace.getConfiguration("protojs").get("path");
  if (configured) return configured;
  const installed = defaultInstallPath();
  return fs.existsSync(installed) ? installed : "protojs";
}

// F5 without a launch.json: VS Code offers the debuggers registered for the
// active file's language (protoJS and Node.js for JavaScript); picking protoJS
// lands here with an empty configuration, which debugs the active file.
// Also fills in `program` when a configuration omits it.
const configurationProvider = {
  resolveDebugConfiguration(folder, config) {
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === "javascript") {
        config.type = "protojs";
        config.name = "Debug current file with protoJS";
        config.request = "launch";
        config.program = "${file}";
      }
    }
    const remoteError = checkRemote(config);
    if (remoteError) {
      return vscode.window.showErrorMessage(remoteError).then(() => undefined);
    }
    if (!config.program) {
      return vscode.window
        .showInformationMessage("protoJS: open a .js file or set 'program' in launch.json.")
        .then(() => undefined);
    }
    return config;
  },
};

// What is wrong with a configuration's `remote`, or null.
function checkRemote(config) {
  if (!config.remote) return null;
  const cmd = config.remote.command;
  if (!Array.isArray(cmd) || cmd.length === 0 || !cmd.every((c) => typeof c === "string")) {
    return "protoJS: 'remote.command' must be a non-empty array of strings, " +
      "e.g. [\"ssh\", \"-T\", \"user@host\"] or [\"wsl\", \"-d\", \"Ubuntu-24.04\", \"--\"].";
  }
  if (!Array.isArray(config.pathMappings) || config.pathMappings.length === 0) {
    return "protoJS: a 'remote' launch needs 'pathMappings': " +
      "[{\"localRoot\": \"${workspaceFolder}\", \"remoteRoot\": \"<the same folder on the remote machine>\"}], " +
      "so that breakpoints and the call stack name the right files on each side.";
  }
  return null;
}

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(session) {
      const config = (session && session.configuration) || {};
      if (config.remote && !checkRemote(config)) {
        const cmd = config.remote.command;
        const exe = config.remote.path || "protojs";
        return new vscode.DebugAdapterExecutable(cmd[0], [...cmd.slice(1), exe, "--dap"]);
      }
      return new vscode.DebugAdapterExecutable(protojsCommand(), ["--dap"]);
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("protojs", factory),
    vscode.debug.registerDebugConfigurationProvider("protojs", configurationProvider)
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
