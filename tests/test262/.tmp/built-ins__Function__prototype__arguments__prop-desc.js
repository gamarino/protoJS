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


// Copyright (C) 2018 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    An Array of all representable Well-Known Intrinsic Objects
defines: [WellKnownIntrinsicObjects, getWellKnownIntrinsicObject]
---*/

const WellKnownIntrinsicObjects = [
  {
    name: '%AggregateError%',
    source: 'AggregateError',
  },
  {
    name: '%Array%',
    source: 'Array',
  },
  {
    name: '%ArrayBuffer%',
    source: 'ArrayBuffer',
  },
  {
    name: '%ArrayIteratorPrototype%',
    source: 'Object.getPrototypeOf([][Symbol.iterator]())',
  },
  {
    // Not currently accessible to ECMAScript user code
    name: '%AsyncFromSyncIteratorPrototype%',
    source: '',
  },
  {
    name: '%AsyncFunction%',
    source: '(async function() {}).constructor',
  },
  {
    name: '%AsyncGeneratorFunction%',
    source: '(async function* () {}).constructor',
  },
  {
    name: '%AsyncGeneratorPrototype%',
    source: 'Object.getPrototypeOf(async function* () {}).prototype',
  },
  {
    name: '%AsyncIteratorPrototype%',
    source: 'Object.getPrototypeOf(Object.getPrototypeOf(async function* () {}).prototype)',
  },
  {
    name: '%Atomics%',
    source: 'Atomics',
  },
  {
    name: '%BigInt%',
    source: 'BigInt',
  },
  {
    name: '%BigInt64Array%',
    source: 'BigInt64Array',
  },
  {
    name: '%BigUint64Array%',
    source: 'BigUint64Array',
  },
  {
    name: '%Boolean%',
    source: 'Boolean',
  },
  {
    name: '%DataView%',
    source: 'DataView',
  },
  {
    name: '%Date%',
    source: 'Date',
  },
  {
    name: '%decodeURI%',
    source: 'decodeURI',
  },
  {
    name: '%decodeURIComponent%',
    source: 'decodeURIComponent',
  },
  {
    name: '%encodeURI%',
    source: 'encodeURI',
  },
  {
    name: '%encodeURIComponent%',
    source: 'encodeURIComponent',
  },
  {
    name: '%Error%',
    source: 'Error',
  },
  {
    name: '%eval%',
    source: 'eval',
  },
  {
    name: '%EvalError%',
    source: 'EvalError',
  },
  {
    name: '%FinalizationRegistry%',
    source: 'FinalizationRegistry',
  },
  {
    name: '%Float32Array%',
    source: 'Float32Array',
  },
  {
    name: '%Float64Array%',
    source: 'Float64Array',
  },
  {
    // Not currently accessible to ECMAScript user code
    name: '%ForInIteratorPrototype%',
    source: '',
  },
  {
    name: '%Function%',
    source: 'Function',
  },
  {
    name: '%GeneratorFunction%',
    source: '(function* () {}).constructor',
  },
  {
    name: '%GeneratorPrototype%',
    source: 'Object.getPrototypeOf(function * () {}).prototype',
  },
  {
    name: '%Int8Array%',
    source: 'Int8Array',
  },
  {
    name: '%Int16Array%',
    source: 'Int16Array',
  },
  {
    name: '%Int32Array%',
    source: 'Int32Array',
  },
  {
    name: '%isFinite%',
    source: 'isFinite',
  },
  {
    name: '%isNaN%',
    source: 'isNaN',
  },
  {
    name: '%Iterator%',
    source: 'typeof Iterator !== "undefined" ? Iterator : Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())).constructor',
  },
  {
    name: '%IteratorHelperPrototype%',
    source: 'Object.getPrototypeOf(Iterator.from([]).drop(0))',
  },
  {
    name: '%JSON%',
    source: 'JSON',
  },
  {
    name: '%Map%',
    source: 'Map',
  },
  {
    name: '%MapIteratorPrototype%',
    source: 'Object.getPrototypeOf(new Map()[Symbol.iterator]())',
  },
  {
    name: '%Math%',
    source: 'Math',
  },
  {
    name: '%Number%',
    source: 'Number',
  },
  {
    name: '%Object%',
    source: 'Object',
  },
  {
    name: '%parseFloat%',
    source: 'parseFloat',
  },
  {
    name: '%parseInt%',
    source: 'parseInt',
  },
  {
    name: '%Promise%',
    source: 'Promise',
  },
  {
    name: '%Proxy%',
    source: 'Proxy',
  },
  {
    name: '%RangeError%',
    source: 'RangeError',
  },
  {
    name: '%ReferenceError%',
    source: 'ReferenceError',
  },
  {
    name: '%Reflect%',
    source: 'Reflect',
  },
  {
    name: '%RegExp%',
    source: 'RegExp',
  },
  {
    name: '%RegExpStringIteratorPrototype%',
    source: 'Object.getPrototypeOf(RegExp.prototype[Symbol.matchAll](""))',
  },
  {
    name: '%Set%',
    source: 'Set',
  },
  {
    name: '%SetIteratorPrototype%',
    source: 'Object.getPrototypeOf(new Set()[Symbol.iterator]())',
  },
  {
    name: '%SharedArrayBuffer%',
    source: 'SharedArrayBuffer',
  },
  {
    name: '%String%',
    source: 'String',
  },
  {
    name: '%StringIteratorPrototype%',
    source: 'Object.getPrototypeOf(new String()[Symbol.iterator]())',
  },
  {
    name: '%Symbol%',
    source: 'Symbol',
  },
  {
    name: '%SyntaxError%',
    source: 'SyntaxError',
  },
  {
    name: '%ThrowTypeError%',
    source: '(function() { "use strict"; return Object.getOwnPropertyDescriptor(arguments, "callee").get })()',
  },
  {
    name: '%TypedArray%',
    source: 'Object.getPrototypeOf(Uint8Array)',
  },
  {
    name: '%TypeError%',
    source: 'TypeError',
  },
  {
    name: '%Uint8Array%',
    source: 'Uint8Array',
  },
  {
    name: '%Uint8ClampedArray%',
    source: 'Uint8ClampedArray',
  },
  {
    name: '%Uint16Array%',
    source: 'Uint16Array',
  },
  {
    name: '%Uint32Array%',
    source: 'Uint32Array',
  },
  {
    name: '%URIError%',
    source: 'URIError',
  },
  {
    name: '%WeakMap%',
    source: 'WeakMap',
  },
  {
    name: '%WeakRef%',
    source: 'WeakRef',
  },
  {
    name: '%WeakSet%',
    source: 'WeakSet',
  },
  {
    name: '%WrapForValidIteratorPrototype%',
    source: 'Object.getPrototypeOf(Iterator.from({ [Symbol.iterator](){ return {}; } }))',
  },

  // Extensions to well-known intrinsic objects.
  //
  // https://tc39.es/ecma262/#sec-additional-properties-of-the-global-object
  {
    name: "%escape%",
    source: "escape",
  },
  {
    name: "%unescape%",
    source: "unescape",
  },

  // Extensions to well-known intrinsic objects.
  //
  // https://tc39.es/ecma402/#sec-402-well-known-intrinsic-objects
  {
    name: "%Intl%",
    source: "Intl",
  },
  {
    name: "%Intl.Collator%",
    source: "Intl.Collator",
  },
  {
    name: "%Intl.DateTimeFormat%",
    source: "Intl.DateTimeFormat",
  },
  {
    name: "%Intl.DisplayNames%",
    source: "Intl.DisplayNames",
  },
  {
    name: "%Intl.DurationFormat%",
    source: "Intl.DurationFormat",
  },
  {
    name: "%Intl.ListFormat%",
    source: "Intl.ListFormat",
  },
  {
    name: "%Intl.Locale%",
    source: "Intl.Locale",
  },
  {
    name: "%Intl.NumberFormat%",
    source: "Intl.NumberFormat",
  },
  {
    name: "%Intl.PluralRules%",
    source: "Intl.PluralRules",
  },
  {
    name: "%Intl.RelativeTimeFormat%",
    source: "Intl.RelativeTimeFormat",
  },
  {
    name: "%Intl.Segmenter%",
    source: "Intl.Segmenter",
  },
  {
    name: "%IntlSegmentIteratorPrototype%",
    source: "Object.getPrototypeOf(new Intl.Segmenter().segment()[Symbol.iterator]())",
  },
  {
    name: "%IntlSegmentsPrototype%",
    source: "Object.getPrototypeOf(new Intl.Segmenter().segment())",
  },

  // Extensions to well-known intrinsic objects.
  //
  // https://tc39.es/proposal-temporal/#sec-well-known-intrinsic-objects
  {
    name: "%Temporal%",
    source: "Temporal",
  },
];

WellKnownIntrinsicObjects.forEach((wkio) => {
  var actual;

  try {
    actual = new Function("return " + wkio.source)();
  } catch (exception) {
    // Nothing to do here.
  }

  wkio.value = actual;
});

/**
 * Returns a well-known intrinsic object, if the implementation provides it.
 * Otherwise, throws.
 * @param {string} key - the specification's name for the intrinsic, for example
 *   "%Array%"
 * @returns {object} the well-known intrinsic object.
 */
function getWellKnownIntrinsicObject(key) {
  for (var ix = 0; ix < WellKnownIntrinsicObjects.length; ix++) {
    if (WellKnownIntrinsicObjects[ix].name === key) {
      var value = WellKnownIntrinsicObjects[ix].value;
      if (value !== undefined)
        return value;
      throw new Test262Error('this implementation could not obtain ' + key);
    }
  }
  throw new Test262Error('unknown well-known intrinsic ' + key);
}


// Copyright (C) 2024 Justin Dorfman. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-addrestrictedfunctionproperties
description: >
  Function.prototype.arguments is an accessor property whose set and get
  functions are both %ThrowTypeError%.
info: |
  2. Let _thrower_ be _realm_.[[Intrinsics]].[[%ThrowTypeError%]].
  3. Perform ! DefinePropertyOrThrow(_F_, *"caller"*, PropertyDescriptor { [[Get]]: _thrower_, [[Set]]: _thrower_, [[Enumerable]]: *false*, [[Configurable]]: *true* }).
  4. Perform ! DefinePropertyOrThrow(_F_, *"arguments"*, PropertyDescriptor { [[Get]]: _thrower_, [[Set]]: _thrower_, [[Enumerable]]: *false*, [[Configurable]]: *true* }).
includes: [propertyHelper.js, wellKnownIntrinsicObjects.js]
---*/

const argumentsDesc = Object.getOwnPropertyDescriptor(Function.prototype, 'arguments');

verifyProperty(
  Function.prototype,
  "arguments",
  { enumerable: false, configurable: true },
  { restore: true }
);

assert.sameValue(typeof argumentsDesc.get, "function",
  "Function.prototype.arguments has function getter");
assert.sameValue(typeof argumentsDesc.set, "function",
  "Function.prototype.arguments has function setter");
assert.sameValue(argumentsDesc.get, argumentsDesc.set,
  "Function.prototype.arguments property getter/setter are the same function");

var throwTypeError;
WellKnownIntrinsicObjects.forEach(function(record) {
  if (record.name === "%ThrowTypeError%") {
    throwTypeError = record.value;
  }
});
if (throwTypeError) {
  assert.sameValue(argumentsDesc.set, throwTypeError, "Function.prototype.arguments getter is %ThrowTypeError%");
}
assert.throws(TypeError, function() {
  return Function.prototype.arguments;
});
assert.throws(TypeError, function() {
  Function.prototype.arguments = arguments;
});
