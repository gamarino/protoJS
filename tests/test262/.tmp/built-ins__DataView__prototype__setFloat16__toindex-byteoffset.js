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


// Copyright (C) 2024 Kevin Gibbons. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-dataview.prototype.setfloat16
description: >
  ToIndex conversions on byteOffset
features: [Float16Array]
---*/

var buffer = new ArrayBuffer(6);
var sample = new DataView(buffer, 0);

var obj1 = {
  valueOf: function() {
    return 3;
  }
};

var obj2 = {
  toString: function() {
    return 4;
  }
};

sample.setFloat16(0, 0);
sample.setFloat16(-0, 42);
assert.sameValue(sample.getFloat16(0), 42, "-0");

sample.setFloat16(3, 0);
sample.setFloat16(obj1, 42);
assert.sameValue(sample.getFloat16(3), 42, "object's valueOf");

sample.setFloat16(4, 0);
sample.setFloat16(obj2, 42);
assert.sameValue(sample.getFloat16(4), 42, "object's toString");

sample.setFloat16(0, 0);
sample.setFloat16("", 42);
assert.sameValue(sample.getFloat16(0), 42, "the Empty string");

sample.setFloat16(0, 0);
sample.setFloat16("0", 42);
assert.sameValue(sample.getFloat16(0), 42, "string '0'");

sample.setFloat16(2, 0);
sample.setFloat16("2", 42);
assert.sameValue(sample.getFloat16(2), 42, "string '2'");

sample.setFloat16(1, 0);
sample.setFloat16(true, 42);
assert.sameValue(sample.getFloat16(1), 42, "true");

sample.setFloat16(0, 0);
sample.setFloat16(false, 42);
assert.sameValue(sample.getFloat16(0), 42, "false");

sample.setFloat16(0, 0);
sample.setFloat16(NaN, 42);
assert.sameValue(sample.getFloat16(0), 42, "NaN");

sample.setFloat16(0, 0);
sample.setFloat16(null, 42);
assert.sameValue(sample.getFloat16(0), 42, "null");

sample.setFloat16(0, 0);
sample.setFloat16(0.1, 42);
assert.sameValue(sample.getFloat16(0), 42, "0.1");

sample.setFloat16(0, 0);
sample.setFloat16(0.9, 42);
assert.sameValue(sample.getFloat16(0), 42, "0.9");

sample.setFloat16(1, 0);
sample.setFloat16(1.1, 42);
assert.sameValue(sample.getFloat16(1), 42, "1.1");

sample.setFloat16(1, 0);
sample.setFloat16(1.9, 42);
assert.sameValue(sample.getFloat16(1), 42, "1.9");

sample.setFloat16(0, 0);
sample.setFloat16(-0.1, 42);
assert.sameValue(sample.getFloat16(0), 42, "-0.1");

sample.setFloat16(0, 0);
sample.setFloat16(-0.99999, 42);
assert.sameValue(sample.getFloat16(0), 42, "-0.99999");

sample.setFloat16(0, 0);
sample.setFloat16(undefined, 42);
assert.sameValue(sample.getFloat16(0), 42, "undefined");

sample.setFloat16(0, 7);
sample.setFloat16();
assert.sameValue(sample.getFloat16(0), NaN, "no arg");
