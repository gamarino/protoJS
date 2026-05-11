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


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Deprecated now that compareArray is defined in assert.js.
defines: [compareArray]
---*/


// Copyright (C) 2025 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-iterator.zip
description: >
  Perform iteration of the "padding" option.
info: |
  Iterator.zip ( iterables [ , options ] )
    ...
    14. If mode is "longest", then
      ...
      b. Else,
        i. Let paddingIter be Completion(GetIterator(paddingOption, sync)).
        ...
        iii. Let usingIterator be true.
        iv. Perform the following steps iterCount times:
          1. If usingIterator is true, then
            a. Set next to Completion(IteratorStepValue(paddingIter)).
            ...
            c. If next is done, then
              i. Set usingIterator to false.
            d. Else,
              i. Append next to padding.
          2. If usingIterator is false, append undefined to padding.
            v. If usingIterator is true, then
              1. Let completion be Completion(IteratorClose(paddingIter, NormalCompletion(unused))).
              ...
    ...

  GetIterator ( obj, kind )
    ...
    2. Else,
      a. Let method be ? GetMethod(obj, %Symbol.iterator%).
    3. If method is undefined, throw a TypeError exception.
    4. Return ? GetIteratorFromMethod(obj, method).

  GetIteratorFromMethod ( obj, method )
    1. Let iterator be ? Call(method, obj).
    2. If iterator is not an Object, throw a TypeError exception.
    3. Return ? GetIteratorDirect(iterator).

  GetIteratorDirect ( obj )
    1. Let nextMethod be ? Get(obj, "next").
    2. Let iteratorRecord be the Iterator Record { [[Iterator]]: obj, [[NextMethod]]: nextMethod, [[Done]]: false }.
    3. Return iteratorRecord.
includes: [proxyTrapsHelper.js, compareArray.js]
features: [joint-iteration]
---*/

function makeProxyWithGetHandler(log, name, obj) {
  return new Proxy(obj, allowProxyTraps({
    get(target, propertyKey, receiver) {
      log.push(`${name}::${String(propertyKey)}`);
      return Reflect.get(target, propertyKey, receiver);
    }
  }));
}

// "padding" option must be an iterable.
assert.throws(TypeError, function() {
  Iterator.zip([], {
    mode: "longest",
    padding: {},
  });
});

for (var n = 0; n <= 5; ++n) {
  var iterables = Array(n).fill([]);

  for (var k = 0; k <= n + 2; ++k) {
    var elements = Array(k).fill(0);
    var elementsIter = elements.values();

    var log = [];

    var padding = makeProxyWithGetHandler(log, "padding", {
      [Symbol.iterator]() {
        log.push("call iterator");

        // Called with the correct receiver and no arguments.
        assert.sameValue(this, padding);
        assert.sameValue(arguments.length, 0);

        return this;
      },
      next() {
        log.push("call next");

        // Called with the correct receiver and no arguments.
        assert.sameValue(this, padding);
        assert.sameValue(arguments.length, 0);

        return elementsIter.next();
      },
      return() {
        log.push("call return");

        // Called with the correct receiver and no arguments.
        assert.sameValue(this, padding);
        assert.sameValue(arguments.length, 0);

        return {};
      }
    });

    Iterator.zip(iterables, {mode: "longest", padding});

    // Property reads and calls from GetIterator.
    var expected = [
      "padding::Symbol(Symbol.iterator)",
      "call iterator",
      "padding::next",
    ];

    // Call the "next" method |n| times until the padding iterator is exhausted.
    for (var i = 0; i < Math.min(n, k); ++i) {
      expected.push("call next");
    }

    // If |n| is larger than |k|, then there was one final call to the "next"
    // method. Otherwise the "return" method was called to close the padding
    // iterator.
    if (n > k) {
      expected.push("call next");
    } else {
      expected.push(
        "padding::return",
        "call return"
      );
    }

    assert.compareArray(log, expected);
  }
}
