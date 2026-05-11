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
description: Bitwise AND for BigInt values
esid: sec-bitwise-op
info: |
  BitwiseOp(op, x, y)

  1. Let result be 0.
  2. Let shift be 0.
  3. Repeat, until (x = 0 or x = -1) and (y = 0 or y = -1),
    a. Let xDigit be x modulo 2.
    b. Let yDigit be y modulo 2.
    c. Let result be result + 2**shift * op(xDigit, yDigit)
    d. Let shift be shift + 1.
    e. Let x be (x - xDigit) / 2.
    f. Let y be (y - yDigit) / 2.
  4. If op(x modulo 2, y modulo 2) ≠ 0,
    a. Let result be result - 2**shift. NOTE: This extends the sign.
  5. Return result.

features: [BigInt]
---*/

assert.sameValue(0b00n & 0b00n, 0b00n, "0b00n & 0b00n === 0b00n");
assert.sameValue(0b00n & 0b01n, 0b00n, "0b00n & 0b01n === 0b00n");
assert.sameValue(0b01n & 0b00n, 0b00n, "0b01n & 0b00n === 0b00n");
assert.sameValue(0b00n & 0b10n, 0b00n, "0b00n & 0b10n === 0b00n");
assert.sameValue(0b10n & 0b00n, 0b00n, "0b10n & 0b00n === 0b00n");
assert.sameValue(0b00n & 0b11n, 0b00n, "0b00n & 0b11n === 0b00n");
assert.sameValue(0b11n & 0b00n, 0b00n, "0b11n & 0b00n === 0b00n");
assert.sameValue(0b01n & 0b01n, 0b01n, "0b01n & 0b01n === 0b01n");
assert.sameValue(0b01n & 0b10n, 0b00n, "0b01n & 0b10n === 0b00n");
assert.sameValue(0b10n & 0b01n, 0b00n, "0b10n & 0b01n === 0b00n");
assert.sameValue(0b01n & 0b11n, 0b01n, "0b01n & 0b11n === 0b01n");
assert.sameValue(0b11n & 0b01n, 0b01n, "0b11n & 0b01n === 0b01n");
assert.sameValue(0b10n & 0b10n, 0b10n, "0b10n & 0b10n === 0b10n");
assert.sameValue(0b10n & 0b11n, 0b10n, "0b10n & 0b11n === 0b10n");
assert.sameValue(0b11n & 0b10n, 0b10n, "0b11n & 0b10n === 0b10n");
assert.sameValue(0xffffffffn & 0n, 0n, "0xffffffffn & 0n === 0n");
assert.sameValue(0n & 0xffffffffn, 0n, "0n & 0xffffffffn === 0n");
assert.sameValue(0xffffffffn & 0xffffffffn, 0xffffffffn, "0xffffffffn & 0xffffffffn === 0xffffffffn");
assert.sameValue(0xffffffffffffffffn & 0n, 0n, "0xffffffffffffffffn & 0n === 0n");
assert.sameValue(0n & 0xffffffffffffffffn, 0n, "0n & 0xffffffffffffffffn === 0n");
assert.sameValue(0xffffffffffffffffn & 0xffffffffn, 0xffffffffn, "0xffffffffffffffffn & 0xffffffffn === 0xffffffffn");
assert.sameValue(0xffffffffn & 0xffffffffffffffffn, 0xffffffffn, "0xffffffffn & 0xffffffffffffffffn === 0xffffffffn");
assert.sameValue(
  0xffffffffffffffffn & 0xffffffffffffffffn, 0xffffffffffffffffn,
  "0xffffffffffffffffn & 0xffffffffffffffffn === 0xffffffffffffffffn");
assert.sameValue(
  0xbf2ed51ff75d380fd3be813ec6185780n & 0x4aabef2324cedff5387f1f65n, 0x42092803008e813400181700n,
  "0xbf2ed51ff75d380fd3be813ec6185780n & 0x4aabef2324cedff5387f1f65n === 0x42092803008e813400181700n");
assert.sameValue(
  0x4aabef2324cedff5387f1f65n & 0xbf2ed51ff75d380fd3be813ec6185780n, 0x42092803008e813400181700n,
  "0x4aabef2324cedff5387f1f65n & 0xbf2ed51ff75d380fd3be813ec6185780n === 0x42092803008e813400181700n");
assert.sameValue(0n & -1n, 0n, "0n & -1n === 0n");
assert.sameValue(-1n & 0n, 0n, "-1n & 0n === 0n");
assert.sameValue(0n & -2n, 0n, "0n & -2n === 0n");
assert.sameValue(-2n & 0n, 0n, "-2n & 0n === 0n");
assert.sameValue(1n & -2n, 0n, "1n & -2n === 0n");
assert.sameValue(-2n & 1n, 0n, "-2n & 1n === 0n");
assert.sameValue(2n & -2n, 2n, "2n & -2n === 2n");
assert.sameValue(-2n & 2n, 2n, "-2n & 2n === 2n");
assert.sameValue(2n & -3n, 0n, "2n & -3n === 0n");
assert.sameValue(-3n & 2n, 0n, "-3n & 2n === 0n");
assert.sameValue(-1n & -2n, -2n, "-1n & -2n === -2n");
assert.sameValue(-2n & -1n, -2n, "-2n & -1n === -2n");
assert.sameValue(-2n & -2n, -2n, "-2n & -2n === -2n");
assert.sameValue(-2n & -3n, -4n, "-2n & -3n === -4n");
assert.sameValue(-3n & -2n, -4n, "-3n & -2n === -4n");
assert.sameValue(0xffffffffn & -1n, 0xffffffffn, "0xffffffffn & -1n === 0xffffffffn");
assert.sameValue(-1n & 0xffffffffn, 0xffffffffn, "-1n & 0xffffffffn === 0xffffffffn");
assert.sameValue(0xffffffffffffffffn & -1n, 0xffffffffffffffffn, "0xffffffffffffffffn & -1n === 0xffffffffffffffffn");
assert.sameValue(-1n & 0xffffffffffffffffn, 0xffffffffffffffffn, "-1n & 0xffffffffffffffffn === 0xffffffffffffffffn");
assert.sameValue(
  0xbf2ed51ff75d380fd3be813ec6185780n & -0x4aabef2324cedff5387f1f65n, 0xbf2ed51fb554100cd330000ac6004080n,
  "0xbf2ed51ff75d380fd3be813ec6185780n & -0x4aabef2324cedff5387f1f65n === 0xbf2ed51fb554100cd330000ac6004080n");
assert.sameValue(
  -0x4aabef2324cedff5387f1f65n & 0xbf2ed51ff75d380fd3be813ec6185780n, 0xbf2ed51fb554100cd330000ac6004080n,
  "-0x4aabef2324cedff5387f1f65n & 0xbf2ed51ff75d380fd3be813ec6185780n === 0xbf2ed51fb554100cd330000ac6004080n");
assert.sameValue(
  -0xbf2ed51ff75d380fd3be813ec6185780n & 0x4aabef2324cedff5387f1f65n, 0x8a2c72024405ec138670800n,
  "-0xbf2ed51ff75d380fd3be813ec6185780n & 0x4aabef2324cedff5387f1f65n === 0x8a2c72024405ec138670800n");
assert.sameValue(
  0x4aabef2324cedff5387f1f65n & -0xbf2ed51ff75d380fd3be813ec6185780n, 0x8a2c72024405ec138670800n,
  "0x4aabef2324cedff5387f1f65n & -0xbf2ed51ff75d380fd3be813ec6185780n === 0x8a2c72024405ec138670800n");
assert.sameValue(
  -0xbf2ed51ff75d380fd3be813ec6185780n & -0x4aabef2324cedff5387f1f65n, -0xbf2ed51fffffff2ff7fedffffe7f5f80n,
  "-0xbf2ed51ff75d380fd3be813ec6185780n & -0x4aabef2324cedff5387f1f65n === -0xbf2ed51fffffff2ff7fedffffe7f5f80n");
assert.sameValue(
  -0x4aabef2324cedff5387f1f65n & -0xbf2ed51ff75d380fd3be813ec6185780n, -0xbf2ed51fffffff2ff7fedffffe7f5f80n,
  "-0x4aabef2324cedff5387f1f65n & -0xbf2ed51ff75d380fd3be813ec6185780n === -0xbf2ed51fffffff2ff7fedffffe7f5f80n");
assert.sameValue(-0xffffffffn & 0n, 0n, "-0xffffffffn & 0n === 0n");
assert.sameValue(0n & -0xffffffffn, 0n, "0n & -0xffffffffn === 0n");
assert.sameValue(
  -0xffffffffffffffffn & 0x10000000000000000n, 0x10000000000000000n,
  "-0xffffffffffffffffn & 0x10000000000000000n === 0x10000000000000000n");
assert.sameValue(
  0x10000000000000000n & -0xffffffffffffffffn, 0x10000000000000000n,
  "0x10000000000000000n & -0xffffffffffffffffn === 0x10000000000000000n");
assert.sameValue(
  -0xffffffffffffffffffffffffn & 0x10000000000000000n, 0n,
  "-0xffffffffffffffffffffffffn & 0x10000000000000000n === 0n");
assert.sameValue(
  0x10000000000000000n & -0xffffffffffffffffffffffffn, 0n,
  "0x10000000000000000n & -0xffffffffffffffffffffffffn === 0n");
