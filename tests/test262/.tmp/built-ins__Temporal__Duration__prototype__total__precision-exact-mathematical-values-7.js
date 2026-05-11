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


// Copyright (C) 2024 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.total
description: >
  DivideNormalizedTimeDuration computes on exact mathematical values.
info: |
  Temporal.Duration.prototype.total ( totalOf )

  ...
  20. Let roundRecord be ? RoundDuration(unbalanceResult.[[Years]],
      unbalanceResult.[[Months]], unbalanceResult.[[Weeks]], days, norm, 1,
      unit, "trunc", plainRelativeTo, calendarRec, zonedRelativeTo, timeZoneRec,
      precalculatedPlainDateTime).
  21. Return 𝔽(roundRecord.[[Total]]).

  RoundDuration ( ... )

  ...
  16. Else if unit is "second", then
    a. Let divisor be 10^9.
    b. Set total to DivideNormalizedTimeDuration(norm, divisor).
    ...
  17. Else if unit is "millisecond", then
    a. Let divisor be 10^6.
    b. Set total to DivideNormalizedTimeDuration(norm, divisor).
    ...
  18. Else if unit is "microsecond", then
    a. Let divisor be 10^3.
    b. Set total to DivideNormalizedTimeDuration(norm, divisor).
    ...

  DivideNormalizedTimeDuration ( d, divisor )

  1. Assert: divisor ≠ 0.
  2. Return d.[[TotalNanoseconds]] / divisor.
features: [Temporal]
---*/

// Test duration units where the fractional part is a power of ten.
const units = [
  "seconds", "milliseconds", "microseconds", "nanoseconds",
];

// Conversion factors to nanoseconds precision.
const toNanos = {
  "seconds": 1_000_000_000n,
  "milliseconds": 1_000_000n,
  "microseconds": 1_000n,
  "nanoseconds": 1n,
};

const integers = [
  // Small integers.
  0,
  1,
  2,

  // Large integers around Number.MAX_SAFE_INTEGER.
  2**51,
  2**52,
  2**53,
  2**54,
];

const fractions = [
  // True fractions.
  0, 1, 10, 100, 125, 200, 250, 500, 750, 800, 900, 950, 999,

  // Fractions with overflow.
  1_000,
  1_999,
  2_000,
  2_999,
  3_000,
  3_999,
  4_000,
  4_999,

  999_999,
  1_000_000,
  1_000_001,

  999_999_999,
  1_000_000_000,
  1_000_000_001,
];

const maxTimeDuration = (2n ** 53n) * (10n ** 9n) - 1n;

// Iterate over all units except the last one.
for (let unit of units.slice(0, -1)) {
  let smallerUnit = units[units.indexOf(unit) + 1];

  for (let integer of integers) {
    for (let fraction of fractions) {
      // Total nanoseconds must not exceed |maxTimeDuration|.
      let totalNanoseconds = BigInt(integer) * toNanos[unit] + BigInt(fraction) * toNanos[smallerUnit];
      if (totalNanoseconds > maxTimeDuration) {
        continue;
      }

      // Get the Number approximation from the string representation.
      let i = BigInt(integer) + BigInt(fraction) / 1000n;
      let f = String(fraction % 1000).padStart(3, "0");
      let expected = Number(`${i}.${f}`);

      let d = Temporal.Duration.from({[unit]: integer, [smallerUnit]: fraction});
      let actual = d.total(unit);

      assert.sameValue(
        actual,
        expected,
        `${unit}=${integer}, ${smallerUnit}=${fraction}`,
      );
    }
  }
}
