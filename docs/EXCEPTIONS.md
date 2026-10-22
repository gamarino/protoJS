# Exceptions

protoJS follows protoCore's unified exception model (protoCore
`docs/EXCEPTIONS.md`, `ProtoException` and `ProtoErrorKind` in `protoCore.h`).
Every language on protoCore throws the same C++ type, so an error raised by a
module written in C++, or in another language, reaches a JavaScript `catch`
exactly like a JavaScript `throw`, and a JavaScript error leaves protoJS the
same way.

## The model

- A JavaScript `throw`, and a native that raises (`signalNativeException`), is
  a C++ `throw` of `proto::ProtoException` carrying the thrown value -- any
  value, `throw undefined` included. The interpreter throws it as its subclass
  `protojs::JSException`, which adds no state
  (`src/runtime/ProtoInterpreter.h`).
- Each JavaScript `try` is a nested interpreter level inside a C++ `try`
  (`runProtectedRange`). Every handler in protoJS -- a JavaScript `catch`, a
  promise job, an event-loop callback, a boundary to QuickJS -- catches the
  base type, `const proto::ProtoException&`, never only `JSException`.
- protoCore pins the value while the exception is in flight
  ([GC_BRIDGING.md](GC_BRIDGING.md#values-that-cross-a-context-boundary)
  explains the pin and its two rules). protoJS keeps no root set of its own
  for it.
- There is no pending-exception flag, no thread-local exception slot and no
  jump-without-throw path: an exception is a C++ exception everywhere.

## Catching an error type defined by a module

A JavaScript `catch (e)` catches every exception, as ECMAScript specifies; the
handler selects by the value's prototype chain and rethrows what it does not
handle. A module's error types are values the script names through the
module's exports. protoCore's example geometry module
(`protoCore/examples/exceptions/`) defines `GeometryError`, with
`NegativeRadius` and `DegenerateTriangle` below it:

```js
const geometry = require('example/geometry');

function areaOrZero(r) {
    try {
        return geometry.circleArea(r);
    } catch (e) {
        if (!(e instanceof geometry.GeometryError)) throw e;   // not ours
        console.log(String(e));   // "NegativeRadius: circleArea: the radius ..."
        return 0;
    }
}
```

A module's type is a plain protoCore prototype, not a function. ECMAScript's
`instanceof` requires a callable right-hand side, so two forms work:

| Form | Where it works |
|---|---|
| `e instanceof geometry.NegativeRadius` | protoJS: a **platform extension** (below) |
| `Object.prototype.isPrototypeOf.call(geometry.NegativeRadius, e)` | any engine: the specification's own test of a prototype chain |

`geometry.NegativeRadius.isPrototypeOf(e)` does **not** work: the module's
objects do not inherit JavaScript's `Object.prototype`, so they have no
`isPrototypeOf` method. Use the `.call` form for code that must also run
elsewhere.

### The `instanceof` extension

For `x instanceof T`, when `T` is an object **defined outside JavaScript** --
not callable, not a Proxy, with no JavaScript `Object.prototype` on its
protoCore prototype chain and no JavaScript `[[Prototype]]` override
(`isForeignObject`, `src/runtime/ProtoInterpreter.h`) -- protoJS answers
whether `T` is on `x`'s prototype chain instead of taking `T.prototype`. This
is the reading `instanceof` already has for a type that is its own prototype,
the same way `Symbol.hasInstance` lets a JavaScript object redefine the test.
Nothing changes for a right-hand side JavaScript created: a function, a class,
a bound function or a Proxy keeps the specification's behaviour, and so does a
JavaScript object such as `{}` or `Object.create(null)`.

It is a deviation from ECMA-262 §13.10.2 only for values no JavaScript program
can create; it is recorded in
[API_REFERENCE.md](API_REFERENCE.md#module-error-types-and-instanceof). The
alternatives were worse surprises: wrapping each module type in a JavaScript
constructor would give the type two identities (the wrapper and the value the
module raises against), and leaving `instanceof` to the specification would
make the most natural handler a `TypeError`.

### What a module's error value looks like

A module's error value is a protoCore object, a child of the module's type,
carrying the attributes the module set (the example sets `message`; its types
carry `name`):

| Expression | Value |
|---|---|
| `e.message`, `e.name` | the attributes, through the chain (`"NegativeRadius"`) |
| `String(e)`, `` `${e}` ``, `"" + e` | `"NegativeRadius: circleArea: the radius -1.000000 is negative"`: Error.prototype.toString over `name` and `message`; `"[object Object]"` when it has neither |
| `Number(e)` | `NaN` |
| `Object.getPrototypeOf(e)` | `geometry.NegativeRadius` |
| `e instanceof Error` | `false`: it is not a JavaScript `Error` |
| `JSON.stringify(e)`, `Object.keys(e)` | its own attributes |

## protoCore's own errors: the error factory

protoCore raises its own errors (an integer division by zero, a malformed
integer literal, ...) as `ProtoException`s whose value the space's error
factory builds. protoJS installs one when each space is created
(`installErrorFactory`, called from the `JSContextWrapper` constructor). It
builds the instance the way the JavaScript constructor would (`makeError`:
a child of the class's `prototype` with an own `message`), reading the classes
from the running frame's global object, or the current wrapper's outside any
frame; both are GC roots.

| `ProtoErrorKind` | JavaScript class | Example |
|---|---|---|
| `InvalidArgument` | `SyntaxError` | a malformed integer literal: a module's `parseRadius("ten")` |
| `TypeMismatch` | `TypeError` | an integer operation on a value that is not an integer |
| `Overflow` | `RangeError` | a large integer read as a 64-bit value |
| `DivisionByZero` | `RangeError`, message `"Division by zero"` | `1n % 0n` |
| `ArityMismatch` | `TypeError` | a call with more positional arguments than parameters |

The factory never throws; if building the error fails it answers a plain
object with the message. Before the first evaluation the realm's global object
does not exist yet, and an error raised then is a plain object with a
`message`.

JavaScript's own checks keep their messages: `BigInt("x")` validates the
literal before protoCore parses it and raises `SyntaxError: Cannot convert
string to a BigInt`, and `1n / 0n` is checked before the division
(`RangeError: Division by zero`). `1n % 0n` reaches protoCore and arrives
through the factory with the same class and message.

## Foreign C++ exceptions

A C++ standard exception (`std::exception`, or anything else) is a defect of
the code that let it escape, not a JavaScript value. protoJS never lets one
cross the interpreter as is:

- **The native-call boundary.** The interpreter calls every native -- a
  built-in, an addon function, a method of a UMD module -- through
  `callNative` (`src/runtime/ProtoInterpreter.h`): a `ProtoException` passes
  through untouched, a `std::exception` becomes a JavaScript `Error` whose
  message is `what()`. `geometry.legacyArea(-1)` throws
  `std::runtime_error("legacyArea: negative radius")` and the script catches
  `Error: legacyArea: negative radius`. A JavaScript `try` translates the same
  way for a native reached by another path (`runProtectedRange`).
- **QuickJS boundaries.** Where QuickJS (C code) calls into protoJS -- the
  CommonJS `require` callback, the ES-module evaluator, the v1 native-module
  wrapper -- a `ProtoException` is raised on the QuickJS context as its value,
  a `std::exception` as an `Error` with `what()`, anything else as an `Error`
  saying so (`TypeBridge::throwCurrentOnQuickJS`).
- **Pool threads.** A Deferred's function on a pool thread rejects the
  Deferred with the `Error` that stands for a foreign exception; a `cpuChunk`
  worker reports it on stderr and answers `undefined`.

## Uncaught exceptions

| Where | Behaviour |
|---|---|
| The main script | `Exception in <file>: <name>: <message>` on stderr, exit status 1 (Node's status). A module's error is reported from its `name` and `message` attributes. |
| An event-loop callback, a job, a microtask | `Uncaught exception in <where>: <name>: <message>`, exit status 1 |
| A rejected promise nobody handled | `Uncaught (in promise) <name>: <message>` (see `--unhandled-rejections`) |
| A worker thread | the worker's `error` event |
| Anything that reaches `main` | `Uncaught exception in main: ...`, or `Uncaught C++ exception in main: <what()>` for a foreign one; exit status 1. Nothing reaches `std::terminate`. |

## Embedding

An unhandled JavaScript error leaves protoJS as the `proto::ProtoException`
carrying the JavaScript error object: `runBytecode`, `callJSFunction` and
`callJSFunctionFromAsync` let it propagate, so a C++ embedder or another
language selects it by prototype like any other (`e.isInstanceOf(ctx,
TypeError.prototype)`); `tests/unit/test_exceptions.cpp` checks it.
`JSContextWrapper::eval` is the command-line entry and reports the exception
itself, as Node does.

protoJS has no conformance host (`docs/CONFORMANCE.md`: the runtime adaptor is
not written), so protoCore's rule-14 cases `exception.foreign_type_caught` and
`exception.language_error_escapes` do not run against it; the tests below cover
the same ground.

## Tests

- `tests/integration/native_addons/test_module_exceptions.js` (ctest
  `js/native_addons/test_module_exceptions`): the geometry module imported
  through protoJS's own module import, every case above. The module is
  registered by a test addon, `tests/native_addons/geometry/geometry_addon.cpp`,
  which compiles protoCore's `GeometryModule.cpp` in and, when loaded,
  registers the `example_geometry` UMD provider and puts
  `provider:example_geometry` first in the space's resolution chain. The
  script loads the addon by path and then requires the bare specifier
  `example/geometry`. The addon is built when protoCore's sources are next to
  protoJS (`PROTOJS_GEOMETRY_EXAMPLE_DIR`, default
  `../protoCore/examples/exceptions/geometry_module`). By hand:

  ```sh
  build/protojs tests/integration/native_addons/test_module_exceptions.js \
      "$PWD/build/tests/native_addons/geometry/geometry.so"
  ```

  The addon path must be absolute: a path that does not start with `.` or `/`
  is a bare specifier, which protoCore's module discovery answers before the
  addon loader sees it.
- `js/native_addons/module_exception_uncaught`: the report of an uncaught
  module error.
- `tests/unit/test_exceptions.cpp` (`[exceptions]`): a JavaScript error
  escaping to C++ as a `ProtoException`; protoCore's errors built by the
  factory.
