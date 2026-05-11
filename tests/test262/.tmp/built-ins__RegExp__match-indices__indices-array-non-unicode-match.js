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


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to safely verify the correctness of
    property descriptors.
defines:
  - verifyProperty
  - verifyCallableProperty
  - verifyEqualTo # deprecated
  - verifyWritable # deprecated
  - verifyNotWritable # deprecated
  - verifyEnumerable # deprecated
  - verifyNotEnumerable # deprecated
  - verifyConfigurable # deprecated
  - verifyNotConfigurable # deprecated
  - verifyPrimordialProperty
  - verifyPrimordialCallableProperty
---*/

// @ts-check

// Capture primordial functions and receiver-uncurried primordial methods that
// are used in verification but might be destroyed *by* that process itself.
var __isArray = Array.isArray;
var __defineProperty = Object.defineProperty;
var __getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
var __getOwnPropertyNames = Object.getOwnPropertyNames;
var __join = Function.prototype.call.bind(Array.prototype.join);
var __push = Function.prototype.call.bind(Array.prototype.push);
var __hasOwnProperty = Function.prototype.call.bind(Object.prototype.hasOwnProperty);
var __propertyIsEnumerable = Function.prototype.call.bind(Object.prototype.propertyIsEnumerable);
var nonIndexNumericPropertyName = Math.pow(2, 32) - 1;

/**
 * @param {object} obj
 * @param {string|symbol} name
 * @param {PropertyDescriptor|undefined} desc
 * @param {object} [options]
 * @param {boolean} [options.restore] revert mutations from verifying writable/configurable
 */
function verifyProperty(obj, name, desc, options) {
  assert(
    arguments.length > 2,
    'verifyProperty should receive at least 3 arguments: obj, name, and descriptor'
  );

  var originalDesc = __getOwnPropertyDescriptor(obj, name);
  var nameStr = String(name);

  // Allows checking for undefined descriptor if it's explicitly given.
  if (desc === undefined) {
    assert.sameValue(
      originalDesc,
      undefined,
      "obj['" + nameStr + "'] descriptor should be undefined"
    );

    // desc and originalDesc are both undefined, problem solved;
    return true;
  }

  assert(
    __hasOwnProperty(obj, name),
    "obj should have an own property " + nameStr
  );

  assert.notSameValue(
    desc,
    null,
    "The desc argument should be an object or undefined, null"
  );

  assert.sameValue(
    typeof desc,
    "object",
    "The desc argument should be an object or undefined, " + String(desc)
  );

  var names = __getOwnPropertyNames(desc);
  for (var i = 0; i < names.length; i++) {
    assert(
      names[i] === "value" ||
        names[i] === "writable" ||
        names[i] === "enumerable" ||
        names[i] === "configurable" ||
        names[i] === "get" ||
        names[i] === "set",
      "Invalid descriptor field: " + names[i],
    );
  }

  var failures = [];

  if (__hasOwnProperty(desc, 'value')) {
    if (!isSameValue(desc.value, originalDesc.value)) {
      __push(failures, "obj['" + nameStr + "'] descriptor value should be " + desc.value);
    }
    if (!isSameValue(desc.value, obj[name])) {
      __push(failures, "obj['" + nameStr + "'] value should be " + desc.value);
    }
  }

  if (__hasOwnProperty(desc, 'enumerable') && desc.enumerable !== undefined) {
    if (desc.enumerable !== originalDesc.enumerable ||
        desc.enumerable !== isEnumerable(obj, name)) {
      __push(failures, "obj['" + nameStr + "'] descriptor should " + (desc.enumerable ? '' : 'not ') + "be enumerable");
    }
  }

  // Operations past this point are potentially destructive!

  if (__hasOwnProperty(desc, 'writable') && desc.writable !== undefined) {
    if (desc.writable !== originalDesc.writable ||
        desc.writable !== isWritable(obj, name)) {
      __push(failures, "obj['" + nameStr + "'] descriptor should " + (desc.writable ? '' : 'not ') + "be writable");
    }
  }

  if (__hasOwnProperty(desc, 'configurable') && desc.configurable !== undefined) {
    if (desc.configurable !== originalDesc.configurable ||
        desc.configurable !== isConfigurable(obj, name)) {
      __push(failures, "obj['" + nameStr + "'] descriptor should " + (desc.configurable ? '' : 'not ') + "be configurable");
    }
  }

  if (failures.length) {
    assert(false, __join(failures, '; '));
  }

  if (options && options.restore) {
    __defineProperty(obj, name, originalDesc);
  }

  return true;
}

function isConfigurable(obj, name) {
  try {
    delete obj[name];
  } catch (e) {
    if (!(e instanceof TypeError)) {
      throw new Test262Error("Expected TypeError, got " + e);
    }
  }
  return !__hasOwnProperty(obj, name);
}

function isEnumerable(obj, name) {
  var stringCheck = false;

  if (typeof name === "string") {
    for (var x in obj) {
      if (x === name) {
        stringCheck = true;
        break;
      }
    }
  } else {
    // skip it if name is not string, works for Symbol names.
    stringCheck = true;
  }

  return stringCheck && __hasOwnProperty(obj, name) && __propertyIsEnumerable(obj, name);
}

function isSameValue(a, b) {
  if (a === 0 && b === 0) return 1 / a === 1 / b;
  if (a !== a && b !== b) return true;

  return a === b;
}

function isWritable(obj, name, verifyProp, value) {
  var unlikelyValue = __isArray(obj) && name === "length" ?
    nonIndexNumericPropertyName :
    "unlikelyValue";
  var newValue = value || unlikelyValue;
  var hadValue = __hasOwnProperty(obj, name);
  var oldValue = obj[name];
  var writeSucceeded;

  if (arguments.length < 4 && newValue === oldValue) {
    newValue = newValue + "2";
  }

  try {
    obj[name] = newValue;
  } catch (e) {
    if (!(e instanceof TypeError)) {
      throw new Test262Error("Expected TypeError, got " + e);
    }
  }

  writeSucceeded = isSameValue(obj[verifyProp || name], newValue);

  // Revert the change only if it was successful (in other cases, reverting
  // is unnecessary and may trigger exceptions for certain property
  // configurations)
  if (writeSucceeded) {
    if (hadValue) {
      obj[name] = oldValue;
    } else {
      delete obj[name];
    }
  }

  return writeSucceeded;
}

/**
 * Verify that there is a function of specified name, length, and containing
 * descriptor associated with `obj[name]` and following the conventions for
 * built-in objects.
 *
 * @param {object} obj
 * @param {string|symbol} name
 * @param {string} [functionName] defaults to name for strings, `[${name.description}]` for symbols
 * @param {number} functionLength
 * @param {PropertyDescriptor} [desc] defaults to data property conventions (writable, non-enumerable, configurable)
 * @param {object} [options]
 * @param {boolean} [options.restore] revert mutations from verifying writable/configurable
 */
function verifyCallableProperty(obj, name, functionName, functionLength, desc, options) {
  var value = obj[name];

  assert.sameValue(typeof value, "function",
    "obj['" + String(name) + "'] descriptor should be a function");

  // Every other data property described in clauses 19 through 28 and in
  // Annex B.2 has the attributes { [[Writable]]: true, [[Enumerable]]: false,
  // [[Configurable]]: true } unless otherwise specified.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html
  if (desc === undefined) {
    desc = {
      writable: true,
      enumerable: false,
      configurable: true,
      value: value
    };
  } else if (!__hasOwnProperty(desc, "value") && !__hasOwnProperty(desc, "get")) {
    desc.value = value;
  }

  verifyProperty(obj, name, desc, options);

  if (functionName === undefined) {
    if (typeof name === "symbol") {
      functionName = "[" + name.description + "]";
    } else {
      functionName = name;
    }
  }
  // Unless otherwise specified, the "name" property of a built-in function
  // object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
  // [[Configurable]]: true }.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html#sec-ecmascript-standard-built-in-objects
  // https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-setfunctionname
  verifyProperty(value, "name", {
    value: functionName,
    writable: false,
    enumerable: false,
    configurable: desc.configurable
  }, options);

  // Unless otherwise specified, the "length" property of a built-in function
  // object has the attributes { [[Writable]]: false, [[Enumerable]]: false,
  // [[Configurable]]: true }.
  // https://tc39.es/ecma262/multipage/ecmascript-standard-built-in-objects.html#sec-ecmascript-standard-built-in-objects
  // https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-setfunctionlength
  verifyProperty(value, "length", {
    value: functionLength,
    writable: false,
    enumerable: false,
    configurable: desc.configurable
  }, options);
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyEqualTo(obj, name, value) {
  if (!isSameValue(obj[name], value)) {
    throw new Test262Error("Expected obj[" + String(name) + "] to equal " + value +
           ", actually " + obj[name]);
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyWritable(obj, name, verifyProp, value) {
  if (!verifyProp) {
    assert(__getOwnPropertyDescriptor(obj, name).writable,
         "Expected obj[" + String(name) + "] to have writable:true.");
  }
  if (!isWritable(obj, name, verifyProp, value)) {
    throw new Test262Error("Expected obj[" + String(name) + "] to be writable, but was not.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyNotWritable(obj, name, verifyProp, value) {
  if (!verifyProp) {
    assert(!__getOwnPropertyDescriptor(obj, name).writable,
         "Expected obj[" + String(name) + "] to have writable:false.");
  }
  if (isWritable(obj, name, verifyProp)) {
    throw new Test262Error("Expected obj[" + String(name) + "] NOT to be writable, but was.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyEnumerable(obj, name) {
  assert(__getOwnPropertyDescriptor(obj, name).enumerable,
       "Expected obj[" + String(name) + "] to have enumerable:true.");
  if (!isEnumerable(obj, name)) {
    throw new Test262Error("Expected obj[" + String(name) + "] to be enumerable, but was not.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyNotEnumerable(obj, name) {
  assert(!__getOwnPropertyDescriptor(obj, name).enumerable,
       "Expected obj[" + String(name) + "] to have enumerable:false.");
  if (isEnumerable(obj, name)) {
    throw new Test262Error("Expected obj[" + String(name) + "] NOT to be enumerable, but was.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyConfigurable(obj, name) {
  assert(__getOwnPropertyDescriptor(obj, name).configurable,
       "Expected obj[" + String(name) + "] to have configurable:true.");
  if (!isConfigurable(obj, name)) {
    throw new Test262Error("Expected obj[" + String(name) + "] to be configurable, but was not.");
  }
}

/**
 * Deprecated; please use `verifyProperty` in new tests.
 */
function verifyNotConfigurable(obj, name) {
  assert(!__getOwnPropertyDescriptor(obj, name).configurable,
       "Expected obj[" + String(name) + "] to have configurable:false.");
  if (isConfigurable(obj, name)) {
    throw new Test262Error("Expected obj[" + String(name) + "] NOT to be configurable, but was.");
  }
}

/**
 * Use this function to verify the properties of a primordial object.
 * For non-primordial objects, use verifyProperty.
 * See: https://github.com/tc39/how-we-work/blob/main/terminology.md#primordial
 */
var verifyPrimordialProperty = verifyProperty;

/**
 * Use this function to verify the primordial function-valued properties.
 * For non-primordial functions, use verifyCallableProperty.
 * See: https://github.com/tc39/how-we-work/blob/main/terminology.md#primordial
 */
var verifyPrimordialCallableProperty = verifyCallableProperty;


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


// Copyright 2019 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Basic matching cases with non-unicode matches.
includes: [compareArray.js, propertyHelper.js, deepEqual.js]
esid: sec-regexpbuiltinexec
features: [regexp-named-groups, regexp-match-indices]
info: |
  Runtime Semantics: RegExpBuiltinExec ( R, S )
    ...
    4. Let _lastIndex_ be ? ToLength(? Get(_R_, `"lastIndex")).
    ...
    8. If _flags_ contains `"d"`, let _hasIndices_ be *true*, else let _hasIndices_ be *false*.
    ...
    26. Let _match_ be the Match { [[StartIndex]]: _lastIndex_, [[EndIndex]]: _e_ }.
    27. Let _indices_ be a new empty List.
    29. Add _match_ as the last element of _indices_.
    ...
    35. For each integer _i_ such that _i_ > 0 and _i_ <= _n_, in ascending order, do
      ...
      f. Else,
        i. Let _captureStart_ be _captureI_'s _startIndex_.
        ii. Let _captureEnd_ be _captureI_'s _endIndex_.
        ...
        iv. Let _capture_ be the Match  { [[StartIndex]]: _captureStart_, [[EndIndex]]: _captureEnd_ }.
        v. Append _capture_ to _indices_.
        ...
    36. If _hasIndices_ is *true*, then
      a. Let _indicesArray_ be MakeIndicesArray( _S_, _indices_, _groupNames_).
      b. Perform ! CreateDataProperty(_A_, `"indices"`, _indicesArray_).
---*/

assert.deepEqual([[1, 2], [1, 2]], "bab".match(/(a)/d).indices);
assert.deepEqual([[0, 3], [1, 2]], "bab".match(/.(a)./d).indices);
assert.deepEqual([[0, 3], [1, 2], [2, 3]], "bab".match(/.(a)(.)/d).indices);
assert.deepEqual([[0, 3], [1, 3]], "bab".match(/.(\w\w)/d).indices);
assert.deepEqual([[0, 3], [0, 3]], "bab".match(/(\w\w\w)/d).indices);
assert.deepEqual([[0, 3], [0, 2], [2, 3]], "bab".match(/(\w\w)(\w)/d).indices);
assert.deepEqual([[0, 2], [0, 2], undefined], "bab".match(/(\w\w)(\W)?/d).indices);

let groups = /(?<a>.)(?<b>.)(?<c>.)\k<c>\k<b>\k<a>/d.exec("abccba").indices.groups;
assert.compareArray([0, 1], groups.a);
assert.compareArray([1, 2], groups.b);
assert.compareArray([2, 3], groups.c);
verifyProperty(groups, "a", {
    enumerable: true,
    writable: true,
    configurable: true
});
verifyProperty(groups, "b", {
    enumerable: true,
    writable: true,
    configurable: true
});
verifyProperty(groups, "c", {
    enumerable: true,
    writable: true,
    configurable: true
});

// "𝐁" is U+1d401 MATHEMATICAL BOLD CAPITAL B
// - Also representable as the code point "\u{1d401}"
// - Also representable as the surrogate pair "\uD835\uDC01"

// Verify assumptions:
assert.sameValue("𝐁".length, 2, 'The length of "𝐁" is 2');
assert.sameValue("\u{1d401}".length, 2, 'The length of "\\u{1d401}" is 2');
assert.sameValue("\uD835\uDC01".length, 2, 'The length of "\\uD835\\uDC01" is 2');
assert.sameValue("𝐁".match(/./)[0].length, 1, 'The length of a single code unit match against "𝐁" is 1 (without /u flag)');
assert.sameValue("\u{1d401}".match(/./)[0].length, 1, 'The length of a single code unit match against "\\u{1d401}" is 1 (without /u flag)');
assert.sameValue("\uD835\uDC01".match(/./)[0].length, 1, 'The length of a single code unit match against "\\ud835\\udc01" is 1 (without /u flag)');

assert.compareArray([0, 1], "𝐁".match(/./d).indices[0], 'Indices for non-unicode match against "𝐁" (without /u flag)');
assert.compareArray([0, 1], "\u{1d401}".match(/./d).indices[0], 'Indices for non-unicode match against "\\u{1d401}" (without /u flag)');
assert.compareArray([0, 1], "\uD835\uDC01".match(/./d).indices[0], 'Indices for non-unicode match against "\\ud835\\udc01" (without /u flag)');
assert.compareArray([0, 1], "𝐁".match(/(?<a>.)/d).indices.groups.a, 'Indices for non-unicode match against "𝐁" in groups.a (without /u flag)');
assert.compareArray([0, 1], "\u{1d401}".match(/(?<a>.)/d).indices.groups.a, 'Indices for non-unicode match against "\\u{1d401}" in groups.a (without /u flag)');
assert.compareArray([0, 1], "\uD835\uDC01".match(/(?<a>.)/d).indices.groups.a, 'Indices for non-unicode match against "\\ud835\\udc01" in groups.a (without /u flag)');
