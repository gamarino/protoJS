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
// - src/class-elements/private-static-method-shadowed-by-getter-on-nested-class.case
// - src/class-elements/default/cls-decl.template
/*---
description: PrivateName of private static method can be shadowed on inner classes by a private getter (field definitions in a class declaration)
esid: prod-FieldDefinition
features: [class-static-methods-private, class-static-fields-public, class-methods-private, class]
flags: [generated]
info: |
    Updated Productions

    CallExpression[Yield, Await]:
      CoverCallExpressionAndAsyncArrowHead[?Yield, ?Await]
      SuperCall[?Yield, ?Await]
      CallExpression[?Yield, ?Await]Arguments[?Yield, ?Await]
      CallExpression[?Yield, ?Await][Expression[+In, ?Yield, ?Await]]
      CallExpression[?Yield, ?Await].IdentifierName
      CallExpression[?Yield, ?Await]TemplateLiteral[?Yield, ?Await]
      CallExpression[?Yield, ?Await].PrivateIdentifier

    ClassTail : ClassHeritage { ClassBody }
      ...
      6. Let classPrivateEnvironment be NewDeclarativeEnvironment(outerPrivateEnvironment).
      7. Let classPrivateEnvRec be classPrivateEnvironment's EnvironmentRecord.
      ...
      15. Set the running execution context's LexicalEnvironment to classScope.
      16. Set the running execution context's PrivateEnvironment to classPrivateEnvironment.
      ...
      33. If PrivateBoundIdentifiers of ClassBody contains a Private Name P such that P's [[Kind]] field is either "method" or "accessor" and P's [[Brand]] is F,
        a. PrivateBrandAdd(F, F).
      34. For each item fieldRecord in order from staticFields,
        a. Perform ? DefineField(F, field).

      FieldDefinition : ClassElementName Initializer_opt
        1. Let name be the result of evaluating ClassElementName.
        2. ReturnIfAbrupt(name).
        3. If Initializer_opt is present,
          a. Let lex be the Lexical Environment of the running execution context.
          b. Let formalParameterList be an instance of the production FormalParameters : [empty].
          c. Let privateScope be the PrivateEnvironment of the running execution context.
          d. Let initializer be FunctionCreate(Method, formalParameterList, Initializer, lex, true, privateScope).
          e. Perform MakeMethod(initializer, homeObject).
          f. Let isAnonymousFunctionDefinition be IsAnonymousFunctionDefinition(Initializer).
        4. Else,
          a. Let initializer be empty.
          b. Let isAnonymousFunctionDeclaration be false.
        5. Return a Record { [[Name]]: name, [[Initializer]]: initializer, [[IsAnonymousFunctionDefinition]]: isAnonymousFunctionDefinition }.

    MemberExpression : MemberExpression.PrivateIdentifier
      1. Let baseReference be the result of evaluating MemberExpression.
      2. Let baseValue be ? GetValue(baseReference).
      3. Let bv be ? RequireObjectCoercible(baseValue).
      4. Let fieldNameString be the StringValue of PrivateIdentifier.
      5. Return MakePrivateReference(bv, fieldNameString).

    MakePrivateReference(baseValue, privateIdentifier)
      1. Let env be the running execution context's PrivateEnvironment.
      2. Let privateNameBinding be ? ResolveBinding(privateIdentifier, env).
      3. Let privateName be GetValue(privateNameBinding).
      4. Assert: privateName is a Private Name.
      5. Return a value of type Reference whose base value is baseValue, whose referenced name is privateName, whose strict reference flag is true.

---*/


class C {
  static #m() { return 'outer class'; }

  static methodAccess() {
    return this.#m();
  }

  static B = class {
    get #m() { return 'inner class'; }

    static access(o) {
      return o.#m;
    }
  }
}

assert.sameValue(C.methodAccess(), 'outer class');

let b = new C.B();

assert.sameValue(C.B.access(b), 'inner class');

assert.throws(TypeError, function() {
  C.B.access(C);
}, 'accessed private getter from an arbritary object');
