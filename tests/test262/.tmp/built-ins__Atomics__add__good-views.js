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


// Copyright (C) 2017 Mozilla Corporation. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to assert the correctness of SharedArrayBuffer objects.
defines:
  - testWithAtomicsOutOfBoundsIndices
  - testWithAtomicsInBoundsIndices
  - testWithAtomicsNonViewValues
---*/


/**
 * Calls the provided function for a each bad index that should throw a
 * RangeError when passed to an Atomics method on a SAB-backed view where
 * index 125 is out of range.
 *
 * @param f - the function to call for each bad index.
 */
function testWithAtomicsOutOfBoundsIndices(f) {
  var bad_indices = [
    function(view) { return -1; },
    function(view) { return view.length; },
    function(view) { return view.length * 2; },
    function(view) { return Number.POSITIVE_INFINITY; },
    function(view) { return Number.NEGATIVE_INFINITY; },
    function(view) { return { valueOf: function() { return 125; } }; },
    function(view) { return { toString: function() { return '125'; }, valueOf: false }; }, // non-callable valueOf triggers invocation of toString
  ];

  for (var i = 0; i < bad_indices.length; ++i) {
    var IdxGen = bad_indices[i];
    try {
      f(IdxGen);
    } catch (e) {
      e.message += ' (Testing with index gen ' + IdxGen + '.)';
      throw e;
    }
  }
}

/**
 * Calls the provided function for each good index that should not throw when
 * passed to an Atomics method on a SAB-backed view.
 *
 * The view must have length greater than zero.
 *
 * @param f - the function to call for each good index.
 */
function testWithAtomicsInBoundsIndices(f) {
  // Most of these are eventually coerced to +0 by ToIndex.
  var good_indices = [
    function(view) { return 0/-1; },
    function(view) { return '-0'; },
    function(view) { return undefined; },
    function(view) { return NaN; },
    function(view) { return 0.5; },
    function(view) { return '0.5'; },
    function(view) { return -0.9; },
    function(view) { return { password: 'qumquat' }; },
    function(view) { return view.length - 1; },
    function(view) { return { valueOf: function() { return 0; } }; },
    function(view) { return { toString: function() { return '0'; }, valueOf: false }; }, // non-callable valueOf triggers invocation of toString
  ];

  for (var i = 0; i < good_indices.length; ++i) {
    var IdxGen = good_indices[i];
    try {
      f(IdxGen);
    } catch (e) {
      e.message += ' (Testing with index gen ' + IdxGen + '.)';
      throw e;
    }
  }
}

/**
 * Calls the provided function for each value that should throw a TypeError
 * when passed to an Atomics method as a view.
 *
 * @param f - the function to call for each non-view value.
 */

function testWithAtomicsNonViewValues(f) {
  var values = [
    null,
    undefined,
    true,
    false,
    new Boolean(true),
    10,
    3.14,
    new Number(4),
    'Hi there',
    new Date,
    /a*utomaton/g,
    { password: 'qumquat' },
    new DataView(new ArrayBuffer(10)),
    new ArrayBuffer(128),
    new SharedArrayBuffer(128),
    new Error('Ouch'),
    [1,1,2,3,5,8],
    function(x) { return -x; },
    Symbol('halleluja'),
    // TODO: Proxy?
    Object,
    Int32Array,
    Date,
    Math,
    Atomics
  ];

  for (var i = 0; i < values.length; ++i) {
    var nonView = values[i];
    try {
      f(nonView);
    } catch (e) {
      e.message += ' (Testing with non-view value ' + nonView + '.)';
      throw e;
    }
  }
}



// Copyright (C) 2015 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to assert the correctness of TypedArray objects.
defines:
  - floatArrayConstructors
  - nonClampedIntArrayConstructors
  - intArrayConstructors
  - typedArrayConstructors
  - TypedArray
  - testWithAllTypedArrayConstructors
  - testWithTypedArrayConstructors
  - testWithBigIntTypedArrayConstructors
  - nonAtomicsFriendlyTypedArrayConstructors
  - testWithAtomicsFriendlyTypedArrayConstructors
  - testWithNonAtomicsFriendlyTypedArrayConstructors
  - testTypedArrayConversions
---*/

var floatArrayConstructors = [
  Float64Array,
  Float32Array
];

var nonClampedIntArrayConstructors = [
  Int32Array,
  Int16Array,
  Int8Array,
  Uint32Array,
  Uint16Array,
  Uint8Array
];

var intArrayConstructors = nonClampedIntArrayConstructors.concat([Uint8ClampedArray]);

// Float16Array is a newer feature
// adding it to this list unconditionally would cause implementations lacking it to fail every test which uses it
if (typeof Float16Array !== "undefined") {
  floatArrayConstructors.push(Float16Array);
}

var bigIntArrayConstructors = [];
if (typeof BigInt64Array !== "undefined") {
  bigIntArrayConstructors.push(BigInt64Array);
}
if (typeof BigUint64Array !== "undefined") {
  bigIntArrayConstructors.push(BigUint64Array);
}

/**
 * Array containing every non-bigint typed array constructor.
 */
var typedArrayConstructors = floatArrayConstructors.concat(intArrayConstructors);

/**
 * Array containing every typed array constructor, including those with bigint values.
 */
var allTypedArrayConstructors = typedArrayConstructors.concat(bigIntArrayConstructors);

/**
 * The %TypedArray% intrinsic constructor function.
 */
var TypedArray = Object.getPrototypeOf(Int8Array);

function isPrimitive(val) {
  return !val || (typeof val !== "object" && typeof val !== "function");
}

function makePassthrough(TA, primitiveOrIterable) {
  return primitiveOrIterable;
}

function makeArray(TA, primitiveOrIterable) {
  if (isPrimitive(primitiveOrIterable)) {
    var n = Number(primitiveOrIterable);
    // Only values between 0 and 2**53 - 1 inclusive can get mapped into TA contents.
    if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
    return Array.from({ length: n }, function() { return "0"; });
  }
  return Array.from(primitiveOrIterable);
}

function makeArrayLike(TA, primitiveOrIterable) {
  var arr = makeArray(TA, primitiveOrIterable);
  if (isPrimitive(arr)) return arr;
  var obj = { length: arr.length };
  for (var i = 0; i < obj.length; i++) obj[i] = arr[i];
  return obj;
}

var makeIterable;
if (typeof Symbol !== "undefined" && Symbol.iterator) {
  makeIterable = function makeIterable(TA, primitiveOrIterable) {
    var src = makeArray(TA, primitiveOrIterable);
    if (isPrimitive(src)) return src;
    var obj = {};
    obj[Symbol.iterator] = function() { return src[Symbol.iterator](); };
    return obj;
  };
}

function makeArrayBuffer(TA, primitiveOrIterable) {
  var arr = makeArray(TA, primitiveOrIterable);
  if (isPrimitive(arr)) return arr;
  return new TA(arr).buffer;
}

var makeResizableArrayBuffer, makeGrownArrayBuffer, makeShrunkArrayBuffer;
if (ArrayBuffer.prototype.resize) {
  var copyIntoArrayBuffer = function(destBuffer, srcBuffer) {
    var destView = new Uint8Array(destBuffer);
    var srcView = new Uint8Array(srcBuffer);
    for (var i = 0; i < srcView.length; i++) destView[i] = srcView[i];
    return destBuffer;
  };

  makeResizableArrayBuffer = function makeResizableArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      return new ArrayBuffer(n, { maxByteLength: n * 2 });
    }
    var fixed = makeArrayBuffer(TA, primitiveOrIterable);
    var byteLength = fixed.byteLength;
    var resizable = new ArrayBuffer(byteLength, { maxByteLength: byteLength * 2 });
    return copyIntoArrayBuffer(resizable, fixed);
  };

  makeGrownArrayBuffer = function makeGrownArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      var grown = new ArrayBuffer(Math.floor(n / 2), { maxByteLength: n });
      grown.resize(n);
    }
    var fixed = makeArrayBuffer(TA, primitiveOrIterable);
    var byteLength = fixed.byteLength;
    var grown = new ArrayBuffer(Math.floor(byteLength / 2), { maxByteLength: byteLength });
    grown.resize(byteLength);
    return copyIntoArrayBuffer(grown, fixed);
  };

  makeShrunkArrayBuffer = function makeShrunkArrayBuffer(TA, primitiveOrIterable) {
    if (isPrimitive(primitiveOrIterable)) {
      var n = Number(primitiveOrIterable) * TA.BYTES_PER_ELEMENT;
      if (!(n >= 0 && n < 9007199254740992)) return primitiveOrIterable;
      var shrunk = new ArrayBuffer(n * 2, { maxByteLength: n * 2 });
      shrunk.resize(n);
    }
    var fixed = makeArrayBuffer(TA, primitiveOrIterable);
    var byteLength = fixed.byteLength;
    var shrunk = new ArrayBuffer(byteLength * 2, { maxByteLength: byteLength * 2 });
    copyIntoArrayBuffer(shrunk, fixed);
    shrunk.resize(byteLength);
    return shrunk;
  };
}

var typedArrayCtorArgFactories = [makePassthrough, makeArray, makeArrayLike];
if (makeIterable) typedArrayCtorArgFactories.push(makeIterable);
typedArrayCtorArgFactories.push(makeArrayBuffer);
if (makeResizableArrayBuffer) typedArrayCtorArgFactories.push(makeResizableArrayBuffer);
if (makeGrownArrayBuffer) typedArrayCtorArgFactories.push(makeGrownArrayBuffer);
if (makeShrunkArrayBuffer) typedArrayCtorArgFactories.push(makeShrunkArrayBuffer);

/**
 * @typedef {"passthrough" | "arraylike" | "iterable" | "arraybuffer" | "resizable"} typedArrayArgFactoryFeature
 */

/**
 * A predicate for testing whether a TypedArray argument factory from this file
 * matches any of the provided features.
 *
 * @param {Function} argFactory
 * @param {typedArrayArgFactoryFeature[]} features
 * @returns {boolean}
 */
function ctorArgFactoryMatchesSome(argFactory, features) {
  for (var i = 0; i < features.length; ++i) {
    switch (features[i]) {
      case "passthrough":
        if (argFactory === makePassthrough) return true;
        break;
      case "arraylike":
        if (argFactory === makeArray || argFactory === makeArrayLike) return true;
        break;
      case "iterable":
        if (argFactory === makeIterable) return true;
        break;
      case "arraybuffer":
        if (
          argFactory === makeArrayBuffer ||
          argFactory === makeResizableArrayBuffer ||
          argFactory === makeGrownArrayBuffer ||
          argFactory === makeShrunkArrayBuffer
        ) {
          return true;
        }
        break;
      case "resizable":
        if (
          argFactory === makeResizableArrayBuffer ||
          argFactory === makeGrownArrayBuffer ||
          argFactory === makeShrunkArrayBuffer
        ) {
          return true;
        }
        break;
      default:
        throw Test262Error("unknown feature: " + features[i]);
    }
  }
  return false;
}

/**
 * Callback for testing a typed array constructor.
 *
 * @callback typedArrayConstructorCallback
 * @param {Function} TypedArrayConstructor the constructor object to test with
 * @param {Function} [TypedArrayConstructorArgFactory] a function for making
 *   a TypedArrayConstructor argument from a primitive (usually a number) or
 *   iterable (usually an array)
 */

/**
 * Calls the provided function with (typedArrayCtor, typedArrayCtorArgFactory)
 * pairs, where typedArrayCtor is Uint8Array/Int8Array/BigInt64Array/etc. and
 * typedArrayCtorArgFactory is a function for mapping a primitive (usually a
 * number) or iterable (usually an array) into a value suitable as the first
 * argument of typedArrayCtor (an Array, arraylike, iterable, or ArrayBuffer).
 *
 * @param {typedArrayConstructorCallback} f - the function to call
 * @param {Array} [constructors] - an explicit list of TypedArray constructors
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithAllTypedArrayConstructors(f, constructors, includeArgFactories, excludeArgFactories) {
  var ctors = constructors || allTypedArrayConstructors;
  var ctorArgFactories = typedArrayCtorArgFactories;
  if (includeArgFactories) {
    ctorArgFactories = [];
    for (var i = 0; i < typedArrayCtorArgFactories.length; ++i) {
      if (ctorArgFactoryMatchesSome(typedArrayCtorArgFactories[i], includeArgFactories)) {
        ctorArgFactories.push(typedArrayCtorArgFactories[i]);
      }
    }
  }
  if (excludeArgFactories) {
    ctorArgFactories = ctorArgFactories.slice();
    for (var i = ctorArgFactories.length - 1; i >= 0; --i) {
      if (ctorArgFactoryMatchesSome(ctorArgFactories[i], excludeArgFactories)) {
        ctorArgFactories.splice(i, 1);
      }
    }
  }
  if (ctorArgFactories.length === 0) {
    throw Test262Error("no arg factories match include " + includeArgFactories + " and exclude " + excludeArgFactories);
  }
  for (var k = 0; k < ctorArgFactories.length; ++k) {
    var argFactory = ctorArgFactories[k];
    for (var i = 0; i < ctors.length; ++i) {
      var constructor = ctors[i];
      var boundArgFactory = argFactory.bind(undefined, constructor);
      try {
        f(constructor, boundArgFactory);
      } catch (e) {
        e.message += " (Testing with " + constructor.name + " and " + argFactory.name + ".)";
        throw e;
      }
    }
  }
}

/**
 * Calls the provided function with (typedArrayCtor, typedArrayCtorArgFactory)
 * pairs, where typedArrayCtor is Uint8Array/Int8Array/BigInt64Array/etc. and
 * typedArrayCtorArgFactory is a function for mapping a primitive (usually a
 * number) or iterable (usually an array) into a value suitable as the first
 * argument of typedArrayCtor (an Array, arraylike, iterable, or ArrayBuffer).
 *
 * typedArrayCtor will not be BigInt64Array or BigUint64Array unless one or both
 * of those are explicitly provided.
 *
 * @param {typedArrayConstructorCallback} f - the function to call
 * @param {Array} [constructors] - an explicit list of TypedArray constructors
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithTypedArrayConstructors(f, constructors, includeArgFactories, excludeArgFactories) {
  var ctors = constructors || typedArrayConstructors;
  testWithAllTypedArrayConstructors(f, ctors, includeArgFactories, excludeArgFactories);
}

/**
 * Calls the provided function for every BigInt typed array constructor.
 *
 * @param {typedArrayConstructorCallback} f - the function to call
 * @param {Array} [constructors] - an explicit list of TypedArray constructors
 * @param {typedArrayArgFactoryFeature[]} [includeArgFactories] - for selecting
 *   initial constructor argument factory functions, rather than starting with
 *   all argument factories
 * @param {typedArrayArgFactoryFeature[]} [excludeArgFactories] - for excluding
 *   constructor argument factory functions, after an initial selection
 */
function testWithBigIntTypedArrayConstructors(f, constructors, includeArgFactories, excludeArgFactories) {
  var ctors = constructors || [BigInt64Array, BigUint64Array];
  testWithAllTypedArrayConstructors(f, ctors, includeArgFactories, excludeArgFactories);
}

var nonAtomicsFriendlyTypedArrayConstructors = floatArrayConstructors.concat([Uint8ClampedArray]);
/**
 * Calls the provided function for every non-"Atomics Friendly" typed array constructor.
 *
 * @param {typedArrayConstructorCallback} f - the function to call for each typed array constructor.
 * @param {Array} selected - An optional Array with filtered typed arrays
 */
function testWithNonAtomicsFriendlyTypedArrayConstructors(f) {
  testWithTypedArrayConstructors(f, nonAtomicsFriendlyTypedArrayConstructors);
}

/**
 * Calls the provided function for every "Atomics Friendly" typed array constructor.
 *
 * @param {typedArrayConstructorCallback} f - the function to call for each typed array constructor.
 * @param {Array} selected - An optional Array with filtered typed arrays
 */
function testWithAtomicsFriendlyTypedArrayConstructors(f) {
  testWithTypedArrayConstructors(f, [
    Int32Array,
    Int16Array,
    Int8Array,
    Uint32Array,
    Uint16Array,
    Uint8Array,
  ]);
}

/**
 * Helper for conversion operations on TypedArrays, the expected values
 * properties are indexed in order to match the respective value for each
 * TypedArray constructor
 * @param  {Function} fn - the function to call for each constructor and value.
 *                         will be called with the constructor, value, expected
 *                         value, and a initial value that can be used to avoid
 *                         a false positive with an equivalent expected value.
 */
function testTypedArrayConversions(byteConversionValues, fn) {
  var values = byteConversionValues.values;
  var expected = byteConversionValues.expected;

  testWithTypedArrayConstructors(function(TA) {
    var name = TA.name.slice(0, -5);

    return values.forEach(function(value, index) {
      var exp = expected[name][index];
      var initial = 0;
      if (exp === 0) {
        initial = 1;
      }
      fn(TA, value, exp, initial);
    });
  });
}

/**
 * Checks if the given argument is one of the float-based TypedArray constructors.
 *
 * @param {constructor} ctor - the value to check
 * @returns {boolean}
 */
function isFloatTypedArrayConstructor(arg) {
  return floatArrayConstructors.indexOf(arg) !== -1;
}

/**
 * Determines the precision of the given float-based TypedArray constructor.
 *
 * @param {constructor} ctor - the value to check
 * @returns {string} "half", "single", or "double" for Float16Array, Float32Array, and Float64Array respectively.
 */
function floatTypedArrayConstructorPrecision(FA) {
  if (typeof Float16Array !== "undefined" && FA === Float16Array) {
    return "half";
  } else if (FA === Float32Array) {
    return "single";
  } else if (FA === Float64Array) {
    return "double";
  } else {
    throw new Error("Malformed test - floatTypedArrayConstructorPrecision called with non-float TypedArray");
  }
}


// Copyright (C) 2017 Mozilla Corporation.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-atomics.add
description: Test Atomics.add on arrays that allow atomic operations.
includes: [testAtomics.js, testTypedArray.js]
features: [ArrayBuffer, Atomics, DataView, SharedArrayBuffer, Symbol, TypedArray]
---*/

const sab = new SharedArrayBuffer(1024);
const ab = new ArrayBuffer(16);
const views = nonClampedIntArrayConstructors.slice();

testWithTypedArrayConstructors(function(TA) {
  // Make it interesting - use non-zero byteOffsets and non-zero indexes.

  const view = new TA(sab, 32, 20);
  const control = new TA(ab, 0, 2);

  // Add positive number
  view[8] = 0;
  assert.sameValue(Atomics.add(view, 8, 10), 0, 'Atomics.add(view, 8, 10) returns 0');
  assert.sameValue(view[8], 10, 'The value of view[8] is 10');

  // Add negative number
  assert.sameValue(Atomics.add(view, 8, -5), 10, 'Atomics.add(view, 8, -5) returns 10');
  assert.sameValue(view[8], 5, 'The value of view[8] is 5');

  view[3] = -5;
  control[0] = -5;
  assert.sameValue(Atomics.add(view, 3, 0), control[0],
    'Atomics.add(view, 3, 0) returns the value of `control[0]` (-5)');

  control[0] = 12345;
  view[3] = 12345;
  assert.sameValue(Atomics.add(view, 3, 0), control[0],
    'Atomics.add(view, 3, 0) returns the value of `control[0]` (12345)');

  control[0] = 123456789;
  view[3] = 123456789;
  assert.sameValue(Atomics.add(view, 3, 0), control[0],
    'Atomics.add(view, 3, 0) returns the value of `control[0]` (123456789)');

  // In-bounds boundary cases for indexing
  testWithAtomicsInBoundsIndices(function(IdxGen) {
    let Idx = IdxGen(view);
    view.fill(0);
    // Atomics.store() computes an index from Idx in the same way as other
    // Atomics operations, not quite like view[Idx].
    Atomics.store(view, Idx, 37);
    assert.sameValue(Atomics.add(view, Idx, 0), 37, 'Atomics.add(view, Idx, 0) returns 37');
  });
}, views);
