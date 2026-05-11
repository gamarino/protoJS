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


// Copyright (C) 2022 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.instant.prototype.subtract
description: >
  Instant is minimum/maximum instant.
features: [Temporal]
---*/

let min = new Temporal.Instant(-86_40000_00000_00000_00000n);
let max = new Temporal.Instant(86_40000_00000_00000_00000n);

let zero = Temporal.Duration.from({nanoseconds: 0});
let one = Temporal.Duration.from({nanoseconds: 1});
let minusOne = Temporal.Duration.from({nanoseconds: -1});

// Adding zero to the minimum instant.
assert.sameValue(min.subtract(zero).epochNanoseconds, min.epochNanoseconds);

// Adding zero to the maximum instant.
assert.sameValue(max.subtract(zero).epochNanoseconds, max.epochNanoseconds);

// Subtracting one from the minimum instant.
assert.throws(RangeError, () => min.subtract(one));

// Adding one to the maximum instant.
assert.throws(RangeError, () => max.subtract(minusOne));

// Adding one to the minimum instant.
assert.sameValue(min.subtract(minusOne).epochNanoseconds, min.epochNanoseconds + 1n);

// Subtracting one from the maximum instant.
assert.sameValue(max.subtract(one).epochNanoseconds, max.epochNanoseconds - 1n);

// From minimum to maximum instant.
assert.sameValue(min.subtract({nanoseconds: -86_40000_00000_00000_00000 * 2}).epochNanoseconds, max.epochNanoseconds);

assert.sameValue(min.subtract({microseconds: -8640_00000_00000_00000 * 2}).epochNanoseconds, max.epochNanoseconds);

assert.sameValue(min.subtract({milliseconds: -8_64000_00000_00000 * 2}).epochNanoseconds, max.epochNanoseconds);

assert.sameValue(min.subtract({seconds: -864_00000_00000 * 2}).epochNanoseconds, max.epochNanoseconds);

// From maximum to minimum instant.
assert.sameValue(max.subtract({nanoseconds: 86_40000_00000_00000_00000 * 2}).epochNanoseconds, min.epochNanoseconds);

assert.sameValue(max.subtract({microseconds: 8640_00000_00000_00000 * 2}).epochNanoseconds, min.epochNanoseconds);

assert.sameValue(max.subtract({milliseconds: 8_64000_00000_00000 * 2}).epochNanoseconds, min.epochNanoseconds);

assert.sameValue(max.subtract({seconds: 864_00000_00000 * 2}).epochNanoseconds, min.epochNanoseconds);
