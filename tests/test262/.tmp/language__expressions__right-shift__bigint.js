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


// Copyright (C) 2017 Josh Wolfe. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Right shift for BigInt values
esid: sec-numeric-types-bigint-signedRightShift
info: |
  BigInt::signedRightShift (x, y)

  The abstract operation BigInt::signedRightShift with arguments x and y of type BigInt:

  1. Return BigInt::leftShift(x, -y).

  sec-numeric-types-bigint-leftShift
  BigInt::leftShift (x, y)

  The abstract operation BigInt::leftShift with two arguments x and y of BigInt:

  1. If y < 0,
    a. Return a BigInt representing x divided by 2-y, rounding down to the nearest integer, including for negative numbers.
  2. Return a BigInt representing x multiplied by 2y.

  NOTE: Semantics here should be equivalent to a bitwise shift, treating the BigInt as an infinite length string of binary two's complement digits.

features: [BigInt]
---*/

assert.sameValue(0n >> 0n, 0n, "0n >> 0n === 0n");
assert.sameValue(0b101n >> -1n, 0b1010n, "0b101n >> -1n === 0b1010n");
assert.sameValue(0b101n >> -2n, 0b10100n, "0b101n >> -2n === 0b10100n");
assert.sameValue(0b101n >> -3n, 0b101000n, "0b101n >> -3n === 0b101000n");
assert.sameValue(0b101n >> 1n, 0b10n, "0b101n >> 1n === 0b10n");
assert.sameValue(0b101n >> 2n, 1n, "0b101n >> 2n === 1n");
assert.sameValue(0b101n >> 3n, 0n, "0b101n >> 3n === 0n");
assert.sameValue(0n >> -128n, 0n, "0n >> -128n === 0n");
assert.sameValue(0n >> 128n, 0n, "0n >> 128n === 0n");
assert.sameValue(0x246n >> 0n, 0x246n, "0x246n >> 0n === 0x246n");
assert.sameValue(0x246n >> -127n, 0x12300000000000000000000000000000000n, "0x246n >> -127n === 0x12300000000000000000000000000000000n");
assert.sameValue(0x246n >> -128n, 0x24600000000000000000000000000000000n, "0x246n >> -128n === 0x24600000000000000000000000000000000n");
assert.sameValue(0x246n >> -129n, 0x48c00000000000000000000000000000000n, "0x246n >> -129n === 0x48c00000000000000000000000000000000n");
assert.sameValue(0x246n >> 128n, 0n, "0x246n >> 128n === 0n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> -64n, 0x123456789abcdef0fedcba98765432123456780000000000000000n,
  "0x123456789abcdef0fedcba9876543212345678n >> -64n === 0x123456789abcdef0fedcba98765432123456780000000000000000n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> -32n, 0x123456789abcdef0fedcba987654321234567800000000n,
  "0x123456789abcdef0fedcba9876543212345678n >> -32n === 0x123456789abcdef0fedcba987654321234567800000000n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> -16n, 0x123456789abcdef0fedcba98765432123456780000n,
  "0x123456789abcdef0fedcba9876543212345678n >> -16n === 0x123456789abcdef0fedcba98765432123456780000n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 0n, 0x123456789abcdef0fedcba9876543212345678n,
  "0x123456789abcdef0fedcba9876543212345678n >> 0n === 0x123456789abcdef0fedcba9876543212345678n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 16n, 0x123456789abcdef0fedcba987654321234n,
  "0x123456789abcdef0fedcba9876543212345678n >> 16n === 0x123456789abcdef0fedcba987654321234n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 32n, 0x123456789abcdef0fedcba98765432n,
  "0x123456789abcdef0fedcba9876543212345678n >> 32n === 0x123456789abcdef0fedcba98765432n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 64n, 0x123456789abcdef0fedcban,
  "0x123456789abcdef0fedcba9876543212345678n >> 64n === 0x123456789abcdef0fedcban");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 127n, 0x2468acn,
  "0x123456789abcdef0fedcba9876543212345678n >> 127n === 0x2468acn");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 128n, 0x123456n,
  "0x123456789abcdef0fedcba9876543212345678n >> 128n === 0x123456n");
assert.sameValue(
  0x123456789abcdef0fedcba9876543212345678n >> 129n, 0x91a2bn,
  "0x123456789abcdef0fedcba9876543212345678n >> 129n === 0x91a2bn");
assert.sameValue(-5n >> -1n, -0xan, "-5n >> -1n === -0xan");
assert.sameValue(-5n >> -2n, -0x14n, "-5n >> -2n === -0x14n");
assert.sameValue(-5n >> -3n, -0x28n, "-5n >> -3n === -0x28n");
assert.sameValue(-5n >> 1n, -3n, "-5n >> 1n === -3n");
assert.sameValue(-5n >> 2n, -2n, "-5n >> 2n === -2n");
assert.sameValue(-5n >> 3n, -1n, "-5n >> 3n === -1n");
assert.sameValue(-1n >> -128n, -0x100000000000000000000000000000000n, "-1n >> -128n === -0x100000000000000000000000000000000n");
assert.sameValue(-1n >> 0n, -1n, "-1n >> 0n === -1n");
assert.sameValue(-1n >> 128n, -1n, "-1n >> 128n === -1n");
assert.sameValue(-0x246n >> 0n, -0x246n, "-0x246n >> 0n === -0x246n");
assert.sameValue(-0x246n >> -127n, -0x12300000000000000000000000000000000n, "-0x246n >> -127n === -0x12300000000000000000000000000000000n");
assert.sameValue(-0x246n >> -128n, -0x24600000000000000000000000000000000n, "-0x246n >> -128n === -0x24600000000000000000000000000000000n");
assert.sameValue(-0x246n >> -129n, -0x48c00000000000000000000000000000000n, "-0x246n >> -129n === -0x48c00000000000000000000000000000000n");
assert.sameValue(-0x246n >> 128n, -1n, "-0x246n >> 128n === -1n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> -64n, -0x123456789abcdef0fedcba98765432123456780000000000000000n,
  "-0x123456789abcdef0fedcba9876543212345678n >> -64n === -0x123456789abcdef0fedcba98765432123456780000000000000000n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> -32n, -0x123456789abcdef0fedcba987654321234567800000000n,
  "-0x123456789abcdef0fedcba9876543212345678n >> -32n === -0x123456789abcdef0fedcba987654321234567800000000n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> -16n, -0x123456789abcdef0fedcba98765432123456780000n,
  "-0x123456789abcdef0fedcba9876543212345678n >> -16n === -0x123456789abcdef0fedcba98765432123456780000n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 0n, -0x123456789abcdef0fedcba9876543212345678n,
  "-0x123456789abcdef0fedcba9876543212345678n >> 0n === -0x123456789abcdef0fedcba9876543212345678n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 16n, -0x123456789abcdef0fedcba987654321235n,
  "-0x123456789abcdef0fedcba9876543212345678n >> 16n === -0x123456789abcdef0fedcba987654321235n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 32n, -0x123456789abcdef0fedcba98765433n,
  "-0x123456789abcdef0fedcba9876543212345678n >> 32n === -0x123456789abcdef0fedcba98765433n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 64n, -0x123456789abcdef0fedcbbn,
  "-0x123456789abcdef0fedcba9876543212345678n >> 64n === -0x123456789abcdef0fedcbbn");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 127n, -0x2468adn,
  "-0x123456789abcdef0fedcba9876543212345678n >> 127n === -0x2468adn");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 128n, -0x123457n,
  "-0x123456789abcdef0fedcba9876543212345678n >> 128n === -0x123457n");
assert.sameValue(
  -0x123456789abcdef0fedcba9876543212345678n >> 129n, -0x91a2cn,
  "-0x123456789abcdef0fedcba9876543212345678n >> 129n === -0x91a2cn");
