// Fixture: paired with same_name_b.js.
//
// Both modules export functions with the SAME names, declared in the same
// order and with the same shape, so their bytecode IDs line up index for
// index. A loader that resolves a closure's __bytecode_id__ against the wrong
// module's function table therefore does not miss and throw — it silently runs
// the OTHER module's body, which is the dangerous failure. These two fixtures
// pin the invariant that a closure always resolves against the module that
// created it.
module.exports = {
    whoami: function whoami() { return 'A'; },
    scale: function scale(n) { return n * 2; }
};
