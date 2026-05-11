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


// Copyright (C) 2026 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.tojson
description: Various maximum value combinations do not go out of range
features: [Temporal]
---*/

const maxYMW = Math.pow(2, 32) - 1; // Maximum years, months, weeks
const maxDays = Math.floor(Number.MAX_SAFE_INTEGER / 86400);
const maxHours = Math.floor(Number.MAX_SAFE_INTEGER / 3600);
const maxMinutes = Math.floor(Number.MAX_SAFE_INTEGER / 60);
const maxSecs = Number.MAX_SAFE_INTEGER; // = 9007199254740991, also max ms, μs, ns

assert.sameValue(new Temporal.Duration(maxYMW).toJSON(),
  'P' + maxYMW + 'Y', "maximum years");
assert.sameValue(new Temporal.Duration(-maxYMW).toJSON(),
  '-P' + maxYMW + 'Y', "minimum years");
assert.sameValue(new Temporal.Duration(0, maxYMW).toJSON(),
  'P' + maxYMW + 'M', "maximum months");
assert.sameValue(new Temporal.Duration(0, -maxYMW).toJSON(),
  '-P' + maxYMW + 'M', "minimum months");
assert.sameValue(new Temporal.Duration(0, 0, maxYMW).toJSON(),
  'P' + maxYMW + 'W', "maximum weeks");
assert.sameValue(new Temporal.Duration(0, 0, -maxYMW).toJSON(),
  '-P' + maxYMW + 'W', "minimum weeks");
assert.sameValue(new Temporal.Duration(0, 0, 0, maxDays).toJSON(),
  'P' + maxDays + 'D', "maximum days");
assert.sameValue(new Temporal.Duration(0, 0, 0, -maxDays).toJSON(),
  '-P' + maxDays + 'D', "minimum days");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, maxHours).toJSON(),
  'PT' + maxHours + 'H', "maximum hours");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, -maxHours).toJSON(),
  '-PT' + maxHours + 'H', "minimum hours");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, maxMinutes).toJSON(),
   'PT' + maxMinutes + 'M', "maximum minutes");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, -maxMinutes).toJSON(),
   '-PT' + maxMinutes + 'M', "minimum minutes");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, maxSecs).toJSON(),
   'PT' + maxSecs + 'S', "maximum seconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, -maxSecs).toJSON(),
   '-PT' + maxSecs + 'S', "minimum seconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, maxSecs).toJSON(),
   'PT' + Math.floor(maxSecs / 1000) + '.' + maxSecs % 1000 + 'S', "maximum milliseconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -maxSecs).toJSON(),
   '-PT' + Math.floor(maxSecs / 1000) + '.' + maxSecs % 1000 + 'S', "minimum milliseconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0,  maxSecs).toJSON(),
   'PT' + Math.floor(maxSecs / 1000000) + '.' + maxSecs % 1000000 + 'S', "maximum microseconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -maxSecs).toJSON(),
   '-PT' + Math.floor(maxSecs / 1000000) + '.' + maxSecs % 1000000 + 'S', "minimum microseconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, maxSecs).toJSON(),
   'PT' + Math.floor(maxSecs / 1000000000) + '.' + maxSecs % 1000000000 + 'S', "maximum nanoseconds");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -maxSecs).toJSON(),
   '-PT' + Math.floor(maxSecs / 1000000000) + '.' + maxSecs % 1000000000 + 'S', "minimum nanoseconds");

// Combinations with maximum values without balancing.
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, maxSecs, 999).toJSON(),
  'PT' + maxSecs + '.999S', "max value ms and s does not go out of range");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, -maxSecs, -999).toJSON(),
  '-PT' + maxSecs + '.999S', "min value ms and s does not go out of range");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, maxSecs, 999, 999).toJSON(),
  'PT' + maxSecs + '.999999S', "max value ms, μs and s does not go out of range");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, -maxSecs, -999, -999).toJSON(),
  '-PT' + maxSecs + '.999999S', "min value ms, μs and s does not go out of range");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, maxSecs, 999, 999, 999).toJSON(),
  'PT' + maxSecs + '.999999999S', "max value ms, μs, ns and s does not go out of range");
assert.sameValue(new Temporal.Duration(0, 0, 0, 0, 0, 0, -maxSecs, -999, -999, -999).toJSON(),
  '-PT' + maxSecs + '.999999999S', "min value ms, μs, ns and s does not go out of range");

// Combinations with maximum values with balancing.
const balanceMaxSecondsMilliseconds = new Temporal.Duration(
  0, 0, 0, 0, 0, 0, maxSecs - Math.floor(maxSecs / 1000), maxSecs, 0, 0);
assert.sameValue(balanceMaxSecondsMilliseconds.toJSON(),
  'PT' + maxSecs + '.' + maxSecs % 1000 + 'S', "balancing max ms to max s");
const balanceMinSecondsMilliseconds = new Temporal.Duration(
  0, 0, 0, 0, 0, 0, -(maxSecs - Math.floor(maxSecs / 1000)), -maxSecs, 0, 0);
assert.sameValue(balanceMinSecondsMilliseconds.toJSON(),
  '-PT' + maxSecs + '.' + maxSecs % 1000 + 'S', "balancing min ms to max s");
const balanceMaxSecondsMicroseconds = new Temporal.Duration(
  0, 0, 0, 0, 0, 0, maxSecs - Math.floor(maxSecs / 1000000), 0, maxSecs, 0);
assert.sameValue(balanceMaxSecondsMicroseconds.toJSON(),
  'PT' + maxSecs + '.' + maxSecs % 1000000 + 'S', "balancing max μs to max s");
const balanceMinSecondsMicroseconds = new Temporal.Duration(
  0, 0, 0, 0, 0, 0, -(maxSecs - Math.floor(maxSecs / 1000000)), 0, -maxSecs, 0);
assert.sameValue(balanceMinSecondsMicroseconds.toJSON(),
  '-PT' + maxSecs + '.' + maxSecs % 1000000 + 'S', "balancing min μs to min s");
const balanceMaxSecondsNanoseconds = new Temporal.Duration(
  0, 0, 0, 0, 0, 0, maxSecs - Math.floor(maxSecs / 1000000000), 0, 0, maxSecs);
assert.sameValue(balanceMaxSecondsNanoseconds.toJSON(),
  'PT' + maxSecs + '.' + maxSecs % 1000000000 + 'S', "balancing max ns to max s");
const balanceMinSecondsNanoseconds = new Temporal.Duration(
  0, 0, 0, 0, 0, 0, -(maxSecs - Math.floor(maxSecs / 1000000000)), 0, 0, -maxSecs);
assert.sameValue(balanceMinSecondsNanoseconds.toJSON(),
  '-PT' + maxSecs + '.' + maxSecs % 1000000000 + 'S', "balancing min ns to min s");
