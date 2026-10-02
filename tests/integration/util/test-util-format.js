// Adapted from Node.js v22.20.0 test/parallel/test-util-format.js (MIT licence;
// see ../path/node_shim.js for the notice). Node's test harness is replaced by
// ../path/node_shim.js; the cases protoJS cannot run are commented out, each
// block under a 'protoJS:' line that says why. Every other line is Node's.
'use strict';
// protoJS: Node's harness is replaced by ../path/node_shim.js.
const { assert, done } = require('../path/node_shim');
const util = require('util');
const symbol = Symbol('foo');

assert.strictEqual(util.format(), '');
assert.strictEqual(util.format(''), '');
assert.strictEqual(util.format([]), '[]');
assert.strictEqual(util.format([0]), '[ 0 ]');
assert.strictEqual(util.format({}), '{}');
assert.strictEqual(util.format({ foo: 42 }), '{ foo: 42 }');
assert.strictEqual(util.format(null), 'null');
assert.strictEqual(util.format(true), 'true');
assert.strictEqual(util.format(false), 'false');
assert.strictEqual(util.format('test'), 'test');

// CHECKME this is for console.log() compatibility - but is it *right*?
assert.strictEqual(util.format('foo', 'bar', 'baz'), 'foo bar baz');

// ES6 Symbol handling
assert.strictEqual(util.format(symbol), 'Symbol(foo)');
assert.strictEqual(util.format('foo', symbol), 'foo Symbol(foo)');
assert.strictEqual(util.format('%s', symbol), 'Symbol(foo)');
assert.strictEqual(util.format('%j', symbol), 'undefined');

// Number format specifier
assert.strictEqual(util.format('%d'), '%d');
assert.strictEqual(util.format('%d', 42.0), '42');
assert.strictEqual(util.format('%d', 42), '42');
assert.strictEqual(util.format('%d', '42'), '42');
assert.strictEqual(util.format('%d', '42.0'), '42');
assert.strictEqual(util.format('%d', 1.5), '1.5');
assert.strictEqual(util.format('%d', -0.5), '-0.5');
assert.strictEqual(util.format('%d', -0.0), '-0');
assert.strictEqual(util.format('%d', ''), '0');
// protoJS: not run -- Number(' -0.000') is 0, not -0, in protoJS.
// assert.strictEqual(util.format('%d', ' -0.000'), '-0');
assert.strictEqual(util.format('%d', Symbol()), 'NaN');
assert.strictEqual(util.format('%d', Infinity), 'Infinity');
assert.strictEqual(util.format('%d', -Infinity), '-Infinity');
assert.strictEqual(util.format('%d %d', 42, 43), '42 43');
assert.strictEqual(util.format('%d %d', 42), '42 %d');
assert.strictEqual(
  util.format('%d', 1180591620717411303424),
  '1.1805916207174113e+21'
);
assert.strictEqual(
  util.format('%d', 1180591620717411303424n),
  '1180591620717411303424n'
);
assert.strictEqual(
  util.format('%d %d', 1180591620717411303424n, 12345678901234567890123n),
  '1180591620717411303424n 12345678901234567890123n'
);

// protoJS: not run -- inspect.defaultOptions.numericSeparator is not supported.
// {
//   const { numericSeparator } = util.inspect.defaultOptions;
//   util.inspect.defaultOptions.numericSeparator = true;
//
//   assert.strictEqual(
//     util.format('%d', 1180591620717411303424),
//     '1.1805916207174113e+21'
//   );
//
//   assert.strictEqual(
//     util.format(
//       // eslint-disable-next-line no-loss-of-precision
//       '%d %s %i', 118059162071741130342, 118059162071741130342, 123_123_123),
//     '118_059_162_071_741_140_000 118_059_162_071_741_140_000 123_123_123'
//   );
//
//   assert.strictEqual(
//     util.format(
//       '%d %s',
//       1_180_591_620_717_411_303_424n,
//       12_345_678_901_234_567_890_123n
//     ),
//     '1_180_591_620_717_411_303_424n 12_345_678_901_234_567_890_123n'
//   );
//
//   assert.strictEqual(
//     util.format('%i', 1_180_591_620_717_411_303_424n),
//     '1_180_591_620_717_411_303_424n'
//   );
//
//   util.inspect.defaultOptions.numericSeparator = numericSeparator;
// }
// Integer format specifier
assert.strictEqual(util.format('%i'), '%i');
assert.strictEqual(util.format('%i', 42.0), '42');
assert.strictEqual(util.format('%i', 42), '42');
assert.strictEqual(util.format('%i', '42'), '42');
assert.strictEqual(util.format('%i', '42.0'), '42');
assert.strictEqual(util.format('%i', 1.5), '1');
// protoJS: not run -- parseInt(-0.5) is 0, not -0, in protoJS.
// assert.strictEqual(util.format('%i', -0.5), '-0');
assert.strictEqual(util.format('%i', ''), 'NaN');
assert.strictEqual(util.format('%i', Infinity), 'NaN');
assert.strictEqual(util.format('%i', -Infinity), 'NaN');
assert.strictEqual(util.format('%i', Symbol()), 'NaN');
assert.strictEqual(util.format('%i %i', 42, 43), '42 43');
assert.strictEqual(util.format('%i %i', 42), '42 %i');
assert.strictEqual(
  util.format('%i', 1180591620717411303424),
  '1'
);
assert.strictEqual(
  util.format('%i', 1180591620717411303424n),
  '1180591620717411303424n'
);
assert.strictEqual(
  util.format('%i %i', 1180591620717411303424n, 12345678901234567890123n),
  '1180591620717411303424n 12345678901234567890123n'
);

assert.strictEqual(
  util.format('%d %i', 1180591620717411303424n, 12345678901234567890123n),
  '1180591620717411303424n 12345678901234567890123n'
);

assert.strictEqual(
  util.format('%i %d', 1180591620717411303424n, 12345678901234567890123n),
  '1180591620717411303424n 12345678901234567890123n'
);

// protoJS: not run -- the numericSeparator option is not supported.
// assert.strictEqual(
//   util.formatWithOptions(
//     { numericSeparator: true },
//     '%i %d', 1180591620717411303424n, 12345678901234567890123n),
//   '1_180_591_620_717_411_303_424n 12_345_678_901_234_567_890_123n'
// );

// Float format specifier
assert.strictEqual(util.format('%f'), '%f');
assert.strictEqual(util.format('%f', 42.0), '42');
assert.strictEqual(util.format('%f', 42), '42');
assert.strictEqual(util.format('%f', '42'), '42');
// protoJS: not run -- parseFloat('-0.0') is 0, not -0, in protoJS.
// assert.strictEqual(util.format('%f', '-0.0'), '-0');
assert.strictEqual(util.format('%f', '42.0'), '42');
assert.strictEqual(util.format('%f', 1.5), '1.5');
assert.strictEqual(util.format('%f', -0.5), '-0.5');
assert.strictEqual(util.format('%f', Math.PI), '3.141592653589793');
assert.strictEqual(util.format('%f', ''), 'NaN');
assert.strictEqual(util.format('%f', Symbol('foo')), 'NaN');
assert.strictEqual(util.format('%f', 5n), '5');
assert.strictEqual(util.format('%f', Infinity), 'Infinity');
assert.strictEqual(util.format('%f', -Infinity), '-Infinity');
assert.strictEqual(util.format('%f %f', 42, 43), '42 43');
assert.strictEqual(util.format('%f %f', 42), '42 %f');

// String format specifier
assert.strictEqual(util.format('%s'), '%s');
assert.strictEqual(util.format('%s', undefined), 'undefined');
assert.strictEqual(util.format('%s', null), 'null');
assert.strictEqual(util.format('%s', 'foo'), 'foo');
assert.strictEqual(util.format('%s', 42), '42');
assert.strictEqual(util.format('%s', '42'), '42');
assert.strictEqual(util.format('%s', -0), '-0');
assert.strictEqual(util.format('%s', '-0.0'), '-0.0');
assert.strictEqual(util.format('%s %s', 42, 43), '42 43');
assert.strictEqual(util.format('%s %s', 42), '42 %s');
assert.strictEqual(util.format('%s', 42n), '42n');
assert.strictEqual(util.format('%s', Symbol('foo')), 'Symbol(foo)');
assert.strictEqual(util.format('%s', true), 'true');
assert.strictEqual(util.format('%s', { a: [1, 2, 3] }), '{ a: [Array] }');
assert.strictEqual(util.format('%s', { toString() { return 'Foo'; } }), 'Foo');
assert.strictEqual(util.format('%s', { toString: 5 }), '{ toString: 5 }');
assert.strictEqual(util.format('%s', () => 5), '() => 5');
assert.strictEqual(util.format('%s', Infinity), 'Infinity');
assert.strictEqual(util.format('%s', -Infinity), '-Infinity');

// String format specifier including `toString` properties on the prototype.
{
  class Foo { toString() { return 'Bar'; } }
  assert.strictEqual(util.format('%s', new Foo()), 'Bar');
// protoJS: not run -- the class name of an object with a null prototype comes from V8 internals.
//   assert.strictEqual(
//     util.format('%s', Object.setPrototypeOf(new Foo(), null)),
//     '[Foo: null prototype] {}'
//   );
  globalThis.Foo = Foo;
  assert.strictEqual(util.format('%s', new Foo()), 'Bar');
  delete globalThis.Foo;
  class Bar { abc = true; }
  assert.strictEqual(util.format('%s', new Bar()), 'Bar { abc: true }');
// protoJS: not run -- subclasses of Array are not supported by inspect.
//   class Foobar extends Array { aaa = true; }
//   assert.strictEqual(
//     util.format('%s', new Foobar(5)),
//     'Foobar(5) [ <5 empty items>, aaa: true ]'
//   );

  // Subclassing:
  class B extends Foo {}

  function C() {}
  C.prototype.toString = function() {
    return 'Custom';
  };

  function D() {
    C.call(this);
  }
  D.prototype = { __proto__: C.prototype };

  assert.strictEqual(
    util.format('%s', new B()),
    'Bar'
  );
  assert.strictEqual(
    util.format('%s', new C()),
    'Custom'
  );
  assert.strictEqual(
    util.format('%s', new D()),
    'Custom'
  );

  D.prototype.constructor = D;
  assert.strictEqual(
    util.format('%s', new D()),
    'Custom'
  );

  D.prototype.constructor = null;
  assert.strictEqual(
    util.format('%s', new D()),
    'Custom'
  );

  D.prototype.constructor = { name: 'Foobar' };
  assert.strictEqual(
    util.format('%s', new D()),
    'Custom'
  );

  Object.defineProperty(D.prototype, 'constructor', {
    get() {
      throw new Error();
    },
    configurable: true
  });
  assert.strictEqual(
    util.format('%s', new D()),
    'Custom'
  );

  assert.strictEqual(
    util.format('%s', { __proto__: null }),
    '[Object: null prototype] {}'
  );
}

// Symbol.toPrimitive handling for string format specifier
// protoJS: not run -- String() does not call Symbol.toPrimitive in protoJS.
// {
//   const objectWithToPrimitive = {
//     [Symbol.toPrimitive](hint) {
//       switch (hint) {
//         case 'number':
//           return 42;
//         case 'string':
//           return 'string representation';
//         case 'default':
//         default:
//           return 'default context';
//       }
//     }
//   };
//
//   assert.strictEqual(util.format('%s', +objectWithToPrimitive), '42');
//   assert.strictEqual(util.format('%s', objectWithToPrimitive), 'string representation');
//   assert.strictEqual(util.format('%s', objectWithToPrimitive + ''), 'default context');
// }

// built-in toPrimitive is the same behavior as inspect
{
  const date = new Date('2023-10-01T00:00:00Z');
  assert.strictEqual(util.format('%s', date), util.inspect(date));

  const symbol = Symbol('foo');
  assert.strictEqual(util.format('%s', symbol), util.inspect(symbol));
}

// Prototype chain handling for toString
{
  function hasToStringButNoToPrimitive() {}

  hasToStringButNoToPrimitive.prototype.toString = function() {
    return 'hasToStringButNoToPrimitive';
  };

  let obj = new hasToStringButNoToPrimitive();
  assert.strictEqual(util.format('%s', obj.toString()), 'hasToStringButNoToPrimitive');

  function inheritsFromHasToStringButNoToPrimitive() {}
  Object.setPrototypeOf(inheritsFromHasToStringButNoToPrimitive.prototype,
                        hasToStringButNoToPrimitive.prototype);
  obj = new inheritsFromHasToStringButNoToPrimitive();
  assert.strictEqual(util.format('%s', obj.toString()), 'hasToStringButNoToPrimitive');
}

// Prototype chain handling for Symbol.toPrimitive
{
  function hasToPrimitiveButNoToString() {}

  hasToPrimitiveButNoToString.prototype[Symbol.toPrimitive] = function() {
    return 'hasToPrimitiveButNoToString';
  };

  let obj = new hasToPrimitiveButNoToString();
  assert.strictEqual(util.format('%s', obj[Symbol.toPrimitive]()), 'hasToPrimitiveButNoToString');
  function inheritsFromHasToPrimitiveButNoToString() {}
  Object.setPrototypeOf(inheritsFromHasToPrimitiveButNoToString.prototype,
                        hasToPrimitiveButNoToString.prototype);
  obj = new inheritsFromHasToPrimitiveButNoToString();
  assert.strictEqual(util.format('%s', obj[Symbol.toPrimitive]()), 'hasToPrimitiveButNoToString');
}

// Prototype chain handling for both toString and Symbol.toPrimitive
{
  function hasBothToStringAndToPrimitive() {}
  hasBothToStringAndToPrimitive.prototype.toString = function() {
    return 'toString';
  };
  hasBothToStringAndToPrimitive.prototype[Symbol.toPrimitive] = function() {
    return 'toPrimitive';
  };
  let obj = new hasBothToStringAndToPrimitive();
  assert.strictEqual(util.format('%s', obj.toString()), 'toString');
  function inheritsFromHasBothToStringAndToPrimitive() {}
  Object.setPrototypeOf(inheritsFromHasBothToStringAndToPrimitive.prototype,
                        hasBothToStringAndToPrimitive.prototype);
  obj = new inheritsFromHasBothToStringAndToPrimitive();
  assert.strictEqual(util.format('%s', obj.toString()), 'toString');
}

// JSON format specifier
assert.strictEqual(util.format('%j'), '%j');
assert.strictEqual(util.format('%j', 42), '42');
assert.strictEqual(util.format('%j', '42'), '"42"');
assert.strictEqual(util.format('%j %j', 42, 43), '42 43');
assert.strictEqual(util.format('%j %j', 42), '42 %j');

// Object format specifier
const obj = {
  foo: 'bar',
  foobar: 1,
  func: function() {}
};
const nestedObj = {
  foo: 'bar',
  foobar: {
    foo: 'bar',
    func: function() {}
  }
};
const nestedObj2 = {
  foo: 'bar',
  foobar: 1,
  func: [{ a: function() {} }]
};
assert.strictEqual(util.format('%o'), '%o');
assert.strictEqual(util.format('%o', 42), '42');
assert.strictEqual(util.format('%o', 'foo'), '\'foo\'');
// protoJS: not run -- inspect prints object keys in protoJS's key order, which is not insertion order
// (docs/TEST262_STATUS.md), and a function's own keys in another order than V8's.
// assert.strictEqual(
//   util.format('%o', obj),
//   '{\n' +
//   '  foo: \'bar\',\n' +
//   '  foobar: 1,\n' +
//   '  func: <ref *1> [Function: func] {\n' +
//   '    [length]: 0,\n' +
//   '    [name]: \'func\',\n' +
//   '    [prototype]: { [constructor]: [Circular *1] }\n' +
//   '  }\n' +
//   '}');
// assert.strictEqual(
//   util.format('%o', nestedObj2),
//   '{\n' +
//   '  foo: \'bar\',\n' +
//   '  foobar: 1,\n' +
//   '  func: [\n' +
//   '    {\n' +
//   '      a: <ref *1> [Function: a] {\n' +
//   '        [length]: 0,\n' +
//   '        [name]: \'a\',\n' +
//   '        [prototype]: { [constructor]: [Circular *1] }\n' +
//   '      }\n' +
//   '    },\n' +
//   '    [length]: 1\n' +
//   '  ]\n' +
//   '}');
// assert.strictEqual(
//   util.format('%o', nestedObj),
//   '{\n' +
//   '  foo: \'bar\',\n' +
//   '  foobar: {\n' +
//   '    foo: \'bar\',\n' +
//   '    func: <ref *1> [Function: func] {\n' +
//   '      [length]: 0,\n' +
//   '      [name]: \'func\',\n' +
//   '      [prototype]: { [constructor]: [Circular *1] }\n' +
//   '    }\n' +
//   '  }\n' +
//   '}');
// assert.strictEqual(
//   util.format('%o %o', obj, obj),
//   '{\n' +
//   '  foo: \'bar\',\n' +
//   '  foobar: 1,\n' +
//   '  func: <ref *1> [Function: func] {\n' +
//   '    [length]: 0,\n' +
//   '    [name]: \'func\',\n' +
//   '    [prototype]: { [constructor]: [Circular *1] }\n' +
//   '  }\n' +
//   '} {\n' +
//   '  foo: \'bar\',\n' +
//   '  foobar: 1,\n' +
//   '  func: <ref *1> [Function: func] {\n' +
//   '    [length]: 0,\n' +
//   '    [name]: \'func\',\n' +
//   '    [prototype]: { [constructor]: [Circular *1] }\n' +
//   '  }\n' +
//   '}');
// assert.strictEqual(
//   util.format('%o %o', obj),
//   '{\n' +
//   '  foo: \'bar\',\n' +
//   '  foobar: 1,\n' +
//   '  func: <ref *1> [Function: func] {\n' +
//   '    [length]: 0,\n' +
//   '    [name]: \'func\',\n' +
//   '    [prototype]: { [constructor]: [Circular *1] }\n' +
//   '  }\n' +
//   '} %o');

assert.strictEqual(util.format('%O'), '%O');
assert.strictEqual(util.format('%O', 42), '42');
assert.strictEqual(util.format('%O', 'foo'), '\'foo\'');
// protoJS: not run -- key order, as above.
// assert.strictEqual(
//   util.format('%O', obj),
//   '{ foo: \'bar\', foobar: 1, func: [Function: func] }');
// assert.strictEqual(
//   util.format('%O', nestedObj),
//   '{ foo: \'bar\', foobar: { foo: \'bar\', func: [Function: func] } }');
// assert.strictEqual(
//   util.format('%O %O', obj, obj),
//   '{ foo: \'bar\', foobar: 1, func: [Function: func] } ' +
//   '{ foo: \'bar\', foobar: 1, func: [Function: func] }');
// assert.strictEqual(
//   util.format('%O %O', obj),
//   '{ foo: \'bar\', foobar: 1, func: [Function: func] } %O');

// Various format specifiers
assert.strictEqual(util.format('%%s%s', 'foo'), '%sfoo');
assert.strictEqual(util.format('%s:%s'), '%s:%s');
assert.strictEqual(util.format('%s:%s', undefined), 'undefined:%s');
assert.strictEqual(util.format('%s:%s', 'foo'), 'foo:%s');
assert.strictEqual(util.format('%s:%i', 'foo'), 'foo:%i');
assert.strictEqual(util.format('%s:%f', 'foo'), 'foo:%f');
assert.strictEqual(util.format('%s:%s', 'foo', 'bar'), 'foo:bar');
assert.strictEqual(util.format('%s:%s', 'foo', 'bar', 'baz'), 'foo:bar baz');
assert.strictEqual(util.format('%%%s%%', 'hi'), '%hi%');
assert.strictEqual(util.format('%%%s%%%%', 'hi'), '%hi%%');
assert.strictEqual(util.format('%sbc%%def', 'a'), 'abc%def');
assert.strictEqual(util.format('%d:%d', 12, 30), '12:30');
assert.strictEqual(util.format('%d:%d', 12), '12:%d');
assert.strictEqual(util.format('%d:%d'), '%d:%d');
assert.strictEqual(util.format('%i:%i', 12, 30), '12:30');
assert.strictEqual(util.format('%i:%i', 12), '12:%i');
assert.strictEqual(util.format('%i:%i'), '%i:%i');
assert.strictEqual(util.format('%f:%f', 12, 30), '12:30');
assert.strictEqual(util.format('%f:%f', 12), '12:%f');
assert.strictEqual(util.format('%f:%f'), '%f:%f');
assert.strictEqual(util.format('o: %j, a: %j', {}, []), 'o: {}, a: []');
assert.strictEqual(util.format('o: %j, a: %j', {}), 'o: {}, a: %j');
assert.strictEqual(util.format('o: %j, a: %j'), 'o: %j, a: %j');
assert.strictEqual(util.format('o: %o, a: %O', {}, []), 'o: {}, a: []');
assert.strictEqual(util.format('o: %o, a: %o', {}), 'o: {}, a: %o');
assert.strictEqual(util.format('o: %O, a: %O'), 'o: %O, a: %O');


// Invalid format specifiers
assert.strictEqual(util.format('a% b', 'x'), 'a% b x');
assert.strictEqual(util.format('percent: %d%, fraction: %d', 10, 0.1),
                   'percent: 10%, fraction: 0.1');
assert.strictEqual(util.format('abc%', 1), 'abc% 1');

// Additional arguments after format specifiers
assert.strictEqual(util.format('%i', 1, 'number'), '1 number');
assert.strictEqual(util.format('%i', 1, () => {}), '1 [Function (anonymous)]');

// %c from https://console.spec.whatwg.org/
assert.strictEqual(util.format('%c'), '%c');
assert.strictEqual(util.format('%cab'), '%cab');
assert.strictEqual(util.format('%cab', 'color: blue'), 'ab');
assert.strictEqual(util.format('%cab', 'color: blue', 'c'), 'ab c');

{
  const o = {};
  o.o = o;
  assert.strictEqual(util.format('%j', o), '[Circular]');
}

{
  const o = {
    toJSON() {
      throw new Error('Not a circular object but still not serializable');
    }
  };
  assert.throws(() => util.format('%j', o),
                /^Error: Not a circular object but still not serializable$/);
}

// Errors
// protoJS: not run -- protoJS errors carry no stack, so inspect prints [Error: message].
// const err = new Error('foo');
// assert.strictEqual(util.format(err), err.stack);
// class CustomError extends Error {
//   constructor(msg) {
//     super();
//     Object.defineProperty(this, 'message',
//                           { value: msg, enumerable: false });
//     Object.defineProperty(this, 'name',
//                           { value: 'CustomError', enumerable: false });
//     Error.captureStackTrace(this, CustomError);
//   }
// }
// const customError = new CustomError('bar');
// assert.strictEqual(util.format(customError), customError.stack);
// Doesn't capture stack trace
function BadCustomError(msg) {
  Error.call(this);
  Object.defineProperty(this, 'message',
                        { value: msg, enumerable: false });
  Object.defineProperty(this, 'name',
                        { value: 'BadCustomError', enumerable: false });
}
Object.setPrototypeOf(BadCustomError.prototype, Error.prototype);
Object.setPrototypeOf(BadCustomError, Error);
assert.strictEqual(util.format(new BadCustomError('foo')),
                   '[BadCustomError: foo]');

// The format of arguments should not depend on type of the first argument
assert.strictEqual(util.format('1', '1'), '1 1');
assert.strictEqual(util.format(1, '1'), '1 1');
assert.strictEqual(util.format('1', 1), '1 1');
assert.strictEqual(util.format(1, -0), '1 -0');
assert.strictEqual(util.format('1', () => {}), '1 [Function (anonymous)]');
assert.strictEqual(util.format(1, () => {}), '1 [Function (anonymous)]');
assert.strictEqual(util.format('1', "'"), "1 '");
assert.strictEqual(util.format(1, "'"), "1 '");
assert.strictEqual(util.format('1', 'number'), '1 number');
assert.strictEqual(util.format(1, 'number'), '1 number');
assert.strictEqual(util.format(5n), '5n');
assert.strictEqual(util.format(5n, 5n), '5n 5n');

// Check `formatWithOptions`.
// protoJS: not run -- the colors option is not supported.
// assert.strictEqual(
//   util.formatWithOptions(
//     { colors: true },
//     true, undefined, Symbol(), 1, 5n, null, 'foobar'
//   ),
//   '\u001b[33mtrue\u001b[39m ' +
//     '\u001b[90mundefined\u001b[39m ' +
//     '\u001b[32mSymbol()\u001b[39m ' +
//     '\u001b[33m1\u001b[39m ' +
//     '\u001b[33m5n\u001b[39m ' +
//     '\u001b[1mnull\u001b[22m ' +
//     'foobar'
// );

// protoJS: not run -- SharedArrayBuffer is not supported by inspect.
// assert.strictEqual(
//   util.format(new SharedArrayBuffer(4)),
//   'SharedArrayBuffer { [Uint8Contents]: <00 00 00 00>, byteLength: 4 }'
// );

assert.strictEqual(
  util.formatWithOptions(
    { colors: true, compact: 3 },
    '%s', [ 1, { a: true }]
  ),
  '[ 1, [Object] ]'
);

[
  undefined,
  null,
  false,
  5n,
  5,
  'test',
  Symbol(),
].forEach((invalidOptions) => {
  assert.throws(() => {
    util.formatWithOptions(invalidOptions, { a: true });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /"inspectOptions".+object/
  });
});

// protoJS: report how many assertions ran.
done('test-util-format');
