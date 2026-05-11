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


// Copyright (C) 2019 Leo Balter. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-object.create
description: >
  The Properties argument is cast to an object if not undefined
info: |
  Object.create ( O, Properties )

  3. If Properties is not undefined, then
    a. Return ? ObjectDefineProperties(obj, Properties).

  Runtime Semantics: ObjectDefineProperties ( O, Properties )

  2. Let props be ? ToObject(Properties).
  3. Let keys be ? props.[[OwnPropertyKeys]]().
  ...
  // All enumerable keys are added to the created object.
features: [Symbol]
---*/

var proto = {};

var obj;
obj = Object.create(proto, true);
assert.sameValue(Object.getPrototypeOf(obj), proto, 'Properties is true: prototype is set');
assert.sameValue(Object.getOwnPropertyNames(obj).length, 0, 'Properties is true: no keys set');
assert.sameValue(Object.getOwnPropertySymbols(obj).length, 0, 'Properties is true: no symbol keys set');

obj = undefined;
obj = Object.create(proto, false);
assert.sameValue(Object.getPrototypeOf(obj), proto, 'Properties is false: prototype is set');
assert.sameValue(Object.getOwnPropertyNames(obj).length, 0, 'Properties is false: no keys set');
assert.sameValue(Object.getOwnPropertySymbols(obj).length, 0, 'Properties is false: no symbol keys set');

obj = undefined;
obj = Object.create(proto, 1);
assert.sameValue(Object.getPrototypeOf(obj), proto, 'Properties is 1: prototype is set');
assert.sameValue(Object.getOwnPropertyNames(obj).length, 0, 'Properties is 1: no keys set');
assert.sameValue(Object.getOwnPropertySymbols(obj).length, 0, 'Properties is 1: no symbol keys set');

obj = undefined;
obj = Object.create(proto, NaN);
assert.sameValue(Object.getPrototypeOf(obj), proto, 'Properties is NaN: prototype is set');
assert.sameValue(Object.getOwnPropertyNames(obj).length, 0, 'Properties is NaN: no keys set');
assert.sameValue(Object.getOwnPropertySymbols(obj).length, 0, 'Properties is NaN: no symbol keys set');

obj = undefined;
obj = Object.create(proto, '');
assert.sameValue(Object.getPrototypeOf(obj), proto, 'Properties is the empty string: prototype is set');
assert.sameValue(Object.getOwnPropertyNames(obj).length, 0, 'Properties is the empty string: no keys set');
assert.sameValue(Object.getOwnPropertySymbols(obj).length, 0, 'Properties is the empty string: no symbol keys set');

obj = undefined;
obj = Object.create(proto, Symbol('s'));
assert.sameValue(Object.getPrototypeOf(obj), proto, 'Properties is symbol: prototype is set');
assert.sameValue(Object.getOwnPropertyNames(obj).length, 0, 'Properties is symbol: no keys set');
assert.sameValue(Object.getOwnPropertySymbols(obj).length, 0, 'Properties is symbol: no symbol keys set');
