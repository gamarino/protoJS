// Global declarations of a classic script (ECMA-262 §16.1.7
// GlobalDeclarationInstantiation and §9.1.1.4 the global Environment Record):
// a top-level `var` and a top-level function declaration are properties of the
// global object -- non-configurable, writable and enumerable -- while `let`,
// `const` and `class` live in the declarative part of the global environment
// and are not properties of the global object.  The top-level `this` is the
// global object.
//
// protoJS keeps the root script's lexical bindings on a child of the global
// object (the module-scope split in runBytecode); before this test the `var`
// and function bindings landed on that child too, so `this.x` and
// `globalThis.x` did not see them and a write through the global object was
// shadowed by the stale binding.
//
// The file must run as a classic script.  Under Node.js, run it with
// vm.runInThisContext (Node runs a file as a CommonJS module, whose top-level
// `var` is a function local):
//   node -e 'require("vm").runInThisContext(require("fs").readFileSync(process.argv[1], "utf8"))' FILE

var failures = 0;
var checks = 0;
function check(name, cond, detail) {
    checks++;
    if (!cond) {
        failures++;
        console.log("FAILED: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}

var G = globalThis;

// --- this and globalThis ---------------------------------------------------------
check("top-level this is globalThis", this === G);

// --- var -------------------------------------------------------------------------
var planet = "mars";
check("var is a global property", G.planet === "mars");
check("var through this", this.planet === "mars");
check("var own property", Object.prototype.hasOwnProperty.call(G, "planet"));
var pd = Object.getOwnPropertyDescriptor(G, "planet");
check("var descriptor", pd !== undefined && pd.value === "mars" && pd.writable === true
      && pd.enumerable === true && pd.configurable === false,
      JSON.stringify(pd));
check("var not deletable", delete G.planet === false && G.planet === "mars");

// A write through the global object is seen by the binding, and the reverse.
G.planet = "venus";
check("write through globalThis seen by the binding", planet === "venus");
planet = "earth";
check("write to the binding seen through globalThis", G.planet === "earth");

// A var declared without an initializer exists (as undefined) from the start.
check("hoisted var is a property", "hoisted" in G && G.hoisted === undefined);
var hoisted;

// Writes from inside a function reach the global object.
var counter = 0;
function bump() { counter++; }
bump(); bump();
check("function writes to a global var", counter === 2 && G.counter === 2);

// A loop variable.
for (var loopVar = 0; loopVar < 3; loopVar++) {}
check("loop var", loopVar === 3 && G.loopVar === 3);

// Functions built by the Function constructor read globals through `this`
// (Test262 built-ins/Function/S15.3_A3_T2, T5, T6).
var f = Function.call(this, "return this.later;");
check("Function() sees no var yet", f() === undefined);
var later = "now";
check("Function() sees the var through this", f() === "now");

// --- function declarations ---------------------------------------------------------
function declaredFn() { return 1; }
check("function declaration is a global property", G.declaredFn === declaredFn);
var fd = Object.getOwnPropertyDescriptor(G, "declaredFn");
check("function declaration descriptor", fd !== undefined && fd.writable === true
      && fd.enumerable === true && fd.configurable === false,
      JSON.stringify(fd && { w: fd.writable, e: fd.enumerable, c: fd.configurable }));
check("function declaration hoisted", typeof G.usedBeforeDeclared === "function");
function usedBeforeDeclared() {}

// --- let / const / class ------------------------------------------------------------
let lexLet = 1;
const lexConst = 2;
class LexClass {}
check("let is not a global property", !("lexLet" in G) && G.lexLet === undefined);
check("const is not a global property", !("lexConst" in G));
check("class is not a global property", !("LexClass" in G));
check("let readable", lexLet === 1 && lexConst === 2 && typeof LexClass === "function");
lexLet = 5;
function readLet() { return lexLet; }
check("let write and read from a function", readLet() === 5 && !("lexLet" in G));

// Assigning a top-level const is a TypeError, also from inside a function.
var constThrew = false;
try { (function () { lexConst = 3; })(); } catch (e) { constThrew = e instanceof TypeError; }
check("const assignment throws TypeError", constThrew && lexConst === 2);

// A let shadows a global property of the same name.
G.shadowed = "property";
let shadowed = "binding";
check("let shadows a global property", shadowed === "binding" && G.shadowed === "property");
shadowed = "binding2";
check("write to the let leaves the property", G.shadowed === "property");

// Top-level let is in its TDZ before its declaration.
var tdzThrew = false;
try { tdzLater; } catch (e) { tdzThrew = e instanceof ReferenceError; }
check("let TDZ at top level", tdzThrew);
let tdzLater = 1;

// --- implicit globals -------------------------------------------------------------
function makeImplicit() { implicitGlobal = 42; }
makeImplicit();
check("implicit global is a configurable property", G.implicitGlobal === 42
      && Object.getOwnPropertyDescriptor(G, "implicitGlobal").configurable === true);
check("implicit global deletable", delete G.implicitGlobal && !("implicitGlobal" in G));

// --- a property defined before the var keeps its identity ---------------------------
G.preexisting = 7;
check("property readable as a binding", preexisting === 7);
preexisting = 8;
check("binding write updates the property", G.preexisting === 8);

if (failures) {
    console.log("global_var_bindings: " + failures + " of " + checks + " checks failed");
    process.exit(1);
}
console.log("global_var_bindings: all " + checks + " checks passed");
