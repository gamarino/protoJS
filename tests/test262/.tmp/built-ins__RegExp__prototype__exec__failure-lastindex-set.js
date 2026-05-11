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


// Copyright (C) 2021 Richard Gibson. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: >
    lastIndex is set to 0 after exhausting the string when global and/or sticky are set.
esid: sec-regexpbuiltinexec
info: |
    RegExpBuiltinExec (
      _R_: an initialized RegExp instance,
      _S_: a String,
    )
    ...
    1. Let _length_ be the number of code units in _S_.
    2. Let _lastIndex_ be ℝ(? ToLength(? Get(_R_, *"lastIndex"*))).
    3. Let _flags_ be _R_.[[OriginalFlags]].
    4. If _flags_ contains *"g"*, let _global_ be *true*; else let _global_ be *false*.
    5. If _flags_ contains *"y"*, let _sticky_ be *true*; else let _sticky_ be *false*.
    ...
    9. Let _matchSucceeded_ be *false*.
    10. Repeat, while _matchSucceeded_ is *false*,
      a. If _lastIndex_ &gt; _length_, then
        i. If _global_ is *true* or _sticky_ is *true*, then
          1. Perform ? Set(_R_, *"lastIndex"*, *+0*<sub>𝔽</sub>, *true*).
        ii. Return *null*.
features: [exponentiation]
---*/

var R_g = /./g, R_y = /./y, R_gy = /./gy;

var S = "test";

var lastIndex;
var bigLastIndexes = [
  Infinity,
  Number.MAX_VALUE,
  Number.MAX_SAFE_INTEGER,
  Number.MAX_SAFE_INTEGER - 1,
  2**32 + 4,
  2**32 + 3,
  2**32 + 2,
  2**32 + 1,
  2**32,
  2**32 - 1,
  5
];
for ( var i = 0; i < bigLastIndexes.length; i++ ) {
  lastIndex = bigLastIndexes[i];
  R_g.lastIndex = lastIndex;
  R_y.lastIndex = lastIndex;
  R_gy.lastIndex = lastIndex;

  assert.sameValue(R_g.exec(S), null,
      "global RegExp instance must fail to match against '" + S +
      "' at lastIndex " + lastIndex);
  assert.sameValue(R_y.exec(S), null,
      "sticky RegExp instance must fail to match against '" + S +
      "' at lastIndex " + lastIndex);
  assert.sameValue(R_gy.exec(S), null,
      "global sticky RegExp instance must fail to match against '" + S +
      "' at lastIndex " + lastIndex);

  assert.sameValue(R_g.lastIndex, 0,
      "global RegExp instance lastIndex must be reset after " + lastIndex +
      " exceeds string length");
  assert.sameValue(R_y.lastIndex, 0,
      "sticky RegExp instance lastIndex must be reset after " + lastIndex +
      " exceeds string length");
  assert.sameValue(R_gy.lastIndex, 0,
      "global sticky RegExp instance lastIndex must be reset after " + lastIndex +
      " exceeds string length");
}
