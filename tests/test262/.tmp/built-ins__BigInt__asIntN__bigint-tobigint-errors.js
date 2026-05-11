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
description: BigInt.asIntN type coercion for bigint parameter
esid: sec-bigint.asintn
info: |
  BigInt.asIntN ( bits, bigint )

  2. Let bigint ? ToBigInt(bigint).
features: [BigInt, computed-property-names, Symbol, Symbol.toPrimitive]
---*/
assert.sameValue(typeof BigInt, 'function');
assert.sameValue(typeof BigInt.asIntN, 'function');

assert.throws(TypeError, function () {
  BigInt.asIntN();
}, "ToBigInt: no argument => undefined => TypeError");
assert.throws(TypeError, function () {
  BigInt.asIntN(0);
}, "ToBigInt: no argument => undefined => TypeError");

assert.throws(TypeError, function() {
  BigInt.asIntN(0, undefined);
}, "ToBigInt: undefined => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    [Symbol.toPrimitive]: function() {
      return undefined;
    }
  });
}, "ToBigInt: @@toPrimitive => undefined => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    valueOf: function() {
      return undefined;
    }
  });
}, "ToBigInt: valueOf => undefined => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    toString: function() {
      return undefined;
    }
  });
}, "ToBigInt: toString => undefined => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, null);
}, "ToBigInt: null => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    [Symbol.toPrimitive]: function() {
      return null;
    }
  });
}, "ToBigInt: @@toPrimitive => null => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    valueOf: function() {
      return null;
    }
  });
}, "ToBigInt: valueOf => null => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    toString: function() {
      return null;
    }
  });
}, "ToBigInt: toString => null => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, 0);
}, "ToBigInt: Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, Object(0));
}, "ToBigInt: unbox object with internal slot => Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    [Symbol.toPrimitive]: function() {
      return 0;
    }
  });
}, "ToBigInt: @@toPrimitive => Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    valueOf: function() {
      return 0;
    }
  });
}, "ToBigInt: valueOf => Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    toString: function() {
      return 0;
    }
  });
}, "ToBigInt: toString => Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, NaN);
}, "ToBigInt: Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, Infinity);
}, "ToBigInt: Number => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, Symbol("1"));
}, "ToBigInt: Symbol => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, Object(Symbol("1")));
}, "ToBigInt: unbox object with internal slot => Symbol => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    [Symbol.toPrimitive]: function() {
      return Symbol("1");
    }
  });
}, "ToBigInt: @@toPrimitive => Symbol => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    valueOf: function() {
      return Symbol("1");
    }
  });
}, "ToBigInt: valueOf => Symbol => TypeError");
assert.throws(TypeError, function() {
  BigInt.asIntN(0, {
    toString: function() {
      return Symbol("1");
    }
  });
}, "ToBigInt: toString => Symbol => TypeError");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, "a");
}, "ToBigInt: unparseable BigInt");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, "0b2");
}, "ToBigInt: unparseable BigInt binary");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, Object("0b2"));
}, "ToBigInt: unbox object with internal slot => unparseable BigInt binary");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, {
    [Symbol.toPrimitive]: function() {
      return "0b2";
    }
  });
}, "ToBigInt: @@toPrimitive => unparseable BigInt binary");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, {
    valueOf: function() {
      return "0b2";
    }
  });
}, "ToBigInt: valueOf => unparseable BigInt binary");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, {
    toString: function() {
      return "0b2";
    }
  });
}, "ToBigInt: toString => unparseable BigInt binary");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, "   0b2   ");
}, "ToBigInt: unparseable BigInt with leading/trailing whitespace");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, "0o8");
}, "ToBigInt: unparseable BigInt octal");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, "0xg");
}, "ToBigInt: unparseable BigInt hex");
assert.throws(SyntaxError, function() {
  BigInt.asIntN(0, "1n");
}, "ToBigInt: unparseable BigInt due to literal suffix");
