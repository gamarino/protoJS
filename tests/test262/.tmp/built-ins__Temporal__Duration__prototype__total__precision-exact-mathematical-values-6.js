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
  14. Else if unit is "hour", then
    a. Let divisor be 3.6 × 10^12.
    b. Set total to DivideNormalizedTimeDuration(norm, divisor).
    ...

  DivideNormalizedTimeDuration ( d, divisor )

  1. Assert: divisor ≠ 0.
  2. Return d.[[TotalNanoseconds]] / divisor.
features: [Temporal]
---*/

// Randomly generated test data.
const data = [
  {
    hours: 816,
    nanoseconds: 2049_187_497_660,
  },
  {
    hours: 7825,
    nanoseconds: 1865_665_040_770,
  },
  {
    hours: 0,
    nanoseconds: 1049_560_584_034,
  },
  {
    hours: 2055144,
    nanoseconds: 2502_078_444_371,
  },
  {
    hours: 31,
    nanoseconds: 1010_734_758_745,
  },
  {
    hours: 24,
    nanoseconds: 2958_999_560_387,
  },
  {
    hours: 0,
    nanoseconds: 342_058_521_588,
  },
  {
    hours: 17746,
    nanoseconds: 3009_093_506_309,
  },
  {
    hours: 4,
    nanoseconds: 892_480_914_569,
  },
  {
    hours: 3954,
    nanoseconds: 571_647_777_618,
  },
  {
    hours: 27,
    nanoseconds: 2322_199_502_640,
  },
  {
    hours: 258054064,
    nanoseconds: 2782_411_891_222,
  },
  {
    hours: 1485,
    nanoseconds: 2422_559_903_100,
  },
  {
    hours: 0,
    nanoseconds: 1461_068_214_153,
  },
  {
    hours: 393,
    nanoseconds: 1250_229_561_658,
  },
  {
    hours: 0,
    nanoseconds: 91_035_820,
  },
  {
    hours: 0,
    nanoseconds: 790_982_655,
  },
  {
    hours: 150,
    nanoseconds: 608_531_524,
  },
  {
    hours: 5469,
    nanoseconds: 889_204_952,
  },
  {
    hours: 7870,
    nanoseconds: 680_042_770,
  },
];

const nsPerHour = 3600_000_000_000;

const fractionDigits = Math.log10(nsPerHour) + Math.log10(100_000_000_000) - Math.log10(36);
assert.sameValue(fractionDigits, 22);

for (let {hours, nanoseconds} of data) {
  assert(nanoseconds < nsPerHour);

  // Compute enough fractional digits to approximate the exact result. Use BigInts
  // to avoid floating point precision loss. Fill to the left with implicit zeros.
  let fraction = ((BigInt(nanoseconds) * 100_000_000_000n) / 36n).toString().padStart(fractionDigits, "0");

  // Get the Number approximation from the string representation.
  let expected = Number(`${hours}.${fraction}`);

  let d = Temporal.Duration.from({hours, nanoseconds});
  let actual = d.total("hours");

  assert.sameValue(
    actual,
    expected,
    `hours=${hours}, nanoseconds=${nanoseconds}`,
  );
}
