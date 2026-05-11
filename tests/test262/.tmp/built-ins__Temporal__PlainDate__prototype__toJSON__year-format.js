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


// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindate.prototype.tojson
description: Verify that the year is appropriately formatted as 4 or 6 digits
features: [Temporal]
---*/

let instance = new Temporal.PlainDate(-100000, 12, 3);
assert.sameValue(instance.toJSON(), "-100000-12-03", "large negative year formatted as 6-digit");

instance = new Temporal.PlainDate(-10000, 4, 5);
assert.sameValue(instance.toJSON(), "-010000-04-05", "smallest 5-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDate(-9999, 6, 7);
assert.sameValue(instance.toJSON(), "-009999-06-07", "largest 4-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDate(-1000, 8, 9);
assert.sameValue(instance.toJSON(), "-001000-08-09", "smallest 4-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDate(-999, 10, 9);
assert.sameValue(instance.toJSON(), "-000999-10-09", "largest 3-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDate(-1, 8, 7);
assert.sameValue(instance.toJSON(), "-000001-08-07", "year -1 formatted as 6-digit");

instance = new Temporal.PlainDate(0, 6, 5);
assert.sameValue(instance.toJSON(), "0000-06-05", "year 0 formatted as 4-digit");

instance = new Temporal.PlainDate(1, 4, 3);
assert.sameValue(instance.toJSON(), "0001-04-03", "year 1 formatted as 4-digit");

instance = new Temporal.PlainDate(999, 2, 10);
assert.sameValue(instance.toJSON(), "0999-02-10", "largest 3-digit positive year formatted as 4-digit");

instance = new Temporal.PlainDate(1000, 1, 23);
assert.sameValue(instance.toJSON(), "1000-01-23", "smallest 4-digit positive year formatted as 4-digit");

instance = new Temporal.PlainDate(9999, 4, 5);
assert.sameValue(instance.toJSON(), "9999-04-05", "largest 4-digit positive year formatted as 4-digit");

instance = new Temporal.PlainDate(10000, 6, 7);
assert.sameValue(instance.toJSON(), "+010000-06-07", "smallest 5-digit positive year formatted as 6-digit");

instance = new Temporal.PlainDate(100000, 8, 9);
assert.sameValue(instance.toJSON(), "+100000-08-09", "large positive year formatted as 6-digit");
