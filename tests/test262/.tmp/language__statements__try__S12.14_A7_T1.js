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


// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: Evaluating the nested productions TryStatement
es5id: 12.14_A7_T1
description: >
    Checking if the production of nested TryStatement statements
    evaluates correct
---*/

// CHECK#1
try{
  try{
    throw "ex2";
  }
  catch(er2){
    if (er2!=="ex2")
      throw new Test262Error('#1.1: Exception === "ex2". Actual:  Exception ==='+ e  );
      throw "ex1";
    }
  }
  catch(er1){
    if (er1!=="ex1") throw new Test262Error('#1.2: Exception === "ex1". Actual: '+er1);
    if (er1==="ex2") throw new Test262Error('#1.3: Exception !== "ex2". Actual: catch previous embedded exception');
}

// CHECK#2
try{
  throw "ex1";
}
catch(er1){
  try{
    throw "ex2";
  }
  catch(er1){
    if (er1==="ex1") throw new Test262Error('#2.1: Exception !== "ex1". Actual: catch previous catching exception');
    if (er1!=="ex2") throw new Test262Error('#2.2: Exception === "ex2". Actual:  Exception ==='+ er1  );
  }
  if (er1!=="ex1") throw new Test262Error('#2.3: Exception === "ex1". Actual:  Exception ==='+ er1  );
  if (er1==="ex2") throw new Test262Error('#2.4: Exception !== "ex2". Actual: catch previous catching exception');
}

// CHECK#3
try{
  throw "ex1";
}
catch(er1){
  if (er1!=="ex1") throw new Test262Error('#3.1: Exception ==="ex1". Actual:  Exception ==='+ er1  );
}
finally{
  try{
    throw "ex2";
  }
  catch(er1){
    if (er1==="ex1") throw new Test262Error('#3.2: Exception !=="ex1". Actual: catch previous embedded exception');
    if (er1!=="ex2") throw new Test262Error('#3.3: Exception ==="ex2". Actual:  Exception ==='+ er1  );
  }
}

// CHECK#4
var c4=0;
try{
  throw "ex1";
}
catch(er1){
  try{
    throw "ex2";
  }
  catch(er1){
    if (er1==="ex1") throw new Test262Error('#4.1: Exception !=="ex1". Actual: catch previous catching exception');
    if (er1!=="ex2") throw new Test262Error('#4.2: Exception ==="ex2". Actual:  Exception ==='+ er1  );
  }
  if (er1!=="ex1") throw new Test262Error('#4.3: Exception ==="ex1". Actual:  Exception ==='+ er1  );
  if (er1==="ex2") throw new Test262Error('#4.4: Exception !=="ex2". Actual: Catch previous embedded exception');
}
finally{
  c4=1;
}
if (c4!==1) throw new Test262Error('#4.5: "finally" block must be evaluated');

// CHECK#5
var c5=0;
try{
  try{
    throw "ex2";
  }
  catch(er1){
    if (er1!=="ex2") throw new Test262Error('#5.1: Exception ==="ex2". Actual:  Exception ==='+ er1  );
  }
  throw "ex1";
}
catch(er1){
  if (er1!=="ex1") throw new Test262Error('#5.2: Exception ==="ex1". Actual:  Exception ==='+ er1  );
  if (er1==="ex2") throw new Test262Error('#5.3: Exception !=="ex2". Actual: catch previous embedded exception');
}
finally{
  c5=1;
}
if (c5!==1) throw new Test262Error('#5.4: "finally" block must be evaluated');

// CHECK#6
var c6=0;
try{
  try{
    throw "ex1";
  }
  catch(er1){
    if (er1!=="ex1") throw new Test262Error('#6.1: Exception ==="ex1". Actual:  Exception ==='+ er1  );
  }
}
finally{
  c6=1;		
}
if (c6!==1) throw new Test262Error('#6.2: "finally" block must be evaluated');

// CHECK#7
var c7=0;
try{
  try{
    throw "ex1";
  }
  finally{
    try{
      c7=1;
      throw "ex2";
    }
    catch(er1){
      if (er1!=="ex2") throw new Test262Error('#7.1: Exception ==="ex2". Actual:  Exception ==='+ er1  );
      if (er1==="ex1") throw new Test262Error('#7.2: Exception !=="ex1". Actual: catch previous embedded exception');
      c7++;
    }
  }
}
catch(er1){
  if (er1!=="ex1") throw new Test262Error('#7.3: Exception ==="ex1". Actual:  Exception ==='+ er1  );
}
if (c7!==2) throw new Test262Error('#7.4: "finally" block must be evaluated');
