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


// Copyright (C) 2016 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Provide a list for original and expected values for different byte
    conversions.
    This helper is mostly used on tests for TypedArray and DataView, and each
    array from the expected values must match the original values array on every
    index containing its original value.
defines: [byteConversionValues]
---*/
var byteConversionValues = {
  values: [
    127,         // 2 ** 7 - 1
    128,         // 2 ** 7
    32767,       // 2 ** 15 - 1
    32768,       // 2 ** 15
    2147483647,  // 2 ** 31 - 1
    2147483648,  // 2 ** 31
    255,         // 2 ** 8 - 1
    256,         // 2 ** 8
    65535,       // 2 ** 16 - 1
    65536,       // 2 ** 16
    4294967295,  // 2 ** 32 - 1
    4294967296,  // 2 ** 32
    9007199254740991, // 2 ** 53 - 1
    9007199254740992, // 2 ** 53
    1.1,
    0.1,
    0.5,
    0.50000001,
    0.6,
    0.7,
    undefined,
    -1,
    -0,
    -0.1,
    -1.1,
    NaN,
    -127,        // - ( 2 ** 7 - 1 )
    -128,        // - ( 2 ** 7 )
    -32767,      // - ( 2 ** 15 - 1 )
    -32768,      // - ( 2 ** 15 )
    -2147483647, // - ( 2 ** 31 - 1 )
    -2147483648, // - ( 2 ** 31 )
    -255,        // - ( 2 ** 8 - 1 )
    -256,        // - ( 2 ** 8 )
    -65535,      // - ( 2 ** 16 - 1 )
    -65536,      // - ( 2 ** 16 )
    -4294967295, // - ( 2 ** 32 - 1 )
    -4294967296, // - ( 2 ** 32 )
    Infinity,
    -Infinity,
    0,
    2049,                         // an integer which rounds down under ties-to-even when cast to float16
    2051,                         // an integer which rounds up under ties-to-even when cast to float16
    0.00006103515625,             // smallest normal float16
    0.00006097555160522461,       // largest subnormal float16
    5.960464477539063e-8,         // smallest float16
    2.9802322387695312e-8,        // largest double which rounds to 0 when cast to float16
    2.980232238769532e-8,         // smallest double which does not round to 0 when cast to float16
    8.940696716308594e-8,         // a double which rounds up to a subnormal under ties-to-even when cast to float16
    1.4901161193847656e-7,        // a double which rounds down to a subnormal under ties-to-even when cast to float16
    1.490116119384766e-7,         // the next double above the one on the previous line one
    65504,                        // max finite float16
    65520,                        // smallest double which rounds to infinity when cast to float16
    65519.99999999999,            // largest double which does not round to infinity when cast to float16
    0.000061005353927612305,      // smallest double which rounds to a non-subnormal when cast to float16
    0.0000610053539276123         // largest double which rounds to a subnormal when cast to float16
  ],

  expected: {
    Int8: [
      127,  // 127
      -128, // 128
      -1,   // 32767
      0,    // 32768
      -1,   // 2147483647
      0,    // 2147483648
      -1,   // 255
      0,    // 256
      -1,   // 65535
      0,    // 65536
      -1,   // 4294967295
      0,    // 4294967296
      -1,   // 9007199254740991
      0,    // 9007199254740992
      1,    // 1.1
      0,    // 0.1
      0,    // 0.5
      0,    // 0.50000001,
      0,    // 0.6
      0,    // 0.7
      0,    // undefined
      -1,   // -1
      0,    // -0
      0,    // -0.1
      -1,   // -1.1
      0,    // NaN
      -127, // -127
      -128, // -128
      1,    // -32767
      0,    // -32768
      1,    // -2147483647
      0,    // -2147483648
      1,    // -255
      0,    // -256
      1,    // -65535
      0,    // -65536
      1,    // -4294967295
      0,    // -4294967296
      0,    // Infinity
      0,    // -Infinity
      0,    // 0
      1,    // 2049
      3,    // 2051
      0,    // 0.00006103515625
      0,    // 0.00006097555160522461
      0,    // 5.960464477539063e-8
      0,    // 2.9802322387695312e-8
      0,    // 2.980232238769532e-8
      0,    // 8.940696716308594e-8
      0,    // 1.4901161193847656e-7
      0,    // 1.490116119384766e-7
      -32,  // 65504
      -16,  // 65520
      -17,  // 65519.99999999999
      0,    // 0.000061005353927612305
      0     // 0.0000610053539276123
    ],
    Uint8: [
      127, // 127
      128, // 128
      255, // 32767
      0,   // 32768
      255, // 2147483647
      0,   // 2147483648
      255, // 255
      0,   // 256
      255, // 65535
      0,   // 65536
      255, // 4294967295
      0,   // 4294967296
      255, // 9007199254740991
      0,   // 9007199254740992
      1,   // 1.1
      0,   // 0.1
      0,   // 0.5
      0,   // 0.50000001,
      0,   // 0.6
      0,   // 0.7
      0,   // undefined
      255, // -1
      0,   // -0
      0,   // -0.1
      255, // -1.1
      0,   // NaN
      129, // -127
      128, // -128
      1,   // -32767
      0,   // -32768
      1,   // -2147483647
      0,   // -2147483648
      1,   // -255
      0,   // -256
      1,   // -65535
      0,   // -65536
      1,   // -4294967295
      0,   // -4294967296
      0,   // Infinity
      0,   // -Infinity
      0,   // 0
      1,   // 2049
      3,   // 2051
      0,   // 0.00006103515625
      0,   // 0.00006097555160522461
      0,   // 5.960464477539063e-8
      0,   // 2.9802322387695312e-8
      0,   // 2.980232238769532e-8
      0,   // 8.940696716308594e-8
      0,   // 1.4901161193847656e-7
      0,   // 1.490116119384766e-7
      224, // 65504
      240, // 65520
      239, // 65519.99999999999
      0,   // 0.000061005353927612305
      0    // 0.0000610053539276123
    ],
    Uint8Clamped: [
      127, // 127
      128, // 128
      255, // 32767
      255, // 32768
      255, // 2147483647
      255, // 2147483648
      255, // 255
      255, // 256
      255, // 65535
      255, // 65536
      255, // 4294967295
      255, // 4294967296
      255, // 9007199254740991
      255, // 9007199254740992
      1,   // 1.1,
      0,   // 0.1
      0,   // 0.5
      1,   // 0.50000001,
      1,   // 0.6
      1,   // 0.7
      0,   // undefined
      0,   // -1
      0,   // -0
      0,   // -0.1
      0,   // -1.1
      0,   // NaN
      0,   // -127
      0,   // -128
      0,   // -32767
      0,   // -32768
      0,   // -2147483647
      0,   // -2147483648
      0,   // -255
      0,   // -256
      0,   // -65535
      0,   // -65536
      0,   // -4294967295
      0,   // -4294967296
      255, // Infinity
      0,   // -Infinity
      0,   // 0
      255, // 2049
      255, // 2051
      0,   // 0.00006103515625
      0,   // 0.00006097555160522461
      0,   // 5.960464477539063e-8
      0,   // 2.9802322387695312e-8
      0,   // 2.980232238769532e-8
      0,   // 8.940696716308594e-8
      0,   // 1.4901161193847656e-7
      0,   // 1.490116119384766e-7
      255, // 65504
      255, // 65520
      255, // 65519.99999999999
      0,   // 0.000061005353927612305
      0    // 0.0000610053539276123
    ],
    Int16: [
      127,    // 127
      128,    // 128
      32767,  // 32767
      -32768, // 32768
      -1,     // 2147483647
      0,      // 2147483648
      255,    // 255
      256,    // 256
      -1,     // 65535
      0,      // 65536
      -1,     // 4294967295
      0,      // 4294967296
      -1,     // 9007199254740991
      0,      // 9007199254740992
      1,      // 1.1
      0,      // 0.1
      0,      // 0.5
      0,      // 0.50000001,
      0,      // 0.6
      0,      // 0.7
      0,      // undefined
      -1,     // -1
      0,      // -0
      0,      // -0.1
      -1,     // -1.1
      0,      // NaN
      -127,   // -127
      -128,   // -128
      -32767, // -32767
      -32768, // -32768
      1,      // -2147483647
      0,      // -2147483648
      -255,   // -255
      -256,   // -256
      1,      // -65535
      0,      // -65536
      1,      // -4294967295
      0,      // -4294967296
      0,      // Infinity
      0,      // -Infinity
      0,      // 0
      2049,   // 2049
      2051,   // 2051
      0,      // 0.00006103515625
      0,      // 0.00006097555160522461
      0,      // 5.960464477539063e-8
      0,      // 2.9802322387695312e-8
      0,      // 2.980232238769532e-8
      0,      // 8.940696716308594e-8
      0,      // 1.4901161193847656e-7
      0,      // 1.490116119384766e-7
      -32,    // 65504
      -16,    // 65520
      -17,    // 65519.99999999999
      0,      // 0.000061005353927612305
      0       // 0.0000610053539276123
    ],
    Uint16: [
      127,   // 127
      128,   // 128
      32767, // 32767
      32768, // 32768
      65535, // 2147483647
      0,     // 2147483648
      255,   // 255
      256,   // 256
      65535, // 65535
      0,     // 65536
      65535, // 4294967295
      0,     // 4294967296
      65535, // 9007199254740991
      0,     // 9007199254740992
      1,     // 1.1
      0,     // 0.1
      0,     // 0.5
      0,     // 0.50000001,
      0,     // 0.6
      0,     // 0.7
      0,     // undefined
      65535, // -1
      0,     // -0
      0,     // -0.1
      65535, // -1.1
      0,     // NaN
      65409, // -127
      65408, // -128
      32769, // -32767
      32768, // -32768
      1,     // -2147483647
      0,     // -2147483648
      65281, // -255
      65280, // -256
      1,     // -65535
      0,     // -65536
      1,     // -4294967295
      0,     // -4294967296
      0,     // Infinity
      0,     // -Infinity
      0,     // 0
      2049,  // 2049
      2051,  // 2051
      0,     // 0.00006103515625
      0,     // 0.00006097555160522461
      0,     // 5.960464477539063e-8
      0,     // 2.9802322387695312e-8
      0,     // 2.980232238769532e-8
      0,     // 8.940696716308594e-8
      0,     // 1.4901161193847656e-7
      0,     // 1.490116119384766e-7
      65504, // 65504
      65520, // 65520
      65519, // 65519.99999999999
      0,     // 0.000061005353927612305
      0      // 0.0000610053539276123
    ],
    Int32: [
      127,         // 127
      128,         // 128
      32767,       // 32767
      32768,       // 32768
      2147483647,  // 2147483647
      -2147483648, // 2147483648
      255,         // 255
      256,         // 256
      65535,       // 65535
      65536,       // 65536
      -1,          // 4294967295
      0,           // 4294967296
      -1,          // 9007199254740991
      0,           // 9007199254740992
      1,           // 1.1
      0,           // 0.1
      0,           // 0.5
      0,           // 0.50000001,
      0,           // 0.6
      0,           // 0.7
      0,           // undefined
      -1,          // -1
      0,           // -0
      0,           // -0.1
      -1,          // -1.1
      0,           // NaN
      -127,        // -127
      -128,        // -128
      -32767,      // -32767
      -32768,      // -32768
      -2147483647, // -2147483647
      -2147483648, // -2147483648
      -255,        // -255
      -256,        // -256
      -65535,      // -65535
      -65536,      // -65536
      1,           // -4294967295
      0,           // -4294967296
      0,           // Infinity
      0,           // -Infinity
      0,           // 0
      2049,        // 2049
      2051,        // 2051
      0,           // 0.00006103515625
      0,           // 0.00006097555160522461
      0,           // 5.960464477539063e-8
      0,           // 2.9802322387695312e-8
      0,           // 2.980232238769532e-8
      0,           // 8.940696716308594e-8
      0,           // 1.4901161193847656e-7
      0,           // 1.490116119384766e-7
      65504,       // 65504
      65520,       // 65520
      65519,       // 65519.99999999999
      0,           // 0.000061005353927612305
      0            // 0.0000610053539276123
    ],
    Uint32: [
      127,        // 127
      128,        // 128
      32767,      // 32767
      32768,      // 32768
      2147483647, // 2147483647
      2147483648, // 2147483648
      255,        // 255
      256,        // 256
      65535,      // 65535
      65536,      // 65536
      4294967295, // 4294967295
      0,          // 4294967296
      4294967295, // 9007199254740991
      0,          // 9007199254740992
      1,          // 1.1
      0,          // 0.1
      0,          // 0.5
      0,          // 0.50000001,
      0,          // 0.6
      0,          // 0.7
      0,          // undefined
      4294967295, // -1
      0,          // -0
      0,          // -0.1
      4294967295, // -1.1
      0,          // NaN
      4294967169, // -127
      4294967168, // -128
      4294934529, // -32767
      4294934528, // -32768
      2147483649, // -2147483647
      2147483648, // -2147483648
      4294967041, // -255
      4294967040, // -256
      4294901761, // -65535
      4294901760, // -65536
      1,          // -4294967295
      0,          // -4294967296
      0,          // Infinity
      0,          // -Infinity
      0,          // 0
      2049,       // 2049
      2051,       // 2051
      0,          // 0.00006103515625
      0,          // 0.00006097555160522461
      0,          // 5.960464477539063e-8
      0,          // 2.9802322387695312e-8
      0,          // 2.980232238769532e-8
      0,          // 8.940696716308594e-8
      0,          // 1.4901161193847656e-7
      0,          // 1.490116119384766e-7
      65504,      // 65504
      65520,      // 65520
      65519,      // 65519.99999999999
      0,          // 0.000061005353927612305
      0           // 0.0000610053539276123
    ],
    Float16: [
      127,                    // 127
      128,                    // 128
      32768,                  // 32767
      32768,                  // 32768
      Infinity,               // 2147483647
      Infinity,               // 2147483648
      255,                    // 255
      256,                    // 256
      Infinity,               // 65535
      Infinity,               // 65536
      Infinity,               // 4294967295
      Infinity,               // 4294967296
      Infinity,               // 9007199254740991
      Infinity,               // 9007199254740992
      1.099609375,            // 1.1
      0.0999755859375,        // 0.1
      0.5,                    // 0.5
      0.5,                    // 0.50000001,
      0.60009765625,          // 0.6
      0.7001953125,           // 0.7
      NaN,                    // undefined
      -1,                     // -1
      -0,                     // -0
      -0.0999755859375,       // -0.1
      -1.099609375,           // -1.1
      NaN,                    // NaN
      -127,                   // -127
      -128,                   // -128
      -32768,                 // -32767
      -32768,                 // -32768
      -Infinity,              // -2147483647
      -Infinity,              // -2147483648
      -255,                   // -255
      -256,                   // -256
      -Infinity,              // -65535
      -Infinity,              // -65536
      -Infinity,              // -4294967295
      -Infinity,              // -4294967296
      Infinity,               // Infinity
      -Infinity,              // -Infinity
      0,                      // 0
      2048,                   // 2049
      2052,                   // 2051
      0.00006103515625,       // 0.00006103515625
      0.00006097555160522461, // 0.00006097555160522461
      5.960464477539063e-8,   // 5.960464477539063e-8
      0,                      // 2.9802322387695312e-8
      5.960464477539063e-8,   // 2.980232238769532e-8
      1.1920928955078125e-7,  // 8.940696716308594e-8
      1.1920928955078125e-7,  // 1.4901161193847656e-7
      1.7881393432617188e-7,  // 1.490116119384766e-7
      65504,                  // 65504
      Infinity,               // 65520
      65504,                  // 65519.99999999999
      0.00006103515625,       // 0.000061005353927612305
      0.00006097555160522461  // 0.0000610053539276123
    ],
    Float32: [
      127,                     // 127
      128,                     // 128
      32767,                   // 32767
      32768,                   // 32768
      2147483648,              // 2147483647
      2147483648,              // 2147483648
      255,                     // 255
      256,                     // 256
      65535,                   // 65535
      65536,                   // 65536
      4294967296,              // 4294967295
      4294967296,              // 4294967296
      9007199254740992,        // 9007199254740991
      9007199254740992,        // 9007199254740992
      1.100000023841858,       // 1.1
      0.10000000149011612,     // 0.1
      0.5,                     // 0.5
      0.5,                     // 0.50000001,
      0.6000000238418579,      // 0.6
      0.699999988079071,       // 0.7
      NaN,                     // undefined
      -1,                      // -1
      -0,                      // -0
      -0.10000000149011612,    // -0.1
      -1.100000023841858,      // -1.1
      NaN,                     // NaN
      -127,                    // -127
      -128,                    // -128
      -32767,                  // -32767
      -32768,                  // -32768
      -2147483648,             // -2147483647
      -2147483648,             // -2147483648
      -255,                    // -255
      -256,                    // -256
      -65535,                  // -65535
      -65536,                  // -65536
      -4294967296,             // -4294967295
      -4294967296,             // -4294967296
      Infinity,                // Infinity
      -Infinity,               // -Infinity
      0,                       // 0
      2049,                    // 2049
      2051,                    // 2051
      0.00006103515625,        // 0.00006103515625
      0.00006097555160522461,  // 0.00006097555160522461
      5.960464477539063e-8,    // 5.960464477539063e-8
      2.9802322387695312e-8,   // 2.9802322387695312e-8
      2.9802322387695312e-8,   // 2.980232238769532e-8
      8.940696716308594e-8,    // 8.940696716308594e-8
      1.4901161193847656e-7,   // 1.4901161193847656e-7
      1.4901161193847656e-7,   // 1.490116119384766e-7
      65504,                   // 65504
      65520,                   // 65520
      65520,                   // 65519.99999999999
      0.000061005353927612305, // 0.000061005353927612305
      0.000061005353927612305  // 0.0000610053539276123
    ],
    Float64: [
      127,         // 127
      128,         // 128
      32767,       // 32767
      32768,       // 32768
      2147483647,  // 2147483647
      2147483648,  // 2147483648
      255,         // 255
      256,         // 256
      65535,       // 65535
      65536,       // 65536
      4294967295,  // 4294967295
      4294967296,  // 4294967296
      9007199254740991, // 9007199254740991
      9007199254740992, // 9007199254740992
      1.1,         // 1.1
      0.1,         // 0.1
      0.5,         // 0.5
      0.50000001,  // 0.50000001,
      0.6,         // 0.6
      0.7,         // 0.7
      NaN,         // undefined
      -1,          // -1
      -0,          // -0
      -0.1,        // -0.1
      -1.1,        // -1.1
      NaN,         // NaN
      -127,        // -127
      -128,        // -128
      -32767,      // -32767
      -32768,      // -32768
      -2147483647, // -2147483647
      -2147483648, // -2147483648
      -255,        // -255
      -256,        // -256
      -65535,      // -65535
      -65536,      // -65536
      -4294967295, // -4294967295
      -4294967296, // -4294967296
      Infinity,    // Infinity
      -Infinity,   // -Infinity
      0,           // 0
      2049,                    // 2049
      2051,                    // 2051
      0.00006103515625,        // 0.00006103515625
      0.00006097555160522461,  // 0.00006097555160522461
      5.960464477539063e-8,    // 5.960464477539063e-8
      2.9802322387695312e-8,   // 2.9802322387695312e-8
      2.980232238769532e-8,    // 2.980232238769532e-8
      8.940696716308594e-8,    // 8.940696716308594e-8
      1.4901161193847656e-7,   // 1.4901161193847656e-7
      1.490116119384766e-7,    // 1.490116119384766e-7
      65504,                   // 65504
      65520,                   // 65520
      65519.99999999999,       // 65519.99999999999
      0.000061005353927612305, // 0.000061005353927612305
      0.0000610053539276123    // 0.0000610053539276123
    ]
  }
};


// Copyright (C) 2016 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-dataview.prototype.setint32
description: >
  Set values and return undefined
info: |
  24.2.4.17 DataView.prototype.setInt32 ( byteOffset, value [ , littleEndian ] )

  1. Let v be the this value.
  2. If littleEndian is not present, let littleEndian be false.
  3. Return ? SetViewValue(v, byteOffset, littleEndian, "Int32", value).

  24.2.1.2 SetViewValue ( view, requestIndex, isLittleEndian, type, value )

  ...
  15. Let bufferIndex be getIndex + viewOffset.
  16. Return SetValueInBuffer(buffer, bufferIndex, type, numberValue, isLittleEndian).

  24.1.1.6 SetValueInBuffer ( arrayBuffer, byteIndex, type, value [ , isLittleEndian ] )

  ...
  11. Store the individual bytes of rawBytes into block, in order, starting at
  block[byteIndex].
  12. Return NormalCompletion(undefined).
features: [DataView.prototype.getInt32]
includes: [byteConversionValues.js]
---*/

var buffer = new ArrayBuffer(8);
var sample = new DataView(buffer, 0);

var values = byteConversionValues.values;
var expectedValues = byteConversionValues.expected.Int32;

values.forEach(function(value, i) {
  var expected = expectedValues[i];

  var result = sample.setInt32(0, value, false);

  assert.sameValue(
    sample.getInt32(0),
    expected,
    "value: " + value
  );
  assert.sameValue(
    result,
    undefined,
    "return is undefined, value: " + value
  );
});
