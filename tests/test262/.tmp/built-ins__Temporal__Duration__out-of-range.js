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
esid: sec-temporal.duration
description: Various arguments to the Duration constructor that are out of range
features: [Temporal]
---*/

// 2^32 = 4294967296
assert.throws(RangeError, () => new Temporal.Duration(4294967296), "years > max");
assert.throws(RangeError, () => new Temporal.Duration(-4294967296), "years < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 4294967296), "months > max");
assert.throws(RangeError, () => new Temporal.Duration(0, -4294967296), "months < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 4294967296), "weeks > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, -4294967296), "weeks < min");

// ceil(max safe integer / 86400) = 104249991375
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 104249991375), "days > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 104249991374, 24), "hours balance into days > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, -104249991375), "days < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, -104249991374, -24), "hours balance into days < min");

// ceil(max safe integer / 3600) = 2501999792984
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 2501999792984), "hours > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 2501999792983, 60), "minutes balance into hours > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, -2501999792984), "hours < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, -2501999792983, -60), "minutes balance into hours < min");

// ceil(max safe integer / 60) = 150119987579017
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 150119987579017), "minutes > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 150119987579016, 60), "seconds balance into minutes > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, -150119987579017), "minutes < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, -150119987579016, -60), "seconds balance into minutes < min");

// 2^53 = 9007199254740992 = max safe integer + 1
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 9007199254740992), "seconds > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 9007199254740991, 1000), "ms balance into seconds > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 9007199254740991, 999, 1000), "µs balance into seconds > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 9007199254740991, 999, 999, 1000), "ns balance into seconds > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -9007199254740992), "seconds < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -9007199254740991, -1000), "ms balance into seconds < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -9007199254740991, -999, -1000), "µs balance into seconds < min");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -9007199254740991, -999, -999, -1000), "ns balance into seconds < min");

// max safe integer - floor(max safe integer / 1000) +1 = 8998192055486252
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 8998192055486252, 9007199254740991 , 0, 0), "max ms balance into s > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -8998192055486252, -9007199254740991, 0, 0), "min ms balance into s < min");

// max safe integer - floor(max safe integer / 1000000) +1 = 9007190247541738
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 9007190247541738, 0, 9007199254740991, 0), "max µs balance into s > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -9007190247541738, 0, -9007199254740991, 0), "min µs balance into s < min");

// max safe integer - floor(max safe integer / 1000000000) +1 = 9007199245733793
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, 9007199245733793, 0, 0, 9007199254740991), "max ns balance into s > max");
assert.throws(RangeError, () => new Temporal.Duration(0, 0, 0, 0, 0, 0, -9007199245733793, 0, 0, -9007199254740991), "min ns balance into s < min");
