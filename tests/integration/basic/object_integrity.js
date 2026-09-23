// Object integrity levels are PER-OBJECT and are never inherited.
//
// ECMA-262 §7.3.15/§7.3.16: [[Extensible]] is an internal slot of the
// object itself.  Freezing, sealing or preventing extensions on a
// prototype must not affect objects that inherit from it — neither ones
// created afterwards with `Object.create` / `new F()` nor ones re-pointed
// at it later with `Object.setPrototypeOf`.
//
// protoJS used to record the integrity level by attaching marker objects
// to the protoCore parent chain.  protoCore 2.0.0's `newChild` captures
// the prototype's CURRENT chain by value, so every child of a frozen
// prototype inherited the markers and came out frozen.  The state now
// lives in a per-object internal own attribute instead, which `newChild`
// never copies.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

// ---- 1. The integrity level applies to the object it was set on -----------

var frozen = Object.freeze({ a: 1 });
check("freeze: isFrozen", Object.isFrozen(frozen) === true);
check("freeze: isSealed", Object.isSealed(frozen) === true);
check("freeze: isExtensible", Object.isExtensible(frozen) === false);
frozen.a = 99;
check("freeze: existing property is not writable", frozen.a === 1, "a = " + frozen.a);
frozen.b = 1;
check("freeze: no new properties", frozen.b === undefined, "b = " + frozen.b);

var sealed = Object.seal({ a: 1 });
check("seal: isSealed", Object.isSealed(sealed) === true);
check("seal: isFrozen is false", Object.isFrozen(sealed) === false);
check("seal: isExtensible", Object.isExtensible(sealed) === false);
sealed.a = 2;
check("seal: existing property stays writable", sealed.a === 2, "a = " + sealed.a);
sealed.b = 1;
check("seal: no new properties", sealed.b === undefined, "b = " + sealed.b);

var nonExt = Object.preventExtensions({ a: 1 });
check("preventExtensions: isExtensible", Object.isExtensible(nonExt) === false);
nonExt.a = 2;
check("preventExtensions: existing property stays writable", nonExt.a === 2, "a = " + nonExt.a);
nonExt.b = 1;
check("preventExtensions: no new properties", nonExt.b === undefined, "b = " + nonExt.b);

// ---- 2. Object.create(frozenProto) ----------------------------------------

var frozenProto = Object.freeze({ inherited: 1 });
var viaCreate = Object.create(frozenProto);
check("Object.create(frozenProto): child is extensible",
      Object.isExtensible(viaCreate) === true);
check("Object.create(frozenProto): child is not frozen",
      Object.isFrozen(viaCreate) === false);
check("Object.create(frozenProto): child is not sealed",
      Object.isSealed(viaCreate) === false);
viaCreate.own = 42;
check("Object.create(frozenProto): child accepts new properties",
      viaCreate.own === 42, "own = " + viaCreate.own);
check("Object.create(frozenProto): inheritance still works",
      viaCreate.inherited === 1, "inherited = " + viaCreate.inherited);

var sealedProto = Object.seal({ inherited: 2 });
var viaCreateSealed = Object.create(sealedProto);
check("Object.create(sealedProto): child is extensible",
      Object.isExtensible(viaCreateSealed) === true);
check("Object.create(sealedProto): child is not sealed",
      Object.isSealed(viaCreateSealed) === false);
viaCreateSealed.own = 7;
check("Object.create(sealedProto): child accepts new properties",
      viaCreateSealed.own === 7, "own = " + viaCreateSealed.own);

var nonExtProto = Object.preventExtensions({ inherited: 3 });
var viaCreateNonExt = Object.create(nonExtProto);
check("Object.create(nonExtensibleProto): child is extensible",
      Object.isExtensible(viaCreateNonExt) === true);
viaCreateNonExt.own = 8;
check("Object.create(nonExtensibleProto): child accepts new properties",
      viaCreateNonExt.own === 8, "own = " + viaCreateNonExt.own);

// ---- 3. new F() with a frozen F.prototype ---------------------------------

function F() {}
F.prototype.shared = 1;
Object.freeze(F.prototype);
var inst = new F();
check("new F() with frozen prototype: instance is extensible",
      Object.isExtensible(inst) === true);
check("new F() with frozen prototype: instance is not frozen",
      Object.isFrozen(inst) === false);
inst.own = 5;
check("new F() with frozen prototype: instance accepts new properties",
      inst.own === 5, "own = " + inst.own);
check("new F() with frozen prototype: inheritance still works",
      inst.shared === 1, "shared = " + inst.shared);

// A constructor that assigns in its body must still work.
function G() { this.x = 1; }
Object.freeze(G.prototype);
var g = new G();
check("new G() with frozen prototype: constructor body assignment works",
      g.x === 1, "x = " + g.x);

// ---- 4. Object.setPrototypeOf(o, frozenProto) -----------------------------

var reparented = {};
Object.setPrototypeOf(reparented, frozenProto);
check("setPrototypeOf(o, frozenProto): o is still extensible",
      Object.isExtensible(reparented) === true);
check("setPrototypeOf(o, frozenProto): o is not frozen",
      Object.isFrozen(reparented) === false);
reparented.own = 3;
check("setPrototypeOf(o, frozenProto): o accepts new properties",
      reparented.own === 3, "own = " + reparented.own);
check("setPrototypeOf(o, frozenProto): inheritance still works",
      reparented.inherited === 1, "inherited = " + reparented.inherited);

// ---- 5. Freezing a prototype does not leak to later plain objects ---------

var deepProto = Object.create(frozenProto);
var deepChild = Object.create(deepProto);
check("grandchild of a frozen prototype is extensible",
      Object.isExtensible(deepChild) === true);
deepChild.own = 1;
check("grandchild of a frozen prototype accepts new properties",
      deepChild.own === 1, "own = " + deepChild.own);

var plain = {};
check("a fresh object literal is extensible", Object.isExtensible(plain) === true);
plain.k = 1;
check("a fresh object literal accepts new properties", plain.k === 1);

// ---- 6. The prototype itself is still frozen ------------------------------

check("frozen prototype is still frozen after children exist",
      Object.isFrozen(frozenProto) === true);
frozenProto.inherited = 99;
check("frozen prototype still rejects writes",
      frozenProto.inherited === 1, "inherited = " + frozenProto.inherited);

// ---- 7. Object.getPrototypeOf is unaffected -------------------------------

check("getPrototypeOf(frozen object) is its real prototype",
      Object.getPrototypeOf(Object.freeze({})) === Object.prototype);
check("getPrototypeOf(Object.create(frozenProto)) is frozenProto",
      Object.getPrototypeOf(Object.create(frozenProto)) === frozenProto);

// ---- 8. Reflect mirrors Object --------------------------------------------

if (typeof Reflect !== "undefined" && Reflect.isExtensible) {
    var rFrozenProto = Object.freeze({});
    var rChild = Object.create(rFrozenProto);
    check("Reflect.isExtensible(frozen) === false",
          Reflect.isExtensible(rFrozenProto) === false);
    check("Reflect.isExtensible(child of frozen) === true",
          Reflect.isExtensible(rChild) === true);

    var rTarget = {};
    Reflect.preventExtensions(rTarget);
    check("Reflect.preventExtensions marks the target",
          Reflect.isExtensible(rTarget) === false);
    var rTargetChild = Object.create(rTarget);
    check("Reflect.preventExtensions does not mark children",
          Reflect.isExtensible(rTargetChild) === true);
}

// ---- 9. Arrays -------------------------------------------------------------

var frozenArr = Object.freeze([1, 2, 3]);
check("frozen array: isFrozen", Object.isFrozen(frozenArr) === true);
frozenArr[0] = 9;
check("frozen array: element write rejected", frozenArr[0] === 1, "[0] = " + frozenArr[0]);
var liveArr = [1, 2, 3];
liveArr[0] = 9;
check("unfrozen array still writable", liveArr[0] === 9, "[0] = " + liveArr[0]);

// ---- Result ---------------------------------------------------------------

if (failures.length) {
    console.log("object_integrity: " + failures.length + " check(s) failed");
    for (var i = 0; i < failures.length; i++) {
        console.log("  FAIL: " + failures[i]);
    }
    process.exit(1);
}
console.log("object_integrity: all checks passed");
