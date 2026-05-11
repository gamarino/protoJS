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


// Copyright (C) 2017 Mozilla Corporation.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-atomics.islockfree
description: >
  Atomics.isLockFree( size )
    Let n be ? ToInteger(size).
    Let AR be the Agent Record of the surrounding agent.
    If n equals 1, return AR.[[IsLockFree1]].
    If n equals 2, return AR.[[IsLockFree2]].
    If n equals 4, return true.
    If n equals 8, return AR.[[IsLockFree8]].
    Return false.
features: [Atomics, Array.prototype.includes]
---*/

// These are the only counts that we care about tracking.
var isLockFree1;
var isLockFree2;
var isLockFree8;

{
  isLockFree1 = Atomics.isLockFree(1);
  //
  // If n equals 1, return AR.[[IsLockFree1]].
  //
  assert.sameValue(typeof isLockFree1, 'boolean', 'The value of `typeof isLockFree1` is "boolean"');
  // Once the values of [[Signifier]], [[IsLockFree1]], [[IsLockFree2]],
  // and [[IsLockFree8]] have been observed by any agent in the agent
  // cluster they cannot change.
  assert.sameValue(
    Atomics.isLockFree(1),
    isLockFree1,
    'Atomics.isLockFree(1) returns the value of `isLockFree1` (Atomics.isLockFree(1))'
  );
};
{
  isLockFree2 = Atomics.isLockFree(2);
  //
  // If n equals 2, return AR.[[IsLockFree2]].
  //
  assert.sameValue(typeof isLockFree2, 'boolean', 'The value of `typeof isLockFree2` is "boolean"');
  // Once the values of [[Signifier]], [[IsLockFree1]], [[IsLockFree2]],
  // and [[IsLockFree8]] have been observed by any agent in the agent
  // cluster they cannot change.
  assert.sameValue(
    Atomics.isLockFree(2),
    isLockFree2,
    'Atomics.isLockFree(2) returns the value of `isLockFree2` (Atomics.isLockFree(2))'
  );
};
{
  let isLockFree4 = Atomics.isLockFree(4);
  //
  // If n equals 4, return true.
  //
  assert.sameValue(typeof isLockFree4, 'boolean', 'The value of `typeof isLockFree4` is "boolean"');
  assert.sameValue(isLockFree4, true, 'The value of `isLockFree4` is true');
};

{
  isLockFree8 = Atomics.isLockFree(8);
  //
  // If n equals 8, return AR.[[IsLockFree8]].
  //
  assert.sameValue(typeof isLockFree8, 'boolean', 'The value of `typeof isLockFree8` is "boolean"');
  // Once the values of [[Signifier]], [[IsLockFree1]], [[IsLockFree2]],
  // and [[IsLockFree8]] have been observed by any agent in the agent
  // cluster they cannot change.
  assert.sameValue(
    Atomics.isLockFree(8),
    isLockFree8,
    'Atomics.isLockFree(8) returns the value of `isLockFree8` (Atomics.isLockFree(8))'
  );
};

{
  for (let i = 0; i < 12; i++) {
    if (![1, 2, 4, 8].includes(i)) {
      assert.sameValue(Atomics.isLockFree(i), false, 'Atomics.isLockFree(i) returns false');
    }
  }
};

assert.sameValue(
  Atomics.isLockFree(1),
  isLockFree1,
  'Atomics.isLockFree(1) returns the value of `isLockFree1` (Atomics.isLockFree(1))'
);
assert.sameValue(
  Atomics.isLockFree(2),
  isLockFree2,
  'Atomics.isLockFree(2) returns the value of `isLockFree2` (Atomics.isLockFree(2))'
);
assert.sameValue(
  Atomics.isLockFree(8),
  isLockFree8,
  'Atomics.isLockFree(8) returns the value of `isLockFree8` (Atomics.isLockFree(8))'
);

// Duplicates behavior created by loop from above
assert.sameValue(Atomics.isLockFree(3), false, 'Atomics.isLockFree(3) returns false');
assert.sameValue(Atomics.isLockFree(4), true, 'Atomics.isLockFree(4) returns true');
assert.sameValue(Atomics.isLockFree(5), false, 'Atomics.isLockFree(5) returns false');
assert.sameValue(Atomics.isLockFree(6), false, 'Atomics.isLockFree(6) returns false');
assert.sameValue(Atomics.isLockFree(7), false, 'Atomics.isLockFree(7) returns false');
assert.sameValue(Atomics.isLockFree(9), false, 'Atomics.isLockFree(9) returns false');
assert.sameValue(Atomics.isLockFree(10), false, 'Atomics.isLockFree(10) returns false');
assert.sameValue(Atomics.isLockFree(11), false, 'Atomics.isLockFree(11) returns false');
assert.sameValue(Atomics.isLockFree(12), false, 'Atomics.isLockFree(12) returns false');
