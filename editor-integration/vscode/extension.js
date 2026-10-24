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
    if (!config.program) {
      return vscode.window
        .showInformationMessage("protoJS: open a .js file or set 'program' in launch.json.")
        .then(() => undefined);
    }
    return config;
  },
};

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(/* session */) {
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
