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


// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    Compute the mathematical integer value
    that is represented by Z in radix-R notation, using the
    letters A-Z and a-z for digits with values 10 through 35.
    Compute the number value for Result(16)
esid: sec-parseint-string-radix
description: Checking algorithm for R = 16
---*/

assert.sameValue(parseInt("0x1", 16), 1, 'parseInt("0x1", 16) must return 1');
assert.sameValue(parseInt("0X10", 16), 16, 'parseInt("0X10", 16) must return 16');
assert.sameValue(parseInt("0x100", 16), 256, 'parseInt("0x100", 16) must return 256');
assert.sameValue(parseInt("0X1000", 16), 4096, 'parseInt("0X1000", 16) must return 4096');
assert.sameValue(parseInt("0x10000", 16), 65536, 'parseInt("0x10000", 16) must return 65536');
assert.sameValue(parseInt("0X100000", 16), 1048576, 'parseInt("0X100000", 16) must return 1048576');
assert.sameValue(parseInt("0x1000000", 16), 16777216, 'parseInt("0x1000000", 16) must return 16777216');
assert.sameValue(parseInt("0x10000000", 16), 268435456, 'parseInt("0x10000000", 16) must return 268435456');
assert.sameValue(parseInt("0x100000000", 16), 4294967296, 'parseInt("0x100000000", 16) must return 4294967296');
assert.sameValue(parseInt("0x1000000000", 16), 68719476736, 'parseInt("0x1000000000", 16) must return 68719476736');
assert.sameValue(parseInt("0x10000000000", 16), 1099511627776, 'parseInt("0x10000000000", 16) must return 1099511627776');

assert.sameValue(
  parseInt("0x100000000000", 16),
  17592186044416,
  'parseInt("0x100000000000", 16) must return 17592186044416'
);

assert.sameValue(
  parseInt("0x1000000000000", 16),
  281474976710656,
  'parseInt("0x1000000000000", 16) must return 281474976710656'
);

assert.sameValue(
  parseInt("0x10000000000000", 16),
  4503599627370496,
  'parseInt("0x10000000000000", 16) must return 4503599627370496'
);

assert.sameValue(
  parseInt("0x100000000000000", 16),
  72057594037927936,
  'parseInt("0x100000000000000", 16) must return 72057594037927936'
);

assert.sameValue(
  parseInt("0x1000000000000000", 16),
  1152921504606846976,
  'parseInt("0x1000000000000000", 16) must return 1152921504606846976'
);

assert.sameValue(
  parseInt("0x10000000000000000", 16),
  18446744073709551616,
  'parseInt("0x10000000000000000", 16) must return 18446744073709551616'
);

assert.sameValue(
  parseInt("0x100000000000000000", 16),
  295147905179352825856,
  'parseInt("0x100000000000000000", 16) must return 295147905179352825856'
);

assert.sameValue(
  parseInt("0x1000000000000000000", 16),
  4722366482869645213696,
  'parseInt("0x1000000000000000000", 16) must return 4722366482869645213696'
);

assert.sameValue(
  parseInt("0x10000000000000000000", 16),
  75557863725914323419136,
  'parseInt("0x10000000000000000000", 16) must return 75557863725914323419136'
);
