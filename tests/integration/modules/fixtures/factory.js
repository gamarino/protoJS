// Fixture: the exported factory builds its closure AFTER the module body has
// returned.
//
// The inner function is therefore created while the interpreter is running a
// nested function of this module, not its top-level body. At that moment the
// running module's own function table is empty — the flat table lives on the
// module root — so this is the case that fails when closure-to-module
// resolution falls back to whichever module happens to be current on the
// thread (the requiring script's).
module.exports = {
    make: function (base) {
        return function (n) { return base + n; };
    }
};
