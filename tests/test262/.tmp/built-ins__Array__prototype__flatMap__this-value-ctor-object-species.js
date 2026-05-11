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


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Deprecated now that compareArray is defined in assert.js.
defines: [compareArray]
---*/


// Copyright (C) 2018 Leo Balter. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-array.prototype.flatmap
description: >
  Assert behavior if this value has a custom object constructor property
info: |
  Array.prototype.flatMap ( mapperFunction [ , thisArg ] )

  1. Let O be ? ToObject(this value).
  2. Let sourceLen be ? ToLength(? Get(O, "length")).
  ...
  5. Let A be ? ArraySpeciesCreate(O, 0).
  ...

  ArraySpeciesCreate ( originalArray, length )

  3. Let isArray be ? IsArray(originalArray).
  4. If isArray is false, return ? ArrayCreate(length).
  5. Let C be ? Get(originalArray, "constructor").
  6. If IsConstructor(C) is true, then
    a. Let thisRealm be the current Realm Record.
    b. Let realmC be ? GetFunctionRealm(C).
    c. If thisRealm and realmC are not the same Realm Record, then
      i. If SameValue(C, realmC.[[Intrinsics]].[[%Array%]]) is true, set C to undefined.
  7. If Type(C) is Object, then
    a. Set C to ? Get(C, @@species).
    b. If C is null, set C to undefined.
  8. If C is undefined, return ? ArrayCreate(length).
  9. If IsConstructor(C) is false, throw a TypeError exception.
  10. Return ? Construct(C, « length »).
features: [Array.prototype.flatMap, Symbol, Symbol.species]
includes: [compareArray.js]
---*/

assert.sameValue(
  typeof Array.prototype.flatMap,
  'function',
  'The value of `typeof Array.prototype.flatMap` is expected to be "function"'
);

var arr = [[42, 1], [42, 2]];
var mapperFn = function(e) { return e; };

arr.constructor = {};
var actual = arr.flatMap(mapperFn);
assert.compareArray(actual, [42, 1, 42, 2], 'The value of actual is expected to be [42, 1, 42, 2]');
assert.sameValue(
  Object.getPrototypeOf(actual),
  Array.prototype,
  'Object.getPrototypeOf(arr.flatMap(mapperFn)) returns Array.prototype'
);

var called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return null;
  }
};
actual = arr.flatMap(mapperFn);
assert.compareArray(actual, [42, 1, 42, 2], 'The value of actual is expected to be [42, 1, 42, 2]');
assert.sameValue(
  Object.getPrototypeOf(actual),
  Array.prototype,
  'Object.getPrototypeOf(arr.flatMap(mapperFn)) returns Array.prototype'
);
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return undefined;
  }
};
actual = arr.flatMap(mapperFn);
assert.compareArray(actual, [42, 1, 42, 2], 'The value of actual is expected to be [42, 1, 42, 2]');
assert.sameValue(
  Object.getPrototypeOf(actual),
  Array.prototype,
  'Object.getPrototypeOf(arr.flatMap(mapperFn)) returns Array.prototype'
);
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return 0;
  }
};
assert.throws(TypeError, function() {
  arr.flatMap(mapperFn);
}, 'arr.flatMap(mapperFn) throws a TypeError exception');
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return '';
  }
};
assert.throws(TypeError, function() {
  arr.flatMap(mapperFn);
}, 'arr.flatMap(mapperFn) throws a TypeError exception');
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return false;
  }
};
assert.throws(TypeError, function() {
  arr.flatMap(mapperFn);
}, 'arr.flatMap(mapperFn) throws a TypeError exception');
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return {};
  }
};
assert.throws(TypeError, function() {
  arr.flatMap(mapperFn);
}, 'arr.flatMap(mapperFn) throws a TypeError exception');
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return [];
  }
};
assert.throws(TypeError, function() {
  arr.flatMap(mapperFn);
}, 'arr.flatMap(mapperFn) throws a TypeError exception');
assert.sameValue(called, 1, 'The value of called is expected to be 1');

called = 0;
arr.constructor = {
  get [Symbol.species]() {
    called++;
    return Symbol();
  }
};
assert.throws(TypeError, function() {
  arr.flatMap(mapperFn);
}, 'arr.flatMap(mapperFn) throws a TypeError exception');
assert.sameValue(called, 1, 'The value of called is expected to be 1');
