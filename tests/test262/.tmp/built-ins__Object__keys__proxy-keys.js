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


// Copyright (C) 2017 Caitlin Potter. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-object.keys
description: >
  Object.keys() observably performs [[GetOwnProperty]]
info: |
  19.1.2.16 Object.keys ( O )

  1. Let obj be ? ToObject(O).
  2. Let nameList be ? EnumerableOwnProperties(obj, "key").
  ...

  7.3.21 EnumerableOwnProperties ( O, kind )

  1. Assert: Type(O) is Object.
  2. Let ownKeys be ? O.[[OwnPropertyKeys]]().
  3. Let properties be a new empty List.
  4. For each element key of ownKeys in List order, do
    a. If Type(key) is String, then
      i. Let desc be ? O.[[GetOwnProperty]](key).
      ...
features: [Symbol]
---*/

let log = [];
let s = Symbol("test");
let target = {
  x: true
};

let ownKeys = {
  get length() {
    log.push({
      name: "get ownKeys['length']",
      receiver: this
    });
    return 3;
  },

  get 0() {
    log.push({
      name: "get ownKeys[0]",
      receiver: this
    });
    return "a";
  },

  get 1() {
    log.push({
      name: "get ownKeys[1]",
      receiver: this
    });
    return s;
  },

  get 2() {
    log.push({
      name: "get ownKeys[2]",
      receiver: this
    });
    return "b";
  }
};

let ownKeysDescriptors = {
  "a": {
    enumerable: true,
    configurable: true,
    value: 1
  },

  "b": {
    enumerable: false,
    configurable: true,
    value: 2
  },

  [s]: {
    enumerable: true,
    configurable: true,
    value: 3
  }
};

let handler = {
  get ownKeys() {
    log.push({
      name: "get handler.ownKeys",
      receiver: this
    });
    return (...args) => {
      log.push({
        name: "call handler.ownKeys",
        receiver: this,
        args
      });
      return ownKeys;
    };
  },

  get getOwnPropertyDescriptor() {
    log.push({
      name: "get handler.getOwnPropertyDescriptor",
      receiver: this
    });
    return (...args) => {
      log.push({
        name: "call handler.getOwnPropertyDescriptor",
        receiver: this,
        args
      });
      const name = args[1];
      return ownKeysDescriptors[name];
    };
  }
};

let proxy = new Proxy(target, handler);
let keys = Object.keys(proxy);

assert.sameValue(log.length, 10);

assert.sameValue(log[0].name, "get handler.ownKeys");
assert.sameValue(log[0].receiver, handler);

assert.sameValue(log[1].name, "call handler.ownKeys");
assert.sameValue(log[1].receiver, handler);
assert.sameValue(log[1].args.length, 1);
assert.sameValue(log[1].args[0], target);

// CreateListFromArrayLike(trapResultArray, « String, Symbol »).
assert.sameValue(log[2].name, "get ownKeys['length']");
assert.sameValue(log[2].receiver, ownKeys);

assert.sameValue(log[3].name, "get ownKeys[0]");
assert.sameValue(log[3].receiver, ownKeys);

assert.sameValue(log[4].name, "get ownKeys[1]");
assert.sameValue(log[4].receiver, ownKeys);

assert.sameValue(log[5].name, "get ownKeys[2]");
assert.sameValue(log[5].receiver, ownKeys);

// Let desc be ? O.[[GetOwnProperty]]("a").
assert.sameValue(log[6].name, "get handler.getOwnPropertyDescriptor");
assert.sameValue(log[6].receiver, handler);

assert.sameValue(log[7].name, "call handler.getOwnPropertyDescriptor");
assert.sameValue(log[7].receiver, handler);
assert.sameValue(log[7].args.length, 2);
assert.sameValue(log[7].args[0], target);
assert.sameValue(log[7].args[1], "a");

// Let desc be ? O.[[GetOwnProperty]]("b").
assert.sameValue(log[8].name, "get handler.getOwnPropertyDescriptor");
assert.sameValue(log[8].receiver, handler);

assert.sameValue(log[9].name, "call handler.getOwnPropertyDescriptor");
assert.sameValue(log[9].receiver, handler);
assert.sameValue(log[9].args.length, 2);
assert.sameValue(log[9].args[0], target);
assert.sameValue(log[9].args[1], "b");

// "a" is the only enumerable String-keyed property.
assert.sameValue(keys.length, 1);
assert.sameValue(keys[0], "a");
