// size(), values() (iteration, in insertion order), remove() and has() of
// protoCore.Set over a mix of values whose equality is by value (long
// strings, boxed doubles, NaN, BigInts) and by identity (objects).
// Exits 1 on the first failed check.

function check(name, cond) {
    if (!cond) { console.log("FAILED: " + name); process.exit(1); }
}

function str(i) { return ["element-number-", String(i)].join(""); }
var obj = {};
var arr = [1, 2];

var s = new protoCore.Set();
var N = 500;
for (var i = 0; i < N; i++) s.add(str(i));
s.add(obj).add(arr).add(2.5).add(Number("x")).add(10n);
check("size", s.size() === N + 5);

// Objects are members by identity only.
check("has(obj)", s.has(obj) === true);
check("an equal-looking object is not a member", s.has({}) === false && s.has([1, 2]) === false);

// Delete every other string through an equal copy.
for (var i = 0; i < N; i += 2) s.remove(str(i));
check("size after deletes", s.size() === N / 2 + 5);
var ok = true;
for (var i = 0; i < N; i++) if (s.has(str(i)) !== (i % 2 === 1)) { ok = false; break; }
check("has() after deletes", ok);

// Iteration: insertion order, every element once.
var v = s.values();
check("values() length", v.length === N / 2 + 5);
var ordered = true;
for (var i = 0; i < N / 2; i++) if (v[i] !== str(2 * i + 1)) { ordered = false; break; }
check("values() keeps insertion order and skips deleted elements", ordered);
check("values() tail", v[N / 2] === obj && v[N / 2 + 1] === arr && v[N / 2 + 2] === 2.5 &&
      v[N / 2 + 3] !== v[N / 2 + 3] && v[N / 2 + 4] === 10n);
check("values() is an Array", Array.isArray(v));

// A deleted element added again goes to the end.
s.add(str(0));
var x = s.values();
check("re-added element is last", x[x.length - 1] === str(0) && s.size() === N / 2 + 6);

// The snapshot does not follow later mutations.
s.remove(obj);
check("values() is a snapshot", x.indexOf(obj) >= 0 && s.has(obj) === false);

// Removing an absent element is a no-op and returns the set.
check("remove(absent) returns the set", s.remove("absent element string") === s);
check("size after no-op remove", s.size() === N / 2 + 5);

// BigInt by value.
check("BigInt by value", s.has(BigInt(10)) === true && s.has(10) === false);
s.remove(5n * 2n);
check("remove(equal BigInt)", s.has(10n) === false);

console.log("pc_set_operations: all checks passed");
