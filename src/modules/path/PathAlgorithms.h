/*
 * PathAlgorithms — Node.js's `path` algorithms (win32 and posix), as pure
 * string functions.
 *
 * A port of lib/path.js from Node.js v22.20.0. Every function follows Node's
 * algorithm step by step, on UTF-16 code units as Node does (the strings are
 * UTF-8 at this interface and converted internally), so results match Node
 * character for character, including the Unicode case mapping that
 * win32.relative applies. Nothing here touches the file system: resolve() is
 * purely lexical and is given the current directory (and, for win32, the
 * per-drive current directories) by its caller.
 *
 * Derived from Node.js, which carries this notice:
 *
 *   Copyright Joyent, Inc. and other Node contributors.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef PROTOJS_PATH_ALGORITHMS_H
#define PROTOJS_PATH_ALGORITHMS_H

#include <functional>
#include <string>
#include <vector>

namespace protojs {
namespace pathalg {

// The result of parse(): the five fields of Node's path object.
struct ParsedPath {
    std::string root;
    std::string dir;
    std::string base;
    std::string ext;
    std::string name;
};

// The input of format(). An empty string stands for any falsy JavaScript
// value, which is how Node's _format treats the fields.
struct FormatInput {
    std::string dir;
    std::string root;
    std::string base;
    std::string name;
    std::string ext;
};

// The environment resolve() needs, supplied by the caller so the algorithms
// stay lexical and testable.
struct ResolveEnv {
    // process.cwd(), in the host's native form.
    std::string cwd;
    // True when the host is Windows (Node's isWindows): selects how the cwd
    // is presented to the namespace of the other platform.
    bool hostIsWindows = false;
    // win32 only: process.env[`=${device}`], the per-drive current directory
    // Windows keeps for each drive letter ("" when not set).
    std::function<std::string(const std::string& device)> driveCwd;
};

namespace win32 {
std::string resolve(const std::vector<std::string>& args, const ResolveEnv& env);
std::string normalize(const std::string& path);
bool isAbsolute(const std::string& path);
std::string join(const std::vector<std::string>& args);
std::string relative(const std::string& from, const std::string& to, const ResolveEnv& env);
std::string toNamespacedPath(const std::string& path, const ResolveEnv& env);
std::string dirname(const std::string& path);
// hasSuffix=false is Node's `suffix === undefined`.
std::string basename(const std::string& path, bool hasSuffix, const std::string& suffix);
std::string extname(const std::string& path);
ParsedPath parse(const std::string& path);
std::string format(const FormatInput& in);
} // namespace win32

namespace posix {
std::string resolve(const std::vector<std::string>& args, const ResolveEnv& env);
std::string normalize(const std::string& path);
bool isAbsolute(const std::string& path);
std::string join(const std::vector<std::string>& args);
std::string relative(const std::string& from, const std::string& to, const ResolveEnv& env);
std::string toNamespacedPath(const std::string& path);
std::string dirname(const std::string& path);
std::string basename(const std::string& path, bool hasSuffix, const std::string& suffix);
std::string extname(const std::string& path);
ParsedPath parse(const std::string& path);
std::string format(const FormatInput& in);
} // namespace posix

} // namespace pathalg
} // namespace protojs

#endif // PROTOJS_PATH_ALGORITHMS_H
