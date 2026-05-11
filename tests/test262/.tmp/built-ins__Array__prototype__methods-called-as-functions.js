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


// Copyright (C) 2020 Alexey Shvayka. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-properties-of-the-array-prototype-object
description: >
    Array.prototype methods resolve `this` value using strict mode semantics,
    throwing TypeError if called as top-level function.
info: |
    Array.prototype.concat ( ...items )

    1. Let O be ? ToObject(this value).

    ToObject ( argument )

    Argument Type: Undefined
    Result: Throw a TypeError exception.
features: [Symbol, Symbol.isConcatSpreadable, Symbol.iterator, Symbol.species, Array.prototype.flat, Array.prototype.flatMap, Array.prototype.includes]
---*/

["constructor", "length", "0", Symbol.isConcatSpreadable, Symbol.species].forEach(function(key) {
    Object.defineProperty(this, key, {
        get: function() {
            throw new Test262Error(String(key) + " lookup should not be performed");
        },
    });
}, this);

function callback() {
    throw new Test262Error("callback should not be called");
}

var index = {
    get valueOf() {
        throw new Test262Error("index should not be coerced to number");
    },
};

var separator = {
    get toString() {
        throw new Test262Error("separator should not be coerced to string");
    },
};

var concat = Array.prototype.concat;
assert.throws(TypeError, function() {
    concat();
}, "concat");

var copyWithin = Array.prototype.copyWithin;
assert.throws(TypeError, function() {
    copyWithin(index, index);
}, "copyWithin");

var entries = Array.prototype.entries;
assert.throws(TypeError, function() {
    entries();
}, "entries");

var every = Array.prototype.every;
assert.throws(TypeError, function() {
    every(callback);
}, "every");

var fill = Array.prototype.fill;
assert.throws(TypeError, function() {
    fill(0);
}, "fill");

var filter = Array.prototype.filter;
assert.throws(TypeError, function() {
    filter(callback);
}, "filter");

var find = Array.prototype.find;
assert.throws(TypeError, function() {
    find(callback);
}, "find");

var findIndex = Array.prototype.findIndex;
assert.throws(TypeError, function() {
    findIndex(callback);
}, "findIndex");

var flat = Array.prototype.flat;
assert.throws(TypeError, function() {
    flat(index);
}, "flat");

var flatMap = Array.prototype.flatMap;
assert.throws(TypeError, function() {
    flatMap(callback);
}, "flatMap");

var forEach = Array.prototype.forEach;
assert.throws(TypeError, function() {
    forEach(callback);
}, "forEach");

var includes = Array.prototype.includes;
assert.throws(TypeError, function() {
    includes(0, index);
}, "includes");

var indexOf = Array.prototype.indexOf;
assert.throws(TypeError, function() {
    indexOf(0, index);
}, "indexOf");

var join = Array.prototype.join;
assert.throws(TypeError, function() {
    join(separator);
}, "join");

var keys = Array.prototype.keys;
assert.throws(TypeError, function() {
    keys();
}, "keys");

var lastIndexOf = Array.prototype.lastIndexOf;
assert.throws(TypeError, function() {
    lastIndexOf(0, index);
}, "lastIndexOf");

var map = Array.prototype.map;
assert.throws(TypeError, function() {
    map(callback);
}, "map");

var pop = Array.prototype.pop;
assert.throws(TypeError, function() {
    pop();
}, "pop");

var push = Array.prototype.push;
assert.throws(TypeError, function() {
    push();
}, "push");

var reduce = Array.prototype.reduce;
assert.throws(TypeError, function() {
    reduce(callback, 0);
}, "reduce");

var reduceRight = Array.prototype.reduceRight;
assert.throws(TypeError, function() {
    reduceRight(callback, 0);
}, "reduceRight");

var reverse = Array.prototype.reverse;
assert.throws(TypeError, function() {
    reverse();
}, "reverse");

var shift = Array.prototype.shift;
assert.throws(TypeError, function() {
    shift();
}, "shift");

var slice = Array.prototype.slice;
assert.throws(TypeError, function() {
    slice(index, index);
}, "slice");

var some = Array.prototype.some;
assert.throws(TypeError, function() {
    some(callback);
}, "some");

var sort = Array.prototype.sort;
assert.throws(TypeError, function() {
    sort(callback);
}, "sort");

var splice = Array.prototype.splice;
assert.throws(TypeError, function() {
    splice(index, index);
}, "splice");

var toLocaleString = Array.prototype.toLocaleString;
assert.throws(TypeError, function() {
    toLocaleString();
}, "toLocaleString");

var toString = Array.prototype.toString;
assert.throws(TypeError, function() {
    toString();
}, "toString");

var unshift = Array.prototype.unshift;
assert.throws(TypeError, function() {
    unshift();
}, "unshift");

var values = Array.prototype.values;
assert.throws(TypeError, function() {
    values();
}, "values");

var iterator = Array.prototype[Symbol.iterator];
assert.throws(TypeError, function() {
    iterator();
}, "Symbol.iterator");
