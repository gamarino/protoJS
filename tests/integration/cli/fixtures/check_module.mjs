// Parses as an ES module. It deliberately has no imports: in module mode the
// syntax check parses the module itself but cannot resolve imports (see
// README.md). The marker must never be printed, because `--check` parses the
// input without executing it.
export const value = 41;

console.log("MODULE-SIDE-EFFECT", value + 1);
