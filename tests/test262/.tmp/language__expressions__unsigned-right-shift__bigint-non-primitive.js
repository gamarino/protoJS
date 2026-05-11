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
description: Unsigned right shift always throws for non-primitive BigInt values
esid: sec-unsigned-right-shift-operator-runtime-semantics-evaluation
info: |
  ShiftExpression : ShiftExpression >>> AdditiveExpression

  1. Let lref be the result of evaluating ShiftExpression.
  2. Let lval be ? GetValue(lref).
  3. Let rref be the result of evaluating AdditiveExpression.
  4. Let rval be ? GetValue(rref).
  5. Let lnum be ? ToNumeric(lval).
  6. Let rnum be ? ToNumeric(rval).
  7. If Type(lnum) does not equal Type(rnum), throw a TypeError exception.
  8. Let T be Type(lnum).
  9. Return T::unsignedRightShift(lnum, rnum).

  Note: BigInt::unsignedRightShift always throws a TypeError

features: [BigInt, Symbol.toPrimitive]
---*/
assert.throws(TypeError, function() {
  Object(0b101n) >>> 1n;
}, 'Object(0b101n) >>> 1n throws TypeError');

assert.throws(TypeError, function() {
  Object(0b101n) >>> Object(1n);
}, 'Object(0b101n) >>> Object(1n) throws TypeError');

function err() {
  throw new Test262Error();
}

assert.throws(TypeError, function() {
  ({
    [Symbol.toPrimitive]: function() {
      return 0b101n;
    },

    valueOf: err,
    toString: err
  }) >>> 1n;
}, '({[Symbol.toPrimitive]: function() {return 0b101n;}, valueOf: err, toString: err}) >>> 1n throws TypeError');

assert.throws(TypeError, function() {
  ({
    valueOf: function() {
      return 0b101n;
    },

    toString: err
  }) >>> 1n;
}, '({valueOf: function() {return 0b101n;}, toString: err}) >>> 1n throws TypeError');

assert.throws(TypeError, function() {
  ({
    toString: function() {
      return 0b101n;
    }
  }) >>> 1n;
}, '({toString: function() {return 0b101n;}}) >>> 1n throws TypeError');

assert.throws(TypeError, function() {
  0b101n >>> {
    [Symbol.toPrimitive]: function() {
      return 1n;
    },

    valueOf: err,
    toString: err
  };
}, '0b101n >>> {[Symbol.toPrimitive]: function() {return 1n;}, valueOf: err, toString: err} throws TypeError');

assert.throws(TypeError, function() {
  0b101n >>> {
    valueOf: function() {
      return 1n;
    },

    toString: err
  };
}, '0b101n >>> {valueOf: function() {return 1n;}, toString: err} throws TypeError');

assert.throws(TypeError, function() {
  0b101n >>> {
    toString: function() {
      return 1n;
    }
  };
}, '0b101n >>> {toString: function() {return 1n;}} throws TypeError');

assert.throws(TypeError, function() {
  ({
    valueOf: function() {
      return 0b101n;
    }
  }) >>> {
    valueOf: function() {
      return 1n;
    }
  };
}, '({valueOf: function() {return 0b101n;}}) >>> {valueOf: function() {return 1n;}} throws TypeError');
