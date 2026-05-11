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


// Copyright (C) 2016 Jordan Harband.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Used to assert the correctness of object behavior in the presence
    and context of Proxy objects.
defines: [allowProxyTraps]
---*/

function allowProxyTraps(overrides) {
  function throwTest262Error(msg) {
    return function () { throw new Test262Error(msg); };
  }
  if (!overrides) { overrides = {}; }
  return {
    getPrototypeOf: overrides.getPrototypeOf || throwTest262Error('[[GetPrototypeOf]] trap called'),
    setPrototypeOf: overrides.setPrototypeOf || throwTest262Error('[[SetPrototypeOf]] trap called'),
    isExtensible: overrides.isExtensible || throwTest262Error('[[IsExtensible]] trap called'),
    preventExtensions: overrides.preventExtensions || throwTest262Error('[[PreventExtensions]] trap called'),
    getOwnPropertyDescriptor: overrides.getOwnPropertyDescriptor || throwTest262Error('[[GetOwnProperty]] trap called'),
    has: overrides.has || throwTest262Error('[[HasProperty]] trap called'),
    get: overrides.get || throwTest262Error('[[Get]] trap called'),
    set: overrides.set || throwTest262Error('[[Set]] trap called'),
    deleteProperty: overrides.deleteProperty || throwTest262Error('[[Delete]] trap called'),
    defineProperty: overrides.defineProperty || throwTest262Error('[[DefineOwnProperty]] trap called'),
    enumerate: throwTest262Error('[[Enumerate]] trap called: this trap has been removed'),
    ownKeys: overrides.ownKeys || throwTest262Error('[[OwnPropertyKeys]] trap called'),
    apply: overrides.apply || throwTest262Error('[[Call]] trap called'),
    construct: overrides.construct || throwTest262Error('[[Construct]] trap called')
  };
}


// Copyright (C) 2017 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-array.prototype.reverse
description: >
  Ensure correct MOP operations are called when length exceeds 2^53-1.
includes: [compareArray.js, proxyTrapsHelper.js]
features: [exponentiation]
---*/

function StopReverse() {}

var arrayLike = {
  0: "zero",
  /* 1: hole, */
  2: "two",
  /* 3: hole, */

  get 4() {
    throw new StopReverse();
  },

  9007199254740987: "2**53-5",
  /* 9007199254740988: hole, */
  /* 9007199254740989: hole, */
  9007199254740990: "2**53-2",

  length: 2 ** 53 + 2,
};

var traps = [];

var proxy = new Proxy(arrayLike, allowProxyTraps({
  getOwnPropertyDescriptor(t, pk) {
    traps.push(`GetOwnPropertyDescriptor:${String(pk)}`);
    return Reflect.getOwnPropertyDescriptor(t, pk);
  },
  defineProperty(t, pk, desc) {
    traps.push(`DefineProperty:${String(pk)}`);
    return Reflect.defineProperty(t, pk, desc);
  },
  has(t, pk) {
    traps.push(`Has:${String(pk)}`);
    return Reflect.has(t, pk);
  },
  get(t, pk, r) {
    traps.push(`Get:${String(pk)}`);
    return Reflect.get(t, pk, r);
  },
  set(t, pk, v, r) {
    traps.push(`Set:${String(pk)}`);
    return Reflect.set(t, pk, v, r);
  },
  deleteProperty(t, pk) {
    traps.push(`Delete:${String(pk)}`);
    return Reflect.deleteProperty(t, pk);
  },
}))

// Uses a separate exception than Test262Error, so that errors from allowProxyTraps
// are properly propagated.
assert.throws(StopReverse, function() {
  Array.prototype.reverse.call(proxy);
}, 'Array.prototype.reverse.call(proxy) throws a StopReverse exception');

assert.compareArray(traps, [
  // Initial get length operation.
  "Get:length",

  // Lower and upper index are both present.
  "Has:0",
  "Get:0",
  "Has:9007199254740990",
  "Get:9007199254740990",
  "Set:0",
  "GetOwnPropertyDescriptor:0",
  "DefineProperty:0",
  "Set:9007199254740990",
  "GetOwnPropertyDescriptor:9007199254740990",
  "DefineProperty:9007199254740990",

  // Lower and upper index are both absent.
  "Has:1",
  "Has:9007199254740989",

  // Lower index is present, upper index is absent.
  "Has:2",
  "Get:2",
  "Has:9007199254740988",
  "Delete:2",
  "Set:9007199254740988",
  "GetOwnPropertyDescriptor:9007199254740988",
  "DefineProperty:9007199254740988",

  // Lower index is absent, upper index is present.
  "Has:3",
  "Has:9007199254740987",
  "Get:9007199254740987",
  "Set:3",
  "GetOwnPropertyDescriptor:3",
  "DefineProperty:3",
  "Delete:9007199254740987",

  // Stop exception.
  "Has:4",
  "Get:4",
], 'The value of traps is expected to be [\n  // Initial get length operation.\n  "Get:length",\n\n  // Lower and upper index are both present.\n  "Has:0",\n  "Get:0",\n  "Has:9007199254740990",\n  "Get:9007199254740990",\n  "Set:0",\n  "GetOwnPropertyDescriptor:0",\n  "DefineProperty:0",\n  "Set:9007199254740990",\n  "GetOwnPropertyDescriptor:9007199254740990",\n  "DefineProperty:9007199254740990",\n\n  // Lower and upper index are both absent.\n  "Has:1",\n  "Has:9007199254740989",\n\n  // Lower index is present, upper index is absent.\n  "Has:2",\n  "Get:2",\n  "Has:9007199254740988",\n  "Delete:2",\n  "Set:9007199254740988",\n  "GetOwnPropertyDescriptor:9007199254740988",\n  "DefineProperty:9007199254740988",\n\n  // Lower index is absent, upper index is present.\n  "Has:3",\n  "Has:9007199254740987",\n  "Get:9007199254740987",\n  "Set:3",\n  "GetOwnPropertyDescriptor:3",\n  "DefineProperty:3",\n  "Delete:9007199254740987",\n\n  // Stop exception.\n  "Has:4",\n  "Get:4",\n]');

assert.sameValue(arrayLike.length, 2 ** 53 + 2, 'The value of arrayLike.length is expected to be 2 ** 53 + 2');

assert.sameValue(arrayLike[0], "2**53-2", 'The value of arrayLike[0] is expected to be "2**53-2"');
assert.sameValue(1 in arrayLike, false, 'The result of evaluating (1 in arrayLike) is expected to be false');
assert.sameValue(2 in arrayLike, false, 'The result of evaluating (2 in arrayLike) is expected to be false');
assert.sameValue(arrayLike[3], "2**53-5", 'The value of arrayLike[3] is expected to be "2**53-5"');

assert.sameValue(9007199254740987 in arrayLike, false, 'The result of evaluating (9007199254740987 in arrayLike) is expected to be false');
assert.sameValue(arrayLike[9007199254740988], "two", 'The value of arrayLike[9007199254740988] is expected to be "two"');
assert.sameValue(9007199254740989 in arrayLike, false, 'The result of evaluating (9007199254740989 in arrayLike) is expected to be false');
assert.sameValue(arrayLike[9007199254740990], "zero", 'The value of arrayLike[9007199254740990] is expected to be "zero"');
