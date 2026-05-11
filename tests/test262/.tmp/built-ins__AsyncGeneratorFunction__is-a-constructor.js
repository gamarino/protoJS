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


// Copyright (C) 2017 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: |
    Test if a given function is a constructor function.
defines: [isConstructor]
features: [Reflect.construct]
---*/

function isConstructor(f) {
    if (typeof f !== "function") {
      throw new Test262Error("isConstructor invoked with a non-function value");
    }

    try {
        Reflect.construct(function(){}, [], f);
    } catch (e) {
        return false;
    }
    return true;
}


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


// Copyright (C) 2020 Rick Waldron. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-ecmascript-standard-built-in-objects
description: >
  The AsyncGeneratorFunction constructor implements [[Construct]]
info: |
  IsConstructor ( argument )

  The abstract operation IsConstructor takes argument argument (an ECMAScript language value).
  It determines if argument is a function object with a [[Construct]] internal method.
  It performs the following steps when called:

  If Type(argument) is not Object, return false.
  If argument has a [[Construct]] internal method, return true.
  Return false.
includes: [isConstructor.js, wellKnownIntrinsicObjects.js]
features: [Reflect.construct]
---*/

var AsyncGeneratorFunction = getWellKnownIntrinsicObject('%AsyncGeneratorFunction%');
assert.sameValue(
  isConstructor(AsyncGeneratorFunction),
  true,
  'isConstructor(AsyncGeneratorFunction) must return true'
);
new AsyncGeneratorFunction();
