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


// This file was procedurally generated from the following sources:
// - src/class-elements/prod-private-method-initialize-order.case
// - src/class-elements/private-methods/cls-expr.template
/*---
description: Private methods are added before any field initializer is run, even if they appear textually later (private method definitions in a class expression)
esid: prod-MethodDefinition
features: [class-methods-private, class-fields-private, class-fields-public, class]
flags: [generated]
info: |
    ClassElement :
      MethodDefinition
      ...
      ;

    ClassElementName :
      PropertyName
      PrivateName

    PrivateName ::
      # IdentifierName

    MethodDefinition :
      ClassElementName ( UniqueFormalParameters ) { FunctionBody }
      GeneratorMethod
      AsyncMethod
      AsyncGeneratorMethod 
      get ClassElementName () { FunctionBody }
      set ClassElementName ( PropertySetParameterList ) { FunctionBody }

    GeneratorMethod :
      * ClassElementName ( UniqueFormalParameters ){GeneratorBody}

    AsyncMethod :
      async [no LineTerminator here] ClassElementName ( UniqueFormalParameters ) { AsyncFunctionBody }

    AsyncGeneratorMethod :
      async [no LineTerminator here]* ClassElementName ( UniqueFormalParameters ) { AsyncGeneratorBody }

    ---

    InitializeClassElements ( F, proto )

    ...
    5. For each item element in order from elements,
      a. Assert: If element.[[Placement]] is "prototype" or "static", then element.[[Key]] is not a Private Name.
      b. If element.[[Kind]] is "method" and element.[[Placement]] is "static" or "prototype",
        i. Let receiver be F if element.[[Placement]] is "static", else let receiver be proto.
        ii. Perform ? DefineClassElement(receiver, element).

    InitializeInstanceElements ( O, constructor )

    ...
    3. Let elements be the value of F's [[Elements]] internal slot.
    4. For each item element in order from elements,
      a. If element.[[Placement]] is "own" and element.[[Kind]] is "method",
        i. Perform ? DefineClassElement(O, element).

    DefineClassElement (receiver, element)

    ...
    6. If key is a Private Name,
      a. Perform ? PrivateFieldDefine(receiver, key, descriptor).

    PrivateFieldDefine (P, O, desc)

    ...
    6. Append { [[PrivateName]]: P, [[PrivateFieldDescriptor]]: desc } to O.[[PrivateFieldDescriptors]].


    InitializeInstanceElements ( O, constructor )
      ...
      4. For each item element in order from elements,
        a. If element.[[Placement]] is "own" and element.[[Kind]] is "method",
          i. Perform ? DefineClassElement(O, element).
      5. For each item element in order from elements,
        a. If element.[[Placement]] is "own" and element.[[Kind]] is "field",
          i. Assert: element.[[Descriptor]] does not have a [[Value]], [[Get]] or [[Set]] slot.
          ii. Perform ? DefineClassElement(O, element).
      6. Return.

      EDITOR'S NOTE:
      Value properties are added before initializers so that private methods are visible from all initializers.

---*/


/***
 * template notes:
 * 1. method should always be #m
 * 2. the template provides c.ref/other.ref for external reference
 */

function hasProp(obj, name, expected, msg) {
  var hasOwnProperty = Object.prototype.hasOwnProperty.call(obj, name);
  assert.sameValue(hasOwnProperty, expected, msg);

  var hasProperty = Reflect.has(obj, name);
  assert.sameValue(hasProperty, expected, msg);
}

var C = class {
  a = this.#m();

  #m() { return 42; }
  get bGetter() { return this.#b; }

  #b = this.#m();


  get ref() { return this.#m; }

  constructor() {
    hasProp(this, '#m', false, 'private methods are defined in an special internal slot and cannot be found as own properties');
    assert.sameValue(typeof this.#m, 'function');
    assert.sameValue(this.ref, this.#m, 'returns the same value');
    assert.sameValue(this.#m, (() => this)().#m, 'memberexpression and call expression forms');

    assert.sameValue(this.a, 42);
    assert.sameValue(this.#b, 42);

  }
}

var c = new C();
var other = new C();

hasProp(C.prototype, '#m', false, 'method is not defined in the prototype');
hasProp(C, '#m', false, 'method is not defined in the contructor');
hasProp(c, '#m', false, 'method cannot be seen outside of the class');

/***
 * MethodDefinition : ClassElementName ( UniqueFormalParameters ) { FunctionBody }
 * 
 * 1. Let methodDef be DefineMethod of MethodDefinition with argument homeObject.
 * ...
 */
assert.sameValue(c.ref, other.ref, 'The method is defined once, and reused on every new instance');

assert.sameValue(c.a, 42);
assert.sameValue(c.bGetter, 42);
