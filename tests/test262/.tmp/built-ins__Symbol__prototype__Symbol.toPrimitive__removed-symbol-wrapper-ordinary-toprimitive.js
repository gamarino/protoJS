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


// Copyright (C) 2021 Alexey Shvayka. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-symbol.prototype-@@toprimitive
description: >
    If deleted, Symbol wrapper objects is converted to primitive via OrdinaryToPrimitive.
info: |
    ToPrimitive ( input [ , preferredType ] )

    [...]
    2. If Type(input) is Object, then
        a. Let exoticToPrim be ? GetMethod(input, @@toPrimitive).
        b. If exoticToPrim is not undefined, then
            [...]
        c. If preferredType is not present, let preferredType be number.
        d. Return ? OrdinaryToPrimitive(input, preferredType).
features: [Symbol.toPrimitive]
---*/

assert(delete Symbol.prototype[Symbol.toPrimitive]);

let valueOfGets = 0;
let valueOfCalls = 0;
let valueOfFunction = () => { ++valueOfCalls; return 123; };
Object.defineProperty(Symbol.prototype, "valueOf", {
    get: () => { ++valueOfGets; return valueOfFunction; },
});

assert(Object(Symbol()) == 123, "hint: default");
assert.sameValue(Object(Symbol()) - 0, 123, "hint: number");
assert.sameValue("".concat(Object(Symbol())), "Symbol()", "hint: string");

assert.sameValue(valueOfGets, 2);
assert.sameValue(valueOfCalls, 2);

let toStringGets = 0;
let toStringCalls = 0;
let toStringFunction = () => { ++toStringCalls; return "foo"; };
Object.defineProperty(Symbol.prototype, "toString", {
    get: () => { ++toStringGets; return toStringFunction; },
});

assert.sameValue("" + Object(Symbol()), "123", "hint: default");
assert.sameValue(Object(Symbol()) * 1, 123, "hint: number");
assert.sameValue({ "123": 1, "Symbol()": 2, "foo": 3 }[Object(Symbol())], 3, "hint: string");

assert.sameValue(valueOfGets, 4);
assert.sameValue(valueOfCalls, 4);
assert.sameValue(toStringGets, 1);
assert.sameValue(toStringCalls, 1);

valueOfFunction = null;

assert.sameValue(new Date(Object(Symbol())).getTime(), NaN, "hint: default");
assert.sameValue(+Object(Symbol()), NaN, "hint: number");
assert.sameValue(`${Object(Symbol())}`, "foo", "hint: string");

assert.sameValue(valueOfGets, 6);
assert.sameValue(valueOfCalls, 4);
assert.sameValue(toStringGets, 4);
assert.sameValue(toStringCalls, 4);

toStringFunction = function() { throw new Test262Error(); };

assert.throws(Test262Error, () => { Object(Symbol()) != 123; }, "hint: default");
assert.throws(Test262Error, () => { Object(Symbol()) / 0; }, "hint: number");
assert.throws(Test262Error, () => { "".concat(Object(Symbol())); }, "hint: string");

assert.sameValue(valueOfGets, 8);
assert.sameValue(valueOfCalls, 4);
assert.sameValue(toStringGets, 7);
assert.sameValue(toStringCalls, 4);

toStringFunction = undefined;

assert.throws(TypeError, () => { 1 + Object(Symbol()); }, "hint: default");
assert.throws(TypeError, () => { Number(Object(Symbol())); }, "hint: number");
assert.throws(TypeError, () => { String(Object(Symbol())); }, "hint: string");

assert.sameValue(valueOfGets, 11);
assert.sameValue(valueOfCalls, 4);
assert.sameValue(toStringGets, 10);
assert.sameValue(toStringCalls, 4);
