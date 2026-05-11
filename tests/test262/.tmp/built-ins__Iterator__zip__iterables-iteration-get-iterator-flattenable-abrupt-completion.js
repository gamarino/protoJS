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


// Copyright (C) 2025 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-iterator.zip
description: >
  Handle abrupt completions during iterables iteration.
info: |
  Iterator.zip ( iterables [ , options ] )
    ...
    12. Repeat, while next is not done,
      ...
      c. If next is not done, then
        i. Let iter be Completion(GetIteratorFlattenable(next, reject-strings)).
        ii. IfAbruptCloseIterators(iter, the list-concatenation of « inputIter » and iters).
    ...

  GetIteratorFlattenable ( obj, primitiveHandling )
    1. If obj is not an Object, then
      a. If primitiveHandling is reject-primitives, throw a TypeError exception.
      b. Assert: primitiveHandling is iterate-string-primitives.
      c. If obj is not a String, throw a TypeError exception.
    2. Let method be ? GetMethod(obj, %Symbol.iterator%).
    3. If method is undefined, then
      a. Let iterator be obj.
    4. Else,
      a. Let iterator be ? Call(method, obj).
    5. If iterator is not an Object, throw a TypeError exception.
    6. Return ? GetIteratorDirect(iterator).

  IteratorCloseAll ( iters, completion )
    1. For each element iter of iters, in reverse List order, do
      a. Set completion to Completion(IteratorClose(iter, completion)).
    2. Return ? completion.

  IteratorClose ( iteratorRecord, completion )
    1. Assert: iteratorRecord.[[Iterator]] is an Object.
    2. Let iterator be iteratorRecord.[[Iterator]].
    3. Let innerResult be Completion(GetMethod(iterator, "return")).
    4. If innerResult is a normal completion, then
      a. Let return be innerResult.[[Value]].
      b. If return is undefined, return ? completion.
      c. Set innerResult to Completion(Call(return, iterator)).
    5. If completion is a throw completion, return ? completion.
    ...
includes: [compareArray.js]
features: [joint-iteration]
---*/

class ExpectedError extends Error {}

var badIterators = [
  // Throw TypeError in GetIteratorFlattenable because strings are rejected.
  {
    iterator: "bad iterator",
    error: TypeError
  },

  // Throw an error when GetIteratorFlattenable performs GetMethod.
  {
    iterator: {
      get [Symbol.iterator]() {
        throw new ExpectedError();
      }
    },
    error: ExpectedError,
  },

  // Throw an error when GetIteratorFlattenable performs Call.
  {
    iterator: {
      [Symbol.iterator]() {
        throw new ExpectedError();
      }
    },
    error: ExpectedError,
  },

  // Throw an error when GetIteratorFlattenable performs GetIteratorDirect.
  {
    iterator: {
      get next() {
        throw new ExpectedError();
      }
    },
    error: ExpectedError,
  },
];

function makeIterables(badIterator) {
  var log = [];

  var first = {
    next() {
      log.push("unexpected call to next method");
    },
    return() {
      // Called with the correct receiver and no arguments.
      assert.sameValue(this, first);
      assert.sameValue(arguments.length, 0);

      // NB: Log after above asserts, because failures aren't propagated.
      log.push("close first iterator");

      // IteratorClose ignores new exceptions when called with a Throw completion.
      throw new Test262Error();
    },
  };

  var second = {
    next() {
      log.push("unexpected call to next method");
    },
    return() {
      // Called with the correct receiver and no arguments.
      assert.sameValue(this, second);
      assert.sameValue(arguments.length, 0);

      // NB: Log after above asserts, because failures aren't propagated.
      log.push("close second iterator");

      // IteratorClose ignores new exceptions when called with a Throw completion.
      throw new Test262Error();
    },
  };

  var elements = [first, second, badIterator];
  var elementsIter = elements.values();

  var iterables = {
    [Symbol.iterator]() {
      return this;
    },
    next() {
      log.push("call next");
      return elementsIter.next();
    },
    return() {
      log.push("close iterables iterator");

      // IteratorClose ignores new exceptions when called with a Throw completion.
      throw new Test262Error();
    },
  };

  return {log, iterables};
}

for (var {iterator, error} of badIterators) {
  var {log, iterables} = makeIterables(iterator);

  assert.throws(error, function() {
    Iterator.zip(iterables);
  });

  // Ensure iterators are closed in the correct order.
  assert.compareArray(log, [
    "call next",
    "call next",
    "call next",
    "close second iterator",
    "close first iterator",
    "close iterables iterator",
  ]);
}
