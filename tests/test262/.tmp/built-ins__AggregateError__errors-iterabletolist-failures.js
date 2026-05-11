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


// Copyright (C) 2019 Leo Balter. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-aggregate-error
description: >
  Return abrupt completion from IterableToList(errors)
info: |
  AggregateError ( errors, message )

  ...
  3. Let errorsList be ? IterableToList(errors).
  4. Set O.[[AggregateErrors]] to errorsList.
  ...
  6. Return O.

  Runtime Semantics: IterableToList ( items [ , method ] )

  1. If method is present, then
    ...
  2. Else,
    b. Let iteratorRecord be ? GetIterator(items, sync).
  3. Let values be a new empty List.
  4. Let next be true.
  5. Repeat, while next is not false
    a. Set next to ? IteratorStep(iteratorRecord).
    b. If next is not false, then
      i. Let nextValue be ? IteratorValue(next).
      ii. Append nextValue to the end of the List values.
  6. Return values.

  GetIterator ( obj [ , hint [ , method ] ] )

  ...
  3. If method is not present, then
    a. If hint is async, then
      ...
    b. Otherwise, set method to ? GetMethod(obj, @@iterator).
  4. Let iterator be ? Call(method, obj).
  5. If Type(iterator) is not Object, throw a TypeError exception.
  6. Let nextMethod be ? GetV(iterator, "next").
  ...
  8. Return iteratorRecord.
features: [AggregateError, Symbol.iterator]
---*/

var case1 = {
  get [Symbol.iterator]() {
    throw new Test262Error();
  }
};

assert.throws(Test262Error, () => {
  var obj = new AggregateError(case1);
}, 'get Symbol.iterator');

var case2 = {
  get [Symbol.iterator]() {
    return {};
  }
};

assert.throws(TypeError, () => {
  var obj = new AggregateError(case2);
}, 'GetMethod(obj, @@iterator) abrupts from non callable');

var case3 = {
  [Symbol.iterator]() {
    throw new Test262Error();
  }
};

assert.throws(Test262Error, () => {
  var obj = new AggregateError(case3);
}, 'Abrupt from @@iterator call');

var case4 = {
  [Symbol.iterator]() {
    return 'a string';
  }
};

assert.throws(TypeError, () => {
  var obj = new AggregateError(case4);
}, '@@iterator call returns a string');

var case5 = {
  [Symbol.iterator]() {
    return undefined;
  }
};

assert.throws(TypeError, () => {
  var obj = new AggregateError(case5);
}, '@@iterator call returns undefined');

var case6 = {
  [Symbol.iterator]() {
    return {
      get next() {
        throw new Test262Error();
      }
    }
  }
};

assert.throws(Test262Error, () => {
  var obj = new AggregateError(case6);
}, 'GetV(iterator, next) returns abrupt');

var case7 = {
  [Symbol.iterator]() {
    return {
      get next() {
        return {};
      }
    }
  }
};

assert.throws(TypeError, () => {
  var obj = new AggregateError(case7);
}, 'GetV(iterator, next) returns a non callable');

var case8 = {
  [Symbol.iterator]() {
    return {
      next() {
        throw new Test262Error();
      }
    }
  }
};

assert.throws(Test262Error, () => {
  var obj = new AggregateError(case8);
}, 'abrupt from iterator.next()');

var case9 = {
  [Symbol.iterator]() {
    return {
      next() {
        return undefined;
      }
    }
  }
};

assert.throws(TypeError, () => {
  var obj = new AggregateError(case9);
}, 'iterator.next() returns undefined');

var case10 = {
  [Symbol.iterator]() {
    return {
      next() {
        return 'a string';
      }
    }
  }
};

assert.throws(TypeError, () => {
  var obj = new AggregateError(case10);
}, 'iterator.next() returns a string');

var case11 = {
  [Symbol.iterator]() {
    return {
      next() {
        return {
          get done() {
            throw new Test262Error();
          }
        };
      }
    }
  }
};

assert.throws(Test262Error, () => {
  var obj = new AggregateError(case11);
}, 'IteratorCompete abrupts getting the done property');
