// Adapted from Node.js v22.20.0 test/parallel/test-path-resolve.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';
// protoJS: `fn(...args)` instead of `fn.apply(null, args)`: protoJS's native
// module functions do not inherit Function.prototype (no call/apply/bind).
// protoJS: `s.split(c).join(r)` instead of `s.replace(/c/g, r)`: a global
// RegExp replace in protoJS skips the character after each match and crashes
// when the last match ends the string (RegExpPrototype.cpp).
const common = require('./node_shim').common;
const assert = require('./node_shim').assert;
const path = require('path');

const failures = [];
const slashRE = /\//g;
const backslashRE = /\\/g;

const posixyCwd = common.isWindows ?
  (() => {
    const _ = process.cwd()
      .replaceAll(path.sep, path.posix.sep);
    return _.slice(_.indexOf(path.posix.sep));
  })() :
  process.cwd();

const resolveTests = [
  [ path.win32.resolve,
    // Arguments                               result
    [[['c:/blah\\blah', 'd:/games', 'c:../a'], 'c:\\blah\\a'],
     [['c:/ignore', 'd:\\a/b\\c/d', '\\e.exe'], 'd:\\e.exe'],
     [['c:/ignore', 'c:/some/file'], 'c:\\some\\file'],
     [['d:/ignore', 'd:some/dir//'], 'd:\\ignore\\some\\dir'],
     [[], process.cwd()],
     [[''], process.cwd()],
     [['.'], process.cwd()],
     [['//server/share', '..', 'relative\\'], '\\\\server\\share\\relative'],
     [['c:/', '//'], 'c:\\'],
     [['c:/', '//dir'], 'c:\\dir'],
     [['c:/', '//server/share'], '\\\\server\\share\\'],
     [['c:/', '//server//share'], '\\\\server\\share\\'],
     [['c:/', '///some//dir'], 'c:\\some\\dir'],
     [['C:\\foo\\tmp.3\\', '..\\tmp.3\\cycles\\root.js'],
      'C:\\foo\\tmp.3\\cycles\\root.js'],
     [['\\\\.\\PHYSICALDRIVE0'], '\\\\.\\PHYSICALDRIVE0'],
     [['\\\\?\\PHYSICALDRIVE0'], '\\\\?\\PHYSICALDRIVE0'],
    ],
  ],
  [ path.posix.resolve,
    // Arguments                    result
    [[['/var/lib', '../', 'file/'], '/var/file'],
     [['/var/lib', '/../', 'file/'], '/file'],
     [['a/b/c/', '../../..'], posixyCwd],
     [[], posixyCwd],
     [[''], posixyCwd],
     [['.'], posixyCwd],
     [['/some/dir', '.', '/absolute/'], '/absolute'],
     [['/foo/tmp.3/', '../tmp.3/cycles/root.js'], '/foo/tmp.3/cycles/root.js'],
    ],
  ],
];
resolveTests.forEach(([resolve, tests]) => {
  tests.forEach(([test, expected]) => {
    const actual = resolve(...test);
    let actualAlt;
    const os = resolve === path.win32.resolve ? 'win32' : 'posix';
    if (resolve === path.win32.resolve && !common.isWindows)
      actualAlt = actual.split('\\').join('/');
    else if (resolve !== path.win32.resolve && common.isWindows)
      actualAlt = actual.split('/').join('\\');

    const message =
      `path.${os}.resolve(${test.map(JSON.stringify).join(',')})\n  expect=${
        JSON.stringify(expected)}\n  actual=${JSON.stringify(actual)}`;
    if (actual !== expected && actualAlt !== expected)
      failures.push(message);
  });
});
assert.strictEqual(failures.length, 0, failures.join('\n'));

// protoJS: two cases of the original are left out.
//  * On Windows it runs a child process (child_process.spawnSync and a
//    fixture script) to check how a bare drive letter resolves in a fresh
//    process; protoJS's child_process has no spawnSync.
//  * Elsewhere it replaces process.cwd with a function returning '' and
//    expects resolve() to fall back to '.'. Node's resolve() calls the
//    JavaScript process.cwd; protoJS's native resolve() asks the operating
//    system, so the patched function is not consulted.

require('./node_shim').done('path/test-path-resolve');
