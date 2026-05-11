// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of assertion functions used throughout test262
defines: [assert]
---*/


function assert(mustBeTrue, message) {
  if (mustBeTrue === true) {
    return;
  }

  if (message === undefined) {
    message = 'Expected true but got ' + assert._toString(mustBeTrue);
  }
  throw new Test262Error(message);
}

assert._isSameValue = function (a, b) {
  if (a === b) {
    // Handle +/-0 vs. -/+0
    return a !== 0 || 1 / a === 1 / b;
  }

  // Handle NaN vs. NaN
  return a !== a && b !== b;
};

assert.sameValue = function (actual, expected, message) {
  try {
    if (assert._isSameValue(actual, expected)) {
      return;
    }
  } catch (error) {
    throw new Test262Error(message + ' (_isSameValue operation threw) ' + error);
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(expected) + '») to be true';

  throw new Test262Error(message);
};

assert.notSameValue = function (actual, unexpected, message) {
  if (!assert._isSameValue(actual, unexpected)) {
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(unexpected) + '») to be false';

  throw new Test262Error(message);
};

assert.throws = function (expectedErrorConstructor, func, message) {
  var expectedName, actualName;
  if (typeof func !== "function") {
    throw new Test262Error('assert.throws requires two arguments: the error constructor ' +
      'and a function to run');
    return;
  }
  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  try {
    func();
  } catch (thrown) {
    if (typeof thrown !== 'object' || thrown === null) {
      message += 'Thrown value was not an object!';
      throw new Test262Error(message);
    } else if (thrown.constructor !== expectedErrorConstructor) {
      expectedName = expectedErrorConstructor.name;
      actualName = thrown.constructor.name;
      if (expectedName === actualName) {
        message += 'Expected a ' + expectedName + ' but got a different error constructor with the same name';
      } else {
        message += 'Expected a ' + expectedName + ' but got a ' + actualName;
      }
      throw new Test262Error(message);
    }
    return;
  }

  message += 'Expected a ' + expectedErrorConstructor.name + ' to be thrown but no exception was thrown at all';
  throw new Test262Error(message);
};

function isPrimitive(value) {
  return !value || (typeof value !== 'object' && typeof value !== 'function');
}

assert.compareArray = function (actual, expected, message) {
  message = message === undefined ? '' : message;

  if (typeof message === 'symbol') {
    message = message.toString();
  }

  if (isPrimitive(actual)) {
    assert(false, `Actual argument [${actual}] shouldn't be primitive. ${message}`);
  } else if (isPrimitive(expected)) {
    assert(false, `Expected argument [${expected}] shouldn't be primitive. ${message}`);
  }
  var result = compareArray(actual, expected);
  if (result) return;

  var format = compareArray.format;
  assert(false, `Actual ${format(actual)} and expected ${format(expected)} should have the same contents. ${message}`);
};

function compareArray(a, b) {
  if (b.length !== a.length) {
    return false;
  }
  for (var i = 0; i < a.length; i++) {
    if (!assert._isSameValue(b[i], a[i])) {
      return false;
    }
  }
  return true;
}

compareArray.format = function (arrayLike) {
  return `[${Array.prototype.map.call(arrayLike, String).join(', ')}]`;
};

assert._formatIdentityFreeValue = function formatIdentityFreeValue(value) {
  switch (value === null ? 'null' : typeof value) {
    case 'string':
      return typeof JSON !== "undefined" ? JSON.stringify(value) : `"${value}"`;
    case 'bigint':
      return `${value}n`;
    case 'number':
      if (value === 0 && 1 / value === -Infinity) return '-0';
      // falls through
    case 'boolean':
    case 'undefined':
    case 'null':
      return String(value);
  }
};

assert._toString = function (value) {
  var basic = assert._formatIdentityFreeValue(value);
  if (basic) return basic;
  try {
    return String(value);
  } catch (err) {
    if (err.name === 'TypeError') {
      return Object.prototype.toString.call(value);
    }
    throw err;
  }
};


// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Provides both:

    - An error class to avoid false positives when testing for thrown exceptions
    - A function to explicitly throw an exception using the Test262Error class
defines: [Test262Error, $DONOTEVALUATE]
---*/


function Test262Error(message) {
  this.message = message || "";
}

Test262Error.prototype.toString = function () {
  return "Test262Error: " + this.message;
};

Test262Error.thrower = function (message) {
  throw new Test262Error(message);
};

function $DONOTEVALUATE() {
  throw "Test262: This statement should not be evaluated.";
}


// Copyright 2019 Google, Inc.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: prod-OptionalExpression
description: >
  optional chain on member expression
info: |
  Left-Hand-Side Expressions
    OptionalExpression:
      MemberExpression OptionalChain
features: [optional-chaining]
---*/

// PrimaryExpression
//   IdentifierReference
const a = {b: 22};
assert.sameValue(22, a?.b);
//   this
function fn () {
  return this?.a
}
assert.sameValue(33, fn.call({a: 33}));
//   Literal
assert.sameValue(undefined, "hello"?.a);
assert.sameValue(undefined, null?.a);
//   ArrayLiteral
assert.sameValue(2, [1, 2]?.[1]);
//   ObjectLiteral
assert.sameValue(44, {a: 44}?.a);
//   FunctionExpression
assert.sameValue('a', (function a () {}?.name));
//   ClassExpression
assert.sameValue('Foo', (class Foo {}?.name));
//   GeneratorFunction
assert.sameValue('a', (function * a () {}?.name));
//   AsyncFunctionExpression
assert.sameValue('a', (async function a () {}?.name));
//   AsyncGeneratorExpression
assert.sameValue('a', (async function * a () {}?.name));
//   RegularExpressionLiteral
assert.sameValue(true, /[a-z]/?.test('a'));
//   TemplateLiteral
assert.sameValue('h', `hello`?.[0]);
//   CoverParenthesizedExpressionAndArrowParameterList
assert.sameValue(undefined, ({a: 33}, null)?.a);
assert.sameValue(33, (undefined, {a: 33})?.a);

//  MemberExpression [ Expression ]
const arr = [{a: 33}];
assert.sameValue(33, arr[0]?.a);
assert.sameValue(undefined, arr[1]?.a);

//  MemberExpression .IdentifierName
const obj = {a: {b: 44}};
assert.sameValue(44, obj.a?.b);
assert.sameValue(undefined, obj.c?.b);

//  MemberExpression TemplateLiteral
function f2 () {
  return {a: 33};
}
function f3 () {}
assert.sameValue(33, f2`hello world`?.a);
assert.sameValue(undefined, f3`hello world`?.a);

//  MemberExpression SuperProperty
class A {
  a () {}
  undf () {
    return super.a?.c;
  }
}
class B extends A {
  dot () {
    return super.a?.name;
  }
  expr () {
    return super['a']?.name;
  }
  undf2 () {
    return super.b?.c;
  }
}
const subcls = new B();
assert.sameValue('a', subcls.dot());
assert.sameValue('a', subcls.expr());
assert.sameValue(undefined, subcls.undf2());
assert.sameValue(undefined, (new A()).undf());

// MemberExpression MetaProperty
class C {
  constructor () {
    assert.sameValue(undefined, new.target?.a);
  }
}
new C();

// new MemberExpression Arguments
class D {
  constructor (val) {
    this.a = val;
  }
}
assert.sameValue(99, new D(99)?.a);
