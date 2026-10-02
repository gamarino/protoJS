#!/usr/bin/env bash
#
# CLI check: a module reached through two spellings of the same file is one
# module -- loaded once, one exports object -- for require() and for import.
#
# A module's identity is its canonical path. CommonJS took it from
# std::filesystem::canonical and ES modules from the realpath() shim; on Windows
# that shim was GetFullPathNameW, which neither resolves a symbolic link nor
# restores the case the file system stores, so an ES module imported as
# "./lib/m.mjs" and as "./LIB/M.mjs" was evaluated twice. Both loaders now take
# the identity from one function, protojs::platform::canonicalPath
# (src/platform/Posix.h): realpath() on POSIX, GetFinalPathNameByHandleW on
# Windows.
#
# Cases (each must load its module exactly once):
#   cjs-symlink   require('./lib/counter.js') and require('./alias/counter.js'),
#                 where alias is a symbolic link to lib
#   esm-symlink   the same for import
#   cjs-case      (Windows only) require('./LIB/Counter.js')
#   esm-case      (Windows only) import './LIB/M.mjs'
# The case variants run only on Windows: there the file system is
# case-insensitive and the canonical path restores the stored case. macOS's
# default volume is case-insensitive too, but realpath() there returns the
# spelling it was given, as Node's does, so the two spellings stay two modules
# on macOS, in protoJS as in Node.
#
# Usage: module-identity.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: module-identity.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: module-identity.sh <protojs> <scratch-dir>}"

IS_WINDOWS=0
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
esac

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/lib" || exit 1
cd "$SCRATCH" || exit 1

cat > lib/counter.js <<'EOF'
globalThis.__loads = (globalThis.__loads || 0) + 1;
module.exports = { loads: globalThis.__loads };
EOF
cat > lib/m.mjs <<'EOF'
export const n = (globalThis.__eloads = (globalThis.__eloads || 0) + 1);
EOF

# A real symbolic link on every platform. Git for Windows' ln copies the
# directory unless told to make a native link; the runners may create them.
if [ "$IS_WINDOWS" -eq 1 ]; then
    MSYS=winsymlinks:nativestrict ln -s lib alias
else
    ln -s lib alias
fi
if [ ! -L alias ]; then
    echo "FAIL: could not create the symbolic link alias -> lib"
    exit 1
fi

FAILED=0

# run_case <name> <flags> <file> : the program throws when the module loaded twice.
run_case() {
    local name="$1" flags="$2" file="$3"
    # shellcheck disable=SC2086
    if timeout 60 "$PROTOJS" $flags "$file" > "$name.out" 2>&1; then
        echo "ok   [$name]"
    else
        echo "FAIL [$name]"
        cat "$name.out"
        FAILED=1
    fi
}

cat > cjs-symlink.js <<'EOF'
const a = require('./lib/counter.js');
const b = require('./alias/counter.js');
if (a !== b || globalThis.__loads !== 1)
    throw new Error('loaded ' + globalThis.__loads + ' times; same object: ' + (a === b));
EOF
run_case cjs-symlink "" cjs-symlink.js

cat > esm-symlink.mjs <<'EOF'
import { n as a } from './lib/m.mjs';
import { n as b } from './alias/m.mjs';
if (a !== 1 || b !== 1 || globalThis.__eloads !== 1)
    throw new Error('evaluated ' + globalThis.__eloads + ' times');
EOF
run_case esm-symlink "--input-type=module" esm-symlink.mjs

if [ "$IS_WINDOWS" -eq 1 ]; then
    cat > cjs-case.js <<'EOF'
const a = require('./lib/counter.js');
const b = require('./LIB/Counter.js');
if (a !== b || globalThis.__loads !== 1)
    throw new Error('loaded ' + globalThis.__loads + ' times; same object: ' + (a === b));
EOF
    run_case cjs-case "" cjs-case.js

    cat > esm-case.mjs <<'EOF'
import { n as a } from './lib/m.mjs';
import { n as b } from './LIB/M.mjs';
if (a !== 1 || b !== 1 || globalThis.__eloads !== 1)
    throw new Error('evaluated ' + globalThis.__eloads + ' times');
EOF
    run_case esm-case "--input-type=module" esm-case.mjs
fi

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "PASS: every spelling of a module file names one module"
