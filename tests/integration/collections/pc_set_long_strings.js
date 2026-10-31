// protoCore.Set compares strings by content (SameValueZero), however they
// were built. Strings longer than six UTF-8 bytes are heap ropes, and two of
// them built at run time are two different objects with equal content: a
// proto::ProtoSet compares its elements by pointer from protoCore 3.5.0 on,
// so this test fails when the binding stores a ProtoSet.
// Exits 1 on the first failed check.

function check(name, cond) {
    if (!cond) { console.log("FAILED: " + name); process.exit(1); }
}

function build(prefix, i) { return [prefix, "-", String(i), "-", "long-enough-to-be-a-rope"].join(""); }

var s = new protoCore.Set();
var N = 200;
for (var i = 0; i < N; i++) s.add(build("key", i));
check("size after distinct adds", s.size() === N);

// Every equal copy built again is found and is not stored twice.
for (var i = 0; i < N; i++) {
    check("has(equal copy) #" + i, s.has(build("key", i)) === true);
    s.add(build("key", i));
}
check("size unchanged after re-adding equal copies", s.size() === N);
check("absent string", s.has(build("other", 1)) === false);

// A literal and a run-time copy are one element.
var lit = new protoCore.Set(["a string literal longer than six bytes"]);
check("literal found via run-time copy",
      lit.has(["a string literal ", "longer than six bytes"].join("")) === true);
lit.add("a string literal longer" + " than six bytes".slice(0));
check("literal and run-time copy are one element", lit.size() === 1);

// The constructor collapses equal copies too.
var seeded = new protoCore.Set([build("k", 1), build("k", 1), build("k", 2)]);
check("constructor collapses equal copies", seeded.size() === 2);

// remove() of an equal copy removes the element.
s.remove(build("key", 7));
check("remove(equal copy) removes", s.has(build("key", 7)) === false && s.size() === N - 1);
s.remove(build("key", 7));
check("second remove is a no-op", s.size() === N - 1);

console.log("pc_set_long_strings: all checks passed");
