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


// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    A collection of assertion and wrapper functions for testing asynchronous built-ins.
defines: [asyncTest, assert.throwsAsync]
---*/

/**
 * Defines the **sole** asynchronous test of a file.
 * @see {@link ../docs/rfcs/async-helpers.md} for background.
 *
 * @param {Function} testFunc a callback whose returned promise indicates test results
 *   (fulfillment for success, rejection for failure)
 * @returns {void}
 */
function asyncTest(testFunc) {
  if (!Object.prototype.hasOwnProperty.call(globalThis, "$DONE")) {
    throw new Test262Error("asyncTest called without async flag");
  }
  if (typeof testFunc !== "function") {
    $DONE(new Test262Error("asyncTest called with non-function argument"));
    return;
  }
  try {
    testFunc().then(
      function () {
        $DONE();
      },
      function (error) {
        $DONE(error);
      }
    );
  } catch (syncError) {
    $DONE(syncError);
  }
}

/**
 * Asserts that a callback asynchronously throws an instance of a particular
 * error (i.e., returns a promise whose rejection value is an object referencing
 * the constructor).
 *
 * @param {Function} expectedErrorConstructor the expected constructor of the
 *   rejection value
 * @param {Function} func the callback
 * @param {string} [message] the prefix to use for failure messages
 * @returns {Promise<void>} fulfills if the expected error is thrown,
 *   otherwise rejects
 */
assert.throwsAsync = function (expectedErrorConstructor, func, message) {
  return new Promise(function (resolve) {
    var fail = function (detail) {
      if (message === undefined) {
        throw new Test262Error(detail);
      }
      throw new Test262Error(message + " " + detail);
    };
    if (typeof expectedErrorConstructor !== "function") {
      fail("assert.throwsAsync called with an argument that is not an error constructor");
    }
    if (typeof func !== "function") {
      fail("assert.throwsAsync called with an argument that is not a function");
    }
    var expectedName = expectedErrorConstructor.name;
    var expectation = "Expected a " + expectedName + " to be thrown asynchronously";
    var res;
    try {
      res = func();
    } catch (thrown) {
      fail(expectation + " but the function threw synchronously");
    }
    if (res === null || typeof res !== "object" || typeof res.then !== "function") {
      fail(expectation + " but result was not a thenable");
    }
    var onResFulfilled, onResRejected;
    var resSettlementP = new Promise(function (onFulfilled, onRejected) {
      onResFulfilled = onFulfilled;
      onResRejected = onRejected;
    });
    try {
      res.then(onResFulfilled, onResRejected)
    } catch (thrown) {
      fail(expectation + " but .then threw synchronously");
    }
    resolve(resSettlementP.then(
      function () {
        fail(expectation + " but no exception was thrown at all");
      },
      function (thrown) {
        var actualName;
        if (thrown === null || typeof thrown !== "object") {
          fail(expectation + " but thrown value was not an object");
        } else if (thrown.constructor !== expectedErrorConstructor) {
          actualName = thrown.constructor.name;
          if (expectedName === actualName) {
            fail(expectation +
              " but got a different error constructor with the same name");
          }
          fail(expectation + " but got a " + actualName);
        }
      }
    ));
  });
};


// Copyright 2019 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: >
  Compare two values structurally
defines: [assert.deepEqual]
---*/

assert.deepEqual = function(actual, expected, message) {
  var format = assert.deepEqual.format;
  var mustBeTrue = assert.deepEqual._compare(actual, expected);

  // format can be slow when `actual` or `expected` are large objects, like for
  // example the global object, so only call it when the assertion will fail.
  if (mustBeTrue !== true) {
    message = `Expected ${format(actual)} to be structurally equal to ${format(expected)}. ${(message || '')}`;
  }

  assert(mustBeTrue, message);
};

(function() {
let getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
let join = arr => arr.join(', ');
function stringFromTemplate(strings, subs) {
  let parts = strings.map((str, i) => `${i === 0 ? '' : subs[i - 1]}${str}`);
  return parts.join('');
}
function escapeKey(key) {
  if (typeof key === 'symbol') return `[${String(key)}]`;
  if (/^[a-zA-Z0-9_$]+$/.test(key)) return key;
  return assert._formatIdentityFreeValue(key);
}

assert.deepEqual.format = function(value, seen) {
  let basic = assert._formatIdentityFreeValue(value);
  if (basic) return basic;
  switch (value === null ? 'null' : typeof value) {
    case 'string':
    case 'bigint':
    case 'number':
    case 'boolean':
    case 'undefined':
    case 'null':
      assert(false, 'values without identity should use basic formatting');
      break;
    case 'symbol':
    case 'function':
    case 'object':
      break;
    default:
      return typeof value;
  }

  if (!seen) {
    seen = {
      counter: 0,
      map: new Map()
    };
  }
  let usage = seen.map.get(value);
  if (usage) {
    usage.used = true;
    return `ref #${usage.id}`;
  }
  usage = { id: ++seen.counter, used: false };
  seen.map.set(value, usage);

  // Properly communicating multiple references requires deferred rendering of
  // all identity-bearing values until the outermost format call finishes,
  // because the current value can also in appear in a not-yet-visited part of
  // the object graph (which, when visited, will update the usage object).
  //
  // To preserve readability of the desired output formatting, we accomplish
  // this deferral using tagged template literals.
  // Evaluation closes over the usage object and returns a function that accepts
  // "mapper" arguments for rendering the corresponding substitution values and
  // returns an object with only a toString method which will itself be invoked
  // when trying to use the result as a string in assert.deepEqual.
  //
  // For convenience, any absent mapper is presumed to be `String`, and the
  // function itself has a toString method that self-invokes with no mappers
  // (allowing returning the function directly when every mapper is `String`).
  function lazyResult(strings, ...subs) {
    function acceptMappers(...mappers) {
      function toString() {
        let renderings = subs.map((sub, i) => (mappers[i] || String)(sub));
        let rendered = stringFromTemplate(strings, renderings);
        if (usage.used) rendered += ` as #${usage.id}`;
        return rendered;
      }

      return { toString };
    }

    acceptMappers.toString = () => String(acceptMappers());
    return acceptMappers;
  }

  let format = assert.deepEqual.format;
  function lazyString(strings, ...subs) {
    return { toString: () => stringFromTemplate(strings, subs) };
  }

  if (typeof value === 'function') {
    return lazyResult`function${value.name ? ` ${String(value.name)}` : ''}`;
  }
  if (typeof value !== 'object') {
    // probably a symbol
    return lazyResult`${value}`;
  }
  if (Array.isArray ? Array.isArray(value) : value instanceof Array) {
    return lazyResult`[${value.map(value => format(value, seen))}]`(join);
  }
  if (value instanceof Date) {
    return lazyResult`Date(${format(value.toISOString(), seen)})`;
  }
  if (value instanceof Error) {
    return lazyResult`error ${value.name || 'Error'}(${format(value.message, seen)})`;
  }
  if (value instanceof RegExp) {
    return lazyResult`${value}`;
  }
  if (typeof Map !== "undefined" && value instanceof Map) {
    let contents = Array.from(value).map(pair => lazyString`${format(pair[0], seen)} => ${format(pair[1], seen)}`);
    return lazyResult`Map {${contents}}`(join);
  }
  if (typeof Set !== "undefined" && value instanceof Set) {
    let contents = Array.from(value).map(value => format(value, seen));
    return lazyResult`Set {${contents}}`(join);
  }

  let tag = Symbol.toStringTag && Symbol.toStringTag in value
    ? value[Symbol.toStringTag]
    : Object.getPrototypeOf(value) === null ? '[Object: null prototype]' : 'Object';
  let keys = Reflect.ownKeys(value).filter(key => getOwnPropertyDescriptor(value, key).enumerable);
  let contents = keys.map(key => lazyString`${escapeKey(key)}: ${format(value[key], seen)}`);
  return lazyResult`${tag ? `${tag} ` : ''}{${contents}}`(String, join);
};
})();

assert.deepEqual._compare = (function () {
  var EQUAL = 1;
  var NOT_EQUAL = -1;
  var UNKNOWN = 0;

  function deepEqual(a, b) {
    return compareEquality(a, b) === EQUAL;
  }

  function compareEquality(a, b, cache) {
    return compareIf(a, b, isOptional, compareOptionality)
      || compareIf(a, b, isPrimitiveEquatable, comparePrimitiveEquality)
      || compareIf(a, b, isObjectEquatable, compareObjectEquality, cache)
      || NOT_EQUAL;
  }

  function compareIf(a, b, test, compare, cache) {
    return !test(a)
      ? !test(b) ? UNKNOWN : NOT_EQUAL
      : !test(b) ? NOT_EQUAL : cacheComparison(a, b, compare, cache);
  }

  function tryCompareStrictEquality(a, b) {
    return a === b ? EQUAL : UNKNOWN;
  }

  function tryCompareTypeOfEquality(a, b) {
    return typeof a !== typeof b ? NOT_EQUAL : UNKNOWN;
  }

  function tryCompareToStringTagEquality(a, b) {
    var aTag = Symbol.toStringTag in a ? a[Symbol.toStringTag] : undefined;
    var bTag = Symbol.toStringTag in b ? b[Symbol.toStringTag] : undefined;
    return aTag !== bTag ? NOT_EQUAL : UNKNOWN;
  }

  function isOptional(value) {
    return value === undefined
      || value === null;
  }

  function compareOptionality(a, b) {
    return tryCompareStrictEquality(a, b)
      || NOT_EQUAL;
  }

  function isPrimitiveEquatable(value) {
    switch (typeof value) {
      case 'string':
      case 'number':
      case 'bigint':
      case 'boolean':
      case 'symbol':
        return true;
      default:
        return isBoxed(value);
    }
  }

  function comparePrimitiveEquality(a, b) {
    if (isBoxed(a)) a = a.valueOf();
    if (isBoxed(b)) b = b.valueOf();
    return tryCompareStrictEquality(a, b)
      || tryCompareTypeOfEquality(a, b)
      || compareIf(a, b, isNaNEquatable, compareNaNEquality)
      || NOT_EQUAL;
  }

  function isNaNEquatable(value) {
    return typeof value === 'number';
  }

  function compareNaNEquality(a, b) {
    return isNaN(a) && isNaN(b) ? EQUAL : NOT_EQUAL;
  }

  function isObjectEquatable(value) {
    return typeof value === 'object' || typeof value === 'function';
  }

  function compareObjectEquality(a, b, cache) {
    if (!cache) cache = new Map();
    return getCache(cache, a, b)
      || setCache(cache, a, b, EQUAL) // consider equal for now
      || cacheComparison(a, b, tryCompareStrictEquality, cache)
      || cacheComparison(a, b, tryCompareToStringTagEquality, cache)
      || compareIf(a, b, isValueOfEquatable, compareValueOfEquality)
      || compareIf(a, b, isToStringEquatable, compareToStringEquality)
      || compareIf(a, b, isArrayLikeEquatable, compareArrayLikeEquality, cache)
      || compareIf(a, b, isStructurallyEquatable, compareStructuralEquality, cache)
      || compareIf(a, b, isIterableEquatable, compareIterableEquality, cache)
      || cacheComparison(a, b, fail, cache);
  }

  function isBoxed(value) {
    return value instanceof String
      || value instanceof Number
      || value instanceof Boolean
      || typeof Symbol === 'function' && value instanceof Symbol
      || typeof BigInt === 'function' && value instanceof BigInt;
  }

  function isValueOfEquatable(value) {
    return value instanceof Date;
  }

  function compareValueOfEquality(a, b) {
    return compareIf(a.valueOf(), b.valueOf(), isPrimitiveEquatable, comparePrimitiveEquality)
      || NOT_EQUAL;
  }

  function isToStringEquatable(value) {
    return value instanceof RegExp;
  }

  function compareToStringEquality(a, b) {
    return compareIf(a.toString(), b.toString(), isPrimitiveEquatable, comparePrimitiveEquality)
      || NOT_EQUAL;
  }

  function isArrayLikeEquatable(value) {
    return (Array.isArray ? Array.isArray(value) : value instanceof Array)
      || (typeof Uint8Array === 'function' && value instanceof Uint8Array)
      || (typeof Uint8ClampedArray === 'function' && value instanceof Uint8ClampedArray)
      || (typeof Uint16Array === 'function' && value instanceof Uint16Array)
      || (typeof Uint32Array === 'function' && value instanceof Uint32Array)
      || (typeof Int8Array === 'function' && value instanceof Int8Array)
      || (typeof Int16Array === 'function' && value instanceof Int16Array)
      || (typeof Int32Array === 'function' && value instanceof Int32Array)
      || (typeof Float32Array === 'function' && value instanceof Float32Array)
      || (typeof Float64Array === 'function' && value instanceof Float64Array)
      || (typeof BigUint64Array === 'function' && value instanceof BigUint64Array)
      || (typeof BigInt64Array === 'function' && value instanceof BigInt64Array);
  }

  function compareArrayLikeEquality(a, b, cache) {
    if (a.length !== b.length) return NOT_EQUAL;
    for (var i = 0; i < a.length; i++) {
      if (compareEquality(a[i], b[i], cache) === NOT_EQUAL) {
        return NOT_EQUAL;
      }
    }
    return EQUAL;
  }

  function isStructurallyEquatable(value) {
    return !(typeof Promise === 'function' && value instanceof Promise // only comparable by reference
      || typeof WeakMap === 'function' && value instanceof WeakMap // only comparable by reference
      || typeof WeakSet === 'function' && value instanceof WeakSet // only comparable by reference
      || typeof Map === 'function' && value instanceof Map // comparable via @@iterator
      || typeof Set === 'function' && value instanceof Set); // comparable via @@iterator
  }

  function compareStructuralEquality(a, b, cache) {
    var aKeys = [];
    for (var key in a) aKeys.push(key);

    var bKeys = [];
    for (var key in b) bKeys.push(key);

    if (aKeys.length !== bKeys.length) {
      return NOT_EQUAL;
    }

    aKeys.sort();
    bKeys.sort();

    for (var i = 0; i < aKeys.length; i++) {
      var aKey = aKeys[i];
      var bKey = bKeys[i];
      if (compareEquality(aKey, bKey, cache) === NOT_EQUAL) {
        return NOT_EQUAL;
      }
      if (compareEquality(a[aKey], b[bKey], cache) === NOT_EQUAL) {
        return NOT_EQUAL;
      }
    }

    return compareIf(a, b, isIterableEquatable, compareIterableEquality, cache)
      || EQUAL;
  }

  function isIterableEquatable(value) {
    return typeof Symbol === 'function'
      && typeof value[Symbol.iterator] === 'function';
  }

  function compareIteratorEquality(a, b, cache) {
    if (typeof Map === 'function' && a instanceof Map && b instanceof Map ||
      typeof Set === 'function' && a instanceof Set && b instanceof Set) {
      if (a.size !== b.size) return NOT_EQUAL; // exit early if we detect a difference in size
    }

    var ar, br;
    while (true) {
      ar = a.next();
      br = b.next();
      if (ar.done) {
        if (br.done) return EQUAL;
        if (b.return) b.return();
        return NOT_EQUAL;
      }
      if (br.done) {
        if (a.return) a.return();
        return NOT_EQUAL;
      }
      if (compareEquality(ar.value, br.value, cache) === NOT_EQUAL) {
        if (a.return) a.return();
        if (b.return) b.return();
        return NOT_EQUAL;
      }
    }
  }

  function compareIterableEquality(a, b, cache) {
    return compareIteratorEquality(a[Symbol.iterator](), b[Symbol.iterator](), cache);
  }

  function cacheComparison(a, b, compare, cache) {
    var result = compare(a, b, cache);
    if (cache && (result === EQUAL || result === NOT_EQUAL)) {
      setCache(cache, a, b, /** @type {EQUAL | NOT_EQUAL} */(result));
    }
    return result;
  }

  function fail() {
    return NOT_EQUAL;
  }

  function setCache(cache, left, right, result) {
    var otherCache;

    otherCache = cache.get(left);
    if (!otherCache) cache.set(left, otherCache = new Map());
    otherCache.set(right, result);

    otherCache = cache.get(right);
    if (!otherCache) cache.set(right, otherCache = new Map());
    otherCache.set(left, result);
  }

  function getCache(cache, left, right) {
    var otherCache;
    var result;

    otherCache = cache.get(left);
    result = otherCache && otherCache.get(right);
    if (result) return result;

    otherCache = cache.get(right);
    result = otherCache && otherCache.get(left);
    if (result) return result;

    return UNKNOWN;
  }

  return deepEqual;
})();


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
defines: [$DONE]
---*/

function __consolePrintHandle__(msg) {
  print(msg);
}

function $DONE(error) {
  if (error) {
    if(typeof error === 'object' && error !== null && 'name' in error) {
      __consolePrintHandle__('Test262:AsyncTestFailure:' + error.name + ': ' + error.message);
    } else {
      __consolePrintHandle__('Test262:AsyncTestFailure:Test262Error: ' + String(error));
    }
  } else {
    __consolePrintHandle__('Test262:AsyncTestComplete');
  }
}


// Copyright (C) 2023 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-let-and-const-declarations-runtime-semantics-evaluation
description: Reads `[Symbol.dispose]` method if `[Symbol.asyncDispose]` is null
info: |
  RS: Evaluation
    AwaitUsingDeclaration : CoverAwaitExpressionAndAwaitUsingDeclarationHead BindingList ;

    1. Perform ? BindingEvaluation of BindingList with argument async-dispose.
    2. Return empty.

  RS: BindingEvaluation
    BindingList : BindingList , LexicalBinding

    1. Perform ? BindingEvaluation of BindingList with argument hint.
    2. Perform ? BindingEvaluation of LexicalBinding with argument hint.
    3. Return unused.

    LexicalBinding : BindingIdentifier Initializer

    ...
    5. Return ? InitializeReferencedBinding(lhs, value, hint).

  InitializeReferencedBinding ( V, W )

  ...
  4. Return ? base.InitializeBinding(V.[[ReferencedName]], W).

  InitializeBinding ( N, V, hint )

  ...
  2. If hint is not normal, perform ? AddDisposableResource(envRec.[[DisposeCapability]], V, hint).
  ...

  AddDisposableResource ( disposeCapability, V, hint [, method ] )

  1. If method is not present then,
    a. If V is either null or undefined and hint is sync-dispose, then
      i. Return unused
    b. Let resource be ? CreateDisposableResource(V, hint).
  ...

  CreateDisposableResource ( V, hint [ , method ] )

  1. If method is not present, then
    a. If V is either null or undefined, then
      i. Set V to undefined
      ii. Set method to undefined
    b. Else,
      i. If Type(V) is not Object, throw a TypeError exception.
      ii. Set method to ? GetDisposeMethod(V, hint).
      iii. If method is undefined, throw a TypeError exception.
  2. Else,
      a. ...
  3. Return the DisposableResource Record { [[ResourceValue]]: V, [[Hint]]: hint, [[DisposeMethod]]: method }.

  GetDisposeMethod ( V, hint )

  1. If hint is async-dispose, then
    a. Let method be ? GetMethod(V, @@asyncDispose).
    b. If method is undefined, then
      i. Set method to ? GetMethod(V, @@dispose).
  2. Else,
    a. Let method be ? GetMethod(V, @@dispose).
  3. Return method.

flags: [async]
includes: [asyncHelpers.js, deepEqual.js]
features: [explicit-resource-management]
---*/

asyncTest(async function () {
  var order = [];
  var resource = {
      get [Symbol.asyncDispose]() {
        order.push('Symbol.asyncDispose');
        return null;
      },
      get [Symbol.dispose]() {
          order.push('Symbol.dispose');
          return function() { };
      }
  };

  {
    await using _ = resource;
  }

  assert.deepEqual(order, ['Symbol.asyncDispose', 'Symbol.dispose'], 'Expected [Symbol.dispose] to have been read after [Symbol.asyncDispose] returns null');
});
