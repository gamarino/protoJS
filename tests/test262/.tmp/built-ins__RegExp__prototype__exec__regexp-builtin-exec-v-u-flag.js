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


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Deprecated now that compareArray is defined in assert.js.
defines: [compareArray]
---*/


// Copyright (C) 2024 Tan Meng. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-regexpbuiltinexec
description: RegExpBuiltinExec behavior with 'u' and 'v'flags
features: [regexp-v-flag, regexp-unicode-property-escapes]
includes: [compareArray.js]
---*/

const text = '𠮷a𠮷b𠮷';

function doExec(regex) {
  const result = regex.exec(text);
  return result ? [result[0], result.index] : null;
}

assert.compareArray(doExec(/𠮷/), ["𠮷", 0], "Basic exec without v flag");

assert.compareArray(doExec(/𠮷/u), ["𠮷", 0], "Exec with u flag");
assert.compareArray(doExec(/\p{Script=Han}/u), ["𠮷", 0], "Unicode property escapes with u flag");
assert.compareArray(doExec(/./u), ["𠮷", 0], "Dot with u flag");

assert.compareArray(doExec(/𠮷/v), ["𠮷", 0], "Exec with v flag");
assert.compareArray(doExec(/\p{Script=Han}/v), ["𠮷", 0], "Unicode property escapes with v flag");
assert.compareArray(doExec(/./v), ["𠮷", 0], "Dot with v flag");

assert.compareArray(doExec(/\p{ASCII}/u), ["a", 2], "ASCII with u flag");
assert.compareArray(doExec(/\p{ASCII}/v), ["a", 2], "ASCII with v flag");

assert.sameValue(doExec(/x/u), null, "Non-matching regex with u flag");
assert.sameValue(doExec(/x/v), null, "Non-matching regex with v flag");

const regexWithGroupsU = /(\p{Script=Han})(.)/u;
const resultWithGroupsU = regexWithGroupsU.exec(text);
assert.sameValue(resultWithGroupsU[1], "𠮷", "Capture group 1 with u flag");
assert.sameValue(resultWithGroupsU[2], "a", "Capture group 2 with u flag");
assert.sameValue(resultWithGroupsU.index, 0, "Match index for groups with u flag");

const regexWithGroupsV = /(\p{Script=Han})(.)/v;
const resultWithGroupsV = regexWithGroupsV.exec(text);
assert.sameValue(resultWithGroupsV[1], "𠮷", "Capture group 1 with v flag");
assert.sameValue(resultWithGroupsV[2], "a", "Capture group 2 with v flag");
assert.sameValue(resultWithGroupsV.index, 0, "Match index for groups with v flag");

const complexText = 'a\u{20BB7}b\u{10FFFF}c';
assert.compareArray(/\P{ASCII}/u.exec(complexText), ["\u{20BB7}"], "Non-ASCII with u flag");
assert.compareArray(/\P{ASCII}/v.exec(complexText), ["\u{20BB7}"], "Non-ASCII with v flag");
