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


// Copyright 2015 Microsoft Corporation. All rights reserved.
// This code is governed by the license found in the LICENSE file.

/*---
esid: sec-object.assign
description: >
  Object.assign with an Array Exotic Object target employs the corresponding
  internal methods.
info: |
  Object.assign ( _target_, ..._sources_ )
  3.a.iii.2.b. Perform ? Set(_to_, _nextKey_, _propValue_, *true*).

  Set ( _O_, _P_, _V_, _Throw_ )
  1. Let _success_ be ? _O_.[[Set]](_P_, _V_, _O_).

  OrdinarySet ( _O_, _P_, _V_, _Receiver_ )
  1. Let _ownDesc_ be ? _O_.[[GetOwnProperty]](_P_).
  2. Return ? OrdinarySetWithOwnDescriptor(_O_, _P_, _V_, _Receiver_, _ownDesc_).

  OrdinarySetWithOwnDescriptor ( _O_, _P_, _V_, _Receiver_, _ownDesc_ )
  1. If _ownDesc_ is *undefined*, then
     a. Let _parent_ be ? O.[[GetPrototypeOf]]().
     b. If _parent_ is not *null*, then
        i. Return ? _parent_.[[Set]](_P_, _V_, _Receiver_).
     c. Else,
        i. Set _ownDesc_ to the PropertyDescriptor { [[Value]]: *undefined*, [[Writable]]: *true*, [[Enumerable]]: *true*, [[Configurable]]: *true* }.
  2. If IsDataDescriptor(_ownDesc_) is *true*, then
     ...
     c. Let _existingDescriptor_ be ? _Receiver_.[[GetOwnProperty]](_P_).
     d. If _existingDescriptor_ is not *undefined*, then
        ...
        iii. Let _valueDesc_ be the PropertyDescriptor { [[Value]]: _V_ }.
        iv. Return ? _Receiver_.[[DefineOwnProperty]](_P_, _valueDesc_).
     e. Else,
        i. Assert: _Receiver_ does not currently have a property _P_.
        ii. Return ? CreateDataProperty(_Receiver_, _P_, _V_).

  CreateDataProperty ( _O_, _P_, _V_ )
  1. Let _newDesc_ be the PropertyDescriptor { [[Value]]: _V_, [[Writable]]: *true*, [[Enumerable]]: *true*, [[Configurable]]: *true* }.
  2. Return ? _O_.[[DefineOwnProperty]](_P_, _newDesc_).

  Array exotic object [[DefineOwnProperty]] ( _P_, _Desc_ )
  1. If _P_ is *"length"*, then
     a. Return ? ArraySetLength(_A_, _Desc_).
  2. Else if _P_ is an array index, then
     ...
     k. If _index_ ≥ _length_, then
        i. Set _lengthDesc_.[[Value]] to _index_ + *1*𝔽.
        ii. Set _succeeded_ to ! OrdinaryDefineOwnProperty(_A_, *"length"*, _lengthDesc_).
  3. Return ? OrdinaryDefineOwnProperty(_A_, _P_, _Desc_).

  The Object Type
  An **integer index** is a property name _n_ such that CanonicalNumericIndexString(_n_) returns an
  integral Number in the inclusive interval from *+0*𝔽 to 𝔽(2**53 - 1). An **array index** is an
  integer index _n_ such that CanonicalNumericIndexString(_n_) returns an integral Number in the
  inclusive interval from *+0*𝔽 to 𝔽(2**32 - 2).
---*/

var target = [7, 8, 9];
var result = Object.assign(target, [1]);
assert.sameValue(result, target);
assert.compareArray(result, [1, 8, 9],
  "elements must be assigned from an array source onto an array target");

var sparseArraySource = [];
sparseArraySource[2] = 3;
result = Object.assign(target, sparseArraySource);
assert.sameValue(result, target);
assert.compareArray(result, [1, 8, 3], "holes in a sparse array source must not be copied");

var shortObjectSource = { 1: 2, length: 2 };
shortObjectSource["-0"] = -1;
shortObjectSource["1.5"] = -2;
shortObjectSource["4294967295"] = -3; // 2**32 - 1
result = Object.assign(target, shortObjectSource);
assert.sameValue(result, target);
assert.compareArray(result, [1, 2],
  "array index properties must be copied from a non-array source");
assert.sameValue(result["-0"], -1,
  "a property with name -0 must be assigned onto an array target");
assert.sameValue(result["1.5"], -2,
  "a property with name 1.5 must be assigned onto an array target");
assert.sameValue(result["4294967295"], -3,
  "a property with name 4294967295 (2**32 - 1) must be assigned onto an array target");

result = Object.assign(target, { length: 1 });
assert.sameValue(result, target);
assert.compareArray(result, [1], "assigning a short length must shrink an array target");

result = Object.assign(target, { 2: 0 });
assert.sameValue(result, target);
assert.compareArray(result, [1, undefined, 0],
  "assigning a high array index must grow an array target");

if (typeof Proxy !== 'undefined') {
  var accordionSource = new Proxy({ length: 0, 1: 9 }, {
    ownKeys: function() {
      return ["length", "1"];
    }
  });
  result = Object.assign(target, accordionSource);
  assert.sameValue(result, target);
  assert.compareArray(result, [undefined, 9],
    "assigning a short length before a high array index must shrink and then grow an array target");
}
