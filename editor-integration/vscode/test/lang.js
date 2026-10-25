// The per-language half of the extension's integration tests: everything the
// shared runner (runTest.js) and suite (suite/extension.test.js) need to know
// about this runtime. The other files under test/ are identical in protoST,
// protoJS, protoPython, protoScala and ProtoClojure; only this file and the
// fixture differ.
module.exports = {
  debugType: "protojs",
  exe: "protojs",
  extensionId: "protojs-placeholder.protojs-debug",
  // The configuration section of the `<section>.path` setting.
  settingSection: "protojs",
  languageId: "javascript",
  fixture: "fixture.js",
  // A line inside `twice` where the local below is already assigned.
  breakpointLine: 4,
  local: { name: "doubled", value: "42" },
  // Where the `raised` exception filter stops (the `throw` line).
  exceptionLine: 7,
  // Printed by the fixture before the breakpoint and at its end.
  stdoutBefore: "e2e-before",
  stdoutAfter: "e2e-after",
  // Built-in extensions that also debug this language (F5 would ask which).
  disableBuiltins: ["ms-vscode.js-debug","ms-vscode.js-debug-companion"],
  // The Linux build used by the remote (WSL) test.
  linuxExe: "/home/gamarino/proto-linux/build/protoJS/protojs",
  linuxLibPath: "/home/gamarino/proto-linux/prefix/lib",
};
