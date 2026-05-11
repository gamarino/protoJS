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


// Copyright (C) 2019 Tiancheng "Timothy" Gu. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-math.fround
description: Math.fround should use roundTiesToEven for conversion to binary32.
---*/

// We test five values against Math.fround, with their binary64 representation
// shown:
// a0 := 1.0                = 0x1p+0
// a1 := 1.0000000596046448 = 0x1.000001p+0
// a2 := 1.0000001192092896 = 0x1.000002p+0
// a3 := 1.0000001788139343 = 0x1.000003p+0
// a4 := 1.000000238418579  = 0x1.000004p+0
// a5 := 1.0000002980232239 = 0x1.000005p+0
// a6 := 1.0000003576278687 = 0x1.000006p+0
// (Note: they are separated by 2 ** -24.)
//
// a0, a2, a4, and a6 are all representable exactly in binary32; however, while
// a0 and a4 have even mantissas in binary32, a2 and a6 have an odd mantissa
// when represented in that way.
//
// a1 is exactly halfway between a0 and a2, a3 between a2 and a4, and a5
// between a4 and a6. By roundTiesToEven, Math.fround should favor a0 and a4
// over a2 when they are equally close, and a4 over a6 when they are equally
// close.

var a0 = 1.0;
var a1 = 1.0000000596046448;
var a2 = 1.0000001192092896;
var a3 = 1.0000001788139343;
var a4 = 1.000000238418579;
var a5 = 1.0000002980232239;
var a6 = 1.0000003576278687;

assert.sameValue(Math.fround(a0), a0, 'Math.fround(a0)');
assert.sameValue(Math.fround(a1), a0, 'Math.fround(a1)');
assert.sameValue(Math.fround(a2), a2, 'Math.fround(a2)');
assert.sameValue(Math.fround(a3), a4, 'Math.fround(a3)');
assert.sameValue(Math.fround(a4), a4, 'Math.fround(a4)');
assert.sameValue(Math.fround(a5), a4, 'Math.fround(a5)');
assert.sameValue(Math.fround(a6), a6, 'Math.fround(a6)');

assert.sameValue(Math.fround(-a0), -a0, 'Math.fround(-a0)');
assert.sameValue(Math.fround(-a1), -a0, 'Math.fround(-a1)');
assert.sameValue(Math.fround(-a2), -a2, 'Math.fround(-a2)');
assert.sameValue(Math.fround(-a3), -a4, 'Math.fround(-a3)');
assert.sameValue(Math.fround(-a4), -a4, 'Math.fround(-a4)');
assert.sameValue(Math.fround(-a5), -a4, 'Math.fround(-a5)');
assert.sameValue(Math.fround(-a6), -a6, 'Math.fround(-a6)');
