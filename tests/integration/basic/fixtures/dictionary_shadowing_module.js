// Loaded by basic/dictionary_shadowing.js after objects of the main script hold
// `shadowModuleLateKey` in their property dictionaries: this module is the first
// code that names the key, and it gives a prototype a named property of the
// same spelling.
'use strict';
module.exports = {
    install(Ctor) { Ctor.prototype.shadowModuleLateKey = 'proto'; },
    read(o) { return o.shadowModuleLateKey; },
    write(o, v) { o.shadowModuleLateKey = v; },
};
