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


// Copyright (C) 2022 Mathias Bynens, Ron Buckton, and the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-get-regexp.prototype.unicodesets
description: >
    `unicodeSets` accessor function invoked on a RegExp instance
info: |
    get RegExp.prototype.unicodeSets -> RegExpHasFlag

    4. Let flags be the value of R’s [[OriginalFlags]] internal slot.
    5. If flags contains the code unit "s", return true.
    6. Return false.
features: [regexp-v-flag]
---*/

assert.sameValue(/./.unicodeSets, false, "/./.unicodeSets");
assert.sameValue(/./d.unicodeSets, false, "/./d.unicodeSets");
assert.sameValue(/./g.unicodeSets, false, "/./g.unicodeSets");
assert.sameValue(/./i.unicodeSets, false, "/./i.unicodeSets");
assert.sameValue(/./m.unicodeSets, false, "/./m.unicodeSets");
assert.sameValue(/./s.unicodeSets, false, "/./s.unicodeSets");
assert.sameValue(/./u.unicodeSets, false, "/./u.unicodeSets");
assert.sameValue(/./y.unicodeSets, false, "/./y.unicodeSets");

assert.sameValue(/./v.unicodeSets, true, "/./v.unicodeSets");
assert.sameValue(/./vd.unicodeSets, true, "/./vd.unicodeSets");
assert.sameValue(/./vg.unicodeSets, true, "/./vg.unicodeSets");
assert.sameValue(/./vi.unicodeSets, true, "/./vi.unicodeSets");
assert.sameValue(/./vm.unicodeSets, true, "/./vm.unicodeSets");
assert.sameValue(/./vs.unicodeSets, true, "/./vs.unicodeSets");
// Note: `/vu` throws an early parse error and is tested separately.
assert.sameValue(/./vy.unicodeSets, true, "/./vy.unicodeSets");

assert.sameValue(new RegExp(".", "").unicodeSets, false, "new RegExp('.', '').unicodeSets");
assert.sameValue(new RegExp(".", "d").unicodeSets, false, "new RegExp('.', 'd').unicodeSets");
assert.sameValue(new RegExp(".", "g").unicodeSets, false, "new RegExp('.', 'g').unicodeSets");
assert.sameValue(new RegExp(".", "i").unicodeSets, false, "new RegExp('.', 'i').unicodeSets");
assert.sameValue(new RegExp(".", "m").unicodeSets, false, "new RegExp('.', 'm').unicodeSets");
assert.sameValue(new RegExp(".", "s").unicodeSets, false, "new RegExp('.', 's').unicodeSets");
assert.sameValue(new RegExp(".", "u").unicodeSets, false, "new RegExp('.', 'u').unicodeSets");
assert.sameValue(new RegExp(".", "y").unicodeSets, false, "new RegExp('.', 'y').unicodeSets");

assert.sameValue(new RegExp(".", "v").unicodeSets, true, "new RegExp('.', 'v').unicodeSets");
assert.sameValue(new RegExp(".", "vd").unicodeSets, true, "new RegExp('.', 'vd').unicodeSets");
assert.sameValue(new RegExp(".", "vg").unicodeSets, true, "new RegExp('.', 'vg').unicodeSets");
assert.sameValue(new RegExp(".", "vi").unicodeSets, true, "new RegExp('.', 'vi').unicodeSets");
assert.sameValue(new RegExp(".", "vm").unicodeSets, true, "new RegExp('.', 'vm').unicodeSets");
assert.sameValue(new RegExp(".", "vs").unicodeSets, true, "new RegExp('.', 'vs').unicodeSets");
// Note: `new RegExp(pattern, 'vu')` throws a runtime error and is tested separately.
assert.sameValue(new RegExp(".", "vy").unicodeSets, true, "new RegExp('.', 'vy').unicodeSets");
