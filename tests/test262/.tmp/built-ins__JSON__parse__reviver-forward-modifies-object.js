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


// Copyright (C) 2023 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-json.parse
description: Codepaths involving InternaliseJSONProperty behave as expected

includes: [compareArray.js]
features: [json-parse-with-source]
---*/

function assertOnlyOwnProperties(object, props, message) {
  assert.compareArray(Object.getOwnPropertyNames(object), props, `${message}: object should have no other properties than expected`);
  assert.compareArray(Object.getOwnPropertySymbols(object), [], `${message}: object should have no own symbol properties`);
}

const replacements = [
  42,
  ["foo"],
  { foo: "bar" },
  "foo"
];

// Test Array forward modify
for (const replacement of replacements) {
  let alreadyReplaced = false;
  let expectedKeys = ["0", "1", ""];
  // if the replacement is an object, add its keys to the expected keys
  if (typeof replacement === "object") {
    expectedKeys.splice(1, 0, ...Object.keys(replacement));
  }
  const o = JSON.parse("[1, 2]", function (k, v, { source }) {
    assert.sameValue(k, expectedKeys.shift());
    if (k === "0") {
      if (!alreadyReplaced) {
        this[1] = replacement;
        alreadyReplaced = true;
      }
    } else if (k !== "") {
      assert.sameValue(source, undefined);
    }
    return this[k];
  });
  assert.sameValue(expectedKeys.length, 0);
  assert.compareArray(o, [1, replacement], `array forward-modified with ${replacement}`);
}

function assertOnlyOwnProperties(object, props, message) {
  assert.compareArray(Object.getOwnPropertyNames(object), props, `${message}: object should have no other properties than expected`);
  assert.compareArray(Object.getOwnPropertySymbols(object), [], `${message}: object should have no own symbol properties`);
}

// Test Object forward modify
for (const replacement of replacements) {
  let alreadyReplaced = false;
  let expectedKeys = ["p", "q", ""];
  if (typeof replacement === "object") {
    expectedKeys.splice(1, 0, ...Object.keys(replacement));
  }
  const o = JSON.parse('{"p":1, "q":2}', function (k, v, { source }) {
    assert.sameValue(k, expectedKeys.shift());
    if (k === 'p') {
      if (!alreadyReplaced) {
        this.q = replacement;
        alreadyReplaced = true;
      }
    } else if (k !== "") {
      assert.sameValue(source, undefined);
    }
    return this[k];
  });
  assert.sameValue(expectedKeys.length, 0);
  assertOnlyOwnProperties(o, ["p", "q"], `object forward-modified with ${replacement}`);
  assert.sameValue(o.p, 1, "property p should not be replaced");
  assert.sameValue(o.q, replacement, `property q should be replaced with ${replacement}`);
}

// Test combinations of possible JSON input with multiple forward modifications

{
  let reviverCallIndex = 0;
  const expectedKeys = ["a", "b", "c", ""];
  const reviver = function(key, value, {source}) {
    assert.sameValue(key, expectedKeys[reviverCallIndex++]);
    if (key === "a") {
      this.b = 2;
      assert.sameValue(source, "0");
    } else if (key === "b") {
      this.c = 3;
      assert.sameValue(value, 2);
      assert.sameValue(source, undefined);
    } else if (key === "c") {
      assert.sameValue(value, 3);
      assert.sameValue(source, undefined);
    }
    return value;
  }
  const parsed = JSON.parse('{"a": 0, "b": 1, "c": [1, 2]}', reviver);
  assertOnlyOwnProperties(parsed, ["a", "b", "c"], "object with forward-modified properties");
  assert.sameValue(parsed.a, 0, "'a' property should be unmodified");
  assert.sameValue(parsed.b, 2, "'b' property should be modified to 2");
  assert.sameValue(parsed.c, 3, "'c' property should be modified to 3");
}

{
  let reviverCallIndex = 0;
  const expectedKeys = ["0", "1", "2", "3", ""];
  const reviver = function(key, value, {source}) {
    assert.sameValue(key, expectedKeys[reviverCallIndex++]);
    if (key === "0") {
      this[1] = 3;
      assert.sameValue(value, 1);
      assert.sameValue(source, "1");
    } else if (key === "1") {
      this[2] = 4;
      assert.sameValue(value, 3);
      assert.sameValue(source, undefined);
    } else if(key === "2") {
      this[3] = 5;
      assert.sameValue(value, 4);
      assert.sameValue(source, undefined);
    } else if(key === "5") {
      assert.sameValue(value, 5);
      assert.sameValue(source, undefined);
    }
    return value;
  }
  assert.compareArray(JSON.parse('[1, 2, 3, {"a": 1}]', reviver), [1, 3, 4, 5], "array with forward-modified elements");
}
