/*
 * PathAlgorithms — a port of Node.js v22.20.0 lib/path.js. See
 * PathAlgorithms.h for the description and for Node's copyright notice,
 * which applies to this file as a derived work.
 *
 * The functions below keep the structure, the variable names and the
 * comments of Node's code so they can be compared with it line by line.
 * They operate on UTF-16 code units (std::u16string), as JavaScript strings
 * do: every index, length and slice means what it means in Node.
 */
#include "PathAlgorithms.h"

#include <cstdint>

// QuickJS's libunicode (deps/quickjs/libunicode.h), declared here rather
// than included: the header carries C-only inline helpers this file does not
// need. conv_type 0 maps to upper case, 1 to lower case; the result is up to
// LRE_CC_RES_LEN_MAX (3) code points, and the count is returned.
extern "C" int lre_case_conv(uint32_t* res, uint32_t c, int conv_type);

namespace protojs {
namespace pathalg {

namespace {

using U = std::u16string;
using Index = long long;

// --- UTF-8 <-> UTF-16 ------------------------------------------------------

// UTF-8 to UTF-16. Surrogates encoded on their own (as WTF-8 does) come back
// as the same code unit, so a JavaScript string survives the round trip;
// invalid bytes become U+FFFD.
U toU16(const std::string& s) {
    U out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        std::size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + len > n) { out.push_back(0xFFFD); ++i; continue; }
        bool ok = true;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string toU8(const U& s) {
    std::string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n; ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        appendUtf8(out, cp);
    }
    return out;
}

// --- JavaScript string primitives -------------------------------------------

Index len(const U& s) { return static_cast<Index>(s.size()); }

// String.prototype.charCodeAt: NaN (here -1, equal to no character) out of range.
int charCodeAt(const U& s, Index i) {
    if (i < 0 || i >= len(s)) return -1;
    return static_cast<int>(s[static_cast<std::size_t>(i)]);
}

// String.prototype.slice, with its handling of negative and missing bounds.
U slice(const U& s, Index start, Index end) {
    const Index n = len(s);
    if (start < 0) start = start + n < 0 ? 0 : start + n;
    if (end < 0) end = end + n < 0 ? 0 : end + n;
    if (start > n) start = n;
    if (end > n) end = n;
    if (start >= end) return U();
    return s.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}
U slice(const U& s, Index start) { return slice(s, start, len(s)); }

Index indexOf(const U& s, char16_t c, Index from = 0) {
    if (from < 0) from = 0;
    if (from >= len(s)) return -1;
    const std::size_t p = s.find(c, static_cast<std::size_t>(from));
    return p == U::npos ? -1 : static_cast<Index>(p);
}

Index lastIndexOf(const U& s, char16_t c) {
    const std::size_t p = s.rfind(c);
    return p == U::npos ? -1 : static_cast<Index>(p);
}

bool includes(const U& s, char16_t c) { return s.find(c) != U::npos; }

// String.prototype.toLowerCase / toUpperCase: the full Unicode mapping of
// QuickJS's libunicode (conv_type 1 = lower, 0 = upper), code point by code
// point. (The context-dependent final sigma is not applied; it cannot occur
// in the comparisons path.js makes.)
U caseConvert(const U& s, int convType) {
    U out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n; ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        uint32_t res[3];
        const int count = (cp >= 0xD800 && cp <= 0xDFFF) ? 0 : lre_case_conv(res, cp, convType);
        if (count <= 0) { res[0] = cp; }
        const int m = count <= 0 ? 1 : count;
        for (int k = 0; k < m; ++k) {
            uint32_t r = res[k];
            if (r >= 0x10000) {
                r -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (r >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (r & 0x3FF)));
            } else {
                out.push_back(static_cast<char16_t>(r));
            }
        }
    }
    return out;
}
U toLowerCase(const U& s) { return caseConvert(s, 1); }
U toUpperCase(const U& s) { return caseConvert(s, 0); }

U repeat(const U& s, Index count) {
    U out;
    for (Index i = 0; i < count; ++i) out += s;
    return out;
}

std::vector<U> split(const U& s, char16_t sep) {
    std::vector<U> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t p = s.find(sep, start);
        if (p == U::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

U joinParts(const std::vector<U>& parts, std::size_t from, const U& sep) {
    U out;
    for (std::size_t i = from; i < parts.size(); ++i) {
        if (i > from) out += sep;
        out += parts[i];
    }
    return out;
}

// --- Node's constants and helpers -------------------------------------------

constexpr int CHAR_UPPERCASE_A = 65;
constexpr int CHAR_LOWERCASE_A = 97;
constexpr int CHAR_UPPERCASE_Z = 90;
constexpr int CHAR_LOWERCASE_Z = 122;
constexpr int CHAR_DOT = 46;
constexpr int CHAR_FORWARD_SLASH = 47;
constexpr int CHAR_BACKWARD_SLASH = 92;
constexpr int CHAR_COLON = 58;
constexpr int CHAR_QUESTION_MARK = 63;

bool isPathSeparator(int code) {
    return code == CHAR_FORWARD_SLASH || code == CHAR_BACKWARD_SLASH;
}

bool isPosixPathSeparator(int code) {
    return code == CHAR_FORWARD_SLASH;
}

const U& reservedName(std::size_t i) {
    static const U names[] = {
        u"CON", u"PRN", u"AUX", u"NUL",
        u"COM1", u"COM2", u"COM3", u"COM4", u"COM5", u"COM6", u"COM7", u"COM8", u"COM9",
        u"LPT1", u"LPT2", u"LPT3", u"LPT4", u"LPT5", u"LPT6", u"LPT7", u"LPT8", u"LPT9",
        u"COM\u00b9", u"COM\u00b2", u"COM\u00b3",
        u"LPT\u00b9", u"LPT\u00b2", u"LPT\u00b3",
    };
    return names[i];
}
constexpr std::size_t kReservedNameCount = 28;

// isWindowsReservedName(path, colonIndex). StringPrototypeSlice(path, 0, -1)
// (no colon) is everything but the last character, as in JavaScript.
bool isWindowsReservedName(const U& path, Index colonIndex) {
    const U devicePart = toUpperCase(slice(path, 0, colonIndex));
    for (std::size_t i = 0; i < kReservedNameCount; ++i) {
        if (devicePart == reservedName(i)) return true;
    }
    return false;
}

bool isWindowsDeviceRoot(int code) {
    return (code >= CHAR_UPPERCASE_A && code <= CHAR_UPPERCASE_Z) ||
           (code >= CHAR_LOWERCASE_A && code <= CHAR_LOWERCASE_Z);
}

// Resolves . and .. elements in a path with directory names
U normalizeString(const U& path, bool allowAboveRoot, char16_t separator,
                  bool (*isSep)(int)) {
    U res;
    Index lastSegmentLength = 0;
    Index lastSlash = -1;
    int dots = 0;
    int code = 0;
    for (Index i = 0; i <= len(path); ++i) {
        if (i < len(path))
            code = charCodeAt(path, i);
        else if (isSep(code))
            break;
        else
            code = CHAR_FORWARD_SLASH;

        if (isSep(code)) {
            if (lastSlash == i - 1 || dots == 1) {
                // NOOP
            } else if (dots == 2) {
                if (len(res) < 2 || lastSegmentLength != 2 ||
                    charCodeAt(res, len(res) - 1) != CHAR_DOT ||
                    charCodeAt(res, len(res) - 2) != CHAR_DOT) {
                    if (len(res) > 2) {
                        const Index lastSlashIndex = len(res) - lastSegmentLength - 1;
                        if (lastSlashIndex == -1) {
                            res.clear();
                            lastSegmentLength = 0;
                        } else {
                            res = slice(res, 0, lastSlashIndex);
                            lastSegmentLength = len(res) - 1 - lastIndexOf(res, separator);
                        }
                        lastSlash = i;
                        dots = 0;
                        continue;
                    } else if (len(res) != 0) {
                        res.clear();
                        lastSegmentLength = 0;
                        lastSlash = i;
                        dots = 0;
                        continue;
                    }
                }
                if (allowAboveRoot) {
                    if (len(res) > 0) { res.push_back(separator); res += u".."; }
                    else res = u"..";
                    lastSegmentLength = 2;
                }
            } else {
                if (len(res) > 0) {
                    res.push_back(separator);
                    res += slice(path, lastSlash + 1, i);
                } else {
                    res = slice(path, lastSlash + 1, i);
                }
                lastSegmentLength = i - lastSlash - 1;
            }
            lastSlash = i;
            dots = 0;
        } else if (code == CHAR_DOT && dots != -1) {
            ++dots;
        } else {
            dots = -1;
        }
    }
    return res;
}

U formatExt(const U& ext) {
    if (ext.empty()) return U();
    return (ext[0] == u'.' ? U() : U(u".")) + ext;
}

std::string formatWith(char16_t sep, const FormatInput& in) {
    const U root = toU16(in.root);
    const U dir = in.dir.empty() ? root : toU16(in.dir);
    const U base = !in.base.empty() ? toU16(in.base) : toU16(in.name) + formatExt(toU16(in.ext));
    if (dir.empty()) return toU8(base);
    if (dir == root) return toU8(dir + base);
    U out = dir;
    out.push_back(sep);
    out += base;
    return toU8(out);
}

// --- win32 -----------------------------------------------------------------

U win32Resolve(const std::vector<U>& args, const ResolveEnv& env) {
    U resolvedDevice;
    U resolvedTail;
    bool resolvedAbsolute = false;

    for (Index i = static_cast<Index>(args.size()) - 1; i >= -1; i--) {
        U path;
        if (i >= 0) {
            path = args[static_cast<std::size_t>(i)];
            // Skip empty entries
            if (len(path) == 0) continue;
        } else if (len(resolvedDevice) == 0) {
            path = toU16(env.cwd);
            // Fast path for current directory
            if (args.empty() ||
                ((args.size() == 1 && (args[0].empty() || args[0] == u".")) &&
                 isPathSeparator(charCodeAt(path, 0)))) {
                if (!env.hostIsWindows) {
                    for (char16_t& c : path) if (c == u'/') c = u'\\';
                }
                return path;
            }
        } else {
            // Windows has the concept of drive-specific current working
            // directories. If we've resolved a drive letter but not yet an
            // absolute path, get cwd for that drive, or the process cwd if
            // the drive cwd is not available. We're sure the device is not
            // a UNC path at this points, because UNC paths are always absolute.
            std::string driveCwd = env.driveCwd ? env.driveCwd(toU8(resolvedDevice)) : std::string();
            path = toU16(driveCwd.empty() ? env.cwd : driveCwd);

            // Verify that a cwd was found and that it actually points
            // to our drive. If not, default to the drive's root.
            if (toLowerCase(slice(path, 0, 2)) != toLowerCase(resolvedDevice) &&
                charCodeAt(path, 2) == CHAR_BACKWARD_SLASH) {
                path = resolvedDevice + u"\\";
            }
        }

        const Index plen = len(path);
        Index rootEnd = 0;
        U device;
        bool isAbsolute = false;
        const int code = charCodeAt(path, 0);

        // Try to match a root
        if (plen == 1) {
            if (isPathSeparator(code)) {
                // `path` contains just a path separator
                rootEnd = 1;
                isAbsolute = true;
            }
        } else if (isPathSeparator(code)) {
            // Possible UNC root

            // If we started with a separator, we know we at least have an
            // absolute path of some kind (UNC or otherwise)
            isAbsolute = true;

            if (isPathSeparator(charCodeAt(path, 1))) {
                // Matched double path separator at beginning
                Index j = 2;
                Index last = j;
                // Match 1 or more non-path separators
                while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
                if (j < plen && j != last) {
                    const U firstPart = slice(path, last, j);
                    // Matched!
                    last = j;
                    // Match 1 or more path separators
                    while (j < plen && isPathSeparator(charCodeAt(path, j))) j++;
                    if (j < plen && j != last) {
                        // Matched!
                        last = j;
                        // Match 1 or more non-path separators
                        while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
                        if (j == plen || j != last) {
                            if (firstPart != u"." && firstPart != u"?") {
                                // We matched a UNC root
                                device = U(u"\\\\") + firstPart + u"\\" + slice(path, last, j);
                                rootEnd = j;
                            } else {
                                // We matched a device root (e.g. \\\\.\\PHYSICALDRIVE0)
                                device = U(u"\\\\") + firstPart;
                                rootEnd = 4;
                            }
                        }
                    }
                }
            } else {
                rootEnd = 1;
            }
        } else if (isWindowsDeviceRoot(code) && charCodeAt(path, 1) == CHAR_COLON) {
            // Possible device root
            device = slice(path, 0, 2);
            rootEnd = 2;
            if (plen > 2 && isPathSeparator(charCodeAt(path, 2))) {
                // Treat separator following drive name as an absolute path
                // indicator
                isAbsolute = true;
                rootEnd = 3;
            }
        }

        if (len(device) > 0) {
            if (len(resolvedDevice) > 0) {
                if (toLowerCase(device) != toLowerCase(resolvedDevice))
                    // This path points to another device so it is not applicable
                    continue;
            } else {
                resolvedDevice = device;
            }
        }

        if (resolvedAbsolute) {
            if (len(resolvedDevice) > 0) break;
        } else {
            resolvedTail = slice(path, rootEnd) + u"\\" + resolvedTail;
            resolvedAbsolute = isAbsolute;
            if (isAbsolute && len(resolvedDevice) > 0) break;
        }
    }

    // At this point the path should be resolved to a full absolute path,
    // but handle relative paths to be safe (might happen when process.cwd()
    // fails)

    // Normalize the tail path
    resolvedTail = normalizeString(resolvedTail, !resolvedAbsolute, u'\\', isPathSeparator);

    if (resolvedAbsolute) return resolvedDevice + u"\\" + resolvedTail;
    U r = resolvedDevice + resolvedTail;
    return r.empty() ? U(u".") : r;
}

U win32Normalize(const U& path) {
    const Index plen = len(path);
    if (plen == 0) return u".";
    Index rootEnd = 0;
    bool hasDevice = false;   // device !== undefined
    U device;
    bool isAbsolute = false;
    const int code = charCodeAt(path, 0);

    // Try to match a root
    if (plen == 1) {
        // `path` contains just a single char, exit early to avoid
        // unnecessary work
        return isPosixPathSeparator(code) ? U(u"\\") : path;
    }
    if (isPathSeparator(code)) {
        // Possible UNC root

        // If we started with a separator, we know we at least have an absolute
        // path of some kind (UNC or otherwise)
        isAbsolute = true;

        if (isPathSeparator(charCodeAt(path, 1))) {
            // Matched double path separator at beginning
            Index j = 2;
            Index last = j;
            // Match 1 or more non-path separators
            while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
            if (j < plen && j != last) {
                const U firstPart = slice(path, last, j);
                // Matched!
                last = j;
                // Match 1 or more path separators
                while (j < plen && isPathSeparator(charCodeAt(path, j))) j++;
                if (j < plen && j != last) {
                    // Matched!
                    last = j;
                    // Match 1 or more non-path separators
                    while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
                    if (j == plen || j != last) {
                        if (firstPart == u"." || firstPart == u"?") {
                            // We matched a device root (e.g. \\\\.\\PHYSICALDRIVE0)
                            device = U(u"\\\\") + firstPart;
                            hasDevice = true;
                            rootEnd = 4;
                            const Index colonIndex = indexOf(path, u':');
                            // Special case: handle \\?\COM1: or similar reserved device paths
                            const U possibleDevice = slice(path, 4, colonIndex + 1);
                            if (isWindowsReservedName(possibleDevice, len(possibleDevice) - 1)) {
                                device = U(u"\\\\?\\") + possibleDevice;
                                rootEnd = 4 + len(possibleDevice);
                            }
                        } else if (j == plen) {
                            // We matched a UNC root only
                            // Return the normalized version of the UNC root since there
                            // is nothing left to process
                            return U(u"\\\\") + firstPart + u"\\" + slice(path, last) + u"\\";
                        } else {
                            // We matched a UNC root with leftovers
                            device = U(u"\\\\") + firstPart + u"\\" + slice(path, last, j);
                            hasDevice = true;
                            rootEnd = j;
                        }
                    }
                }
            }
        } else {
            rootEnd = 1;
        }
    } else {
        const Index colonIndex = indexOf(path, u':');
        if (colonIndex > 0) {
            if (isWindowsDeviceRoot(code) && colonIndex == 1) {
                device = slice(path, 0, 2);
                hasDevice = true;
                rootEnd = 2;
                if (plen > 2 && isPathSeparator(charCodeAt(path, 2))) {
                    isAbsolute = true;
                    rootEnd = 3;
                }
            } else if (isWindowsReservedName(path, colonIndex)) {
                device = slice(path, 0, colonIndex + 1);
                hasDevice = true;
                rootEnd = colonIndex + 1;
            }
        }
    }

    U tail = rootEnd < plen
        ? normalizeString(slice(path, rootEnd), !isAbsolute, u'\\', isPathSeparator)
        : U();
    if (len(tail) == 0 && !isAbsolute) tail = u".";
    if (len(tail) > 0 && isPathSeparator(charCodeAt(path, plen - 1))) tail += u"\\";
    if (!isAbsolute && !hasDevice && includes(path, u':')) {
        // If the original path was not absolute and if we have not been able
        // to resolve it relative to a particular device, we need to ensure that
        // the `tail` has not become something that Windows might interpret as
        // an absolute path. See CVE-2024-36139.
        if (len(tail) >= 2 && isWindowsDeviceRoot(charCodeAt(tail, 0)) &&
            charCodeAt(tail, 1) == CHAR_COLON) {
            return U(u".\\") + tail;
        }
        Index index = indexOf(path, u':');

        do {
            if (index == plen - 1 || isPathSeparator(charCodeAt(path, index + 1))) {
                return U(u".\\") + tail;
            }
        } while ((index = indexOf(path, u':', index + 1)) != -1);
    }
    const Index colonIndex = indexOf(path, u':');
    if (isWindowsReservedName(path, colonIndex)) {
        return U(u".\\") + (hasDevice ? device : U()) + tail;
    }
    if (!hasDevice) return isAbsolute ? U(u"\\") + tail : tail;
    return isAbsolute ? device + u"\\" + tail : device + tail;
}

bool win32IsAbsolute(const U& path) {
    const Index plen = len(path);
    if (plen == 0) return false;
    const int code = charCodeAt(path, 0);
    return isPathSeparator(code) ||
           // Possible device root
           (plen > 2 && isWindowsDeviceRoot(code) && charCodeAt(path, 1) == CHAR_COLON &&
            isPathSeparator(charCodeAt(path, 2)));
}

U win32Join(const std::vector<U>& args) {
    if (args.empty()) return u".";

    std::vector<U> path;
    for (const U& arg : args) {
        if (len(arg) > 0) path.push_back(arg);
    }

    if (path.empty()) return u".";

    const U& firstPart = path[0];
    U joined = joinParts(path, 0, u"\\");

    // Make sure that the joined path doesn't start with two slashes, because
    // normalize() will mistake it for a UNC path then.
    //
    // This step is skipped when it is very clear that the user actually
    // intended to point at a UNC path. This is assumed when the first
    // non-empty string arguments starts with exactly two slashes followed by
    // at least one more non-slash character.
    //
    // Note that for normalize() to treat a path as a UNC path it needs to
    // have at least 2 components, so we don't filter for that here.
    // This means that the user can use join to construct UNC paths from
    // a server name and a share name; for example:
    //   path.join('//server', 'share') -> '\\\\server\\share\\')
    bool needsReplace = true;
    Index slashCount = 0;
    if (isPathSeparator(charCodeAt(firstPart, 0))) {
        ++slashCount;
        const Index firstLen = len(firstPart);
        if (firstLen > 1 && isPathSeparator(charCodeAt(firstPart, 1))) {
            ++slashCount;
            if (firstLen > 2) {
                if (isPathSeparator(charCodeAt(firstPart, 2)))
                    ++slashCount;
                else {
                    // We matched a UNC path in the first part
                    needsReplace = false;
                }
            }
        }
    }
    if (needsReplace) {
        // Find any more consecutive slashes we need to replace
        while (slashCount < len(joined) && isPathSeparator(charCodeAt(joined, slashCount))) {
            slashCount++;
        }

        // Replace the slashes if needed
        if (slashCount >= 2) joined = U(u"\\") + slice(joined, slashCount);
    }

    // Skip normalization when reserved device names are present
    std::vector<U> parts;
    U part;

    for (Index i = 0; i < len(joined); i++) {
        if (joined[static_cast<std::size_t>(i)] == u'\\') {
            if (!part.empty()) parts.push_back(part);
            part.clear();
            // Skip consecutive backslashes
            while (i + 1 < len(joined) && joined[static_cast<std::size_t>(i + 1)] == u'\\') i++;
        } else {
            part.push_back(joined[static_cast<std::size_t>(i)]);
        }
    }
    // Add the final part if any
    if (!part.empty()) parts.push_back(part);

    // Check if any part has a Windows reserved name
    bool reserved = false;
    for (const U& p : parts) {
        const Index colonIndex = indexOf(p, u':');
        if (colonIndex != -1 && isWindowsReservedName(p, colonIndex)) { reserved = true; break; }
    }
    if (reserved) {
        // Replace forward slashes with backslashes
        U result = joined;
        for (char16_t& c : result) if (c == u'/') c = u'\\';
        return result;
    }

    return win32Normalize(joined);
}

U win32Relative(const U& fromIn, const U& toIn, const ResolveEnv& env) {
    if (fromIn == toIn) return U();

    const U fromOrig = win32Resolve({fromIn}, env);
    const U toOrig = win32Resolve({toIn}, env);

    if (fromOrig == toOrig) return U();

    const U from = toLowerCase(fromOrig);
    const U to = toLowerCase(toOrig);

    if (from == to) return U();

    if (len(fromOrig) != len(from) || len(toOrig) != len(to)) {
        std::vector<U> fromSplit = split(fromOrig, u'\\');
        std::vector<U> toSplit = split(toOrig, u'\\');
        if (fromSplit.back().empty()) fromSplit.pop_back();
        if (toSplit.back().empty()) toSplit.pop_back();

        const Index fromLen = static_cast<Index>(fromSplit.size());
        const Index toLen = static_cast<Index>(toSplit.size());
        const Index length = fromLen < toLen ? fromLen : toLen;

        Index i;
        for (i = 0; i < length; i++) {
            if (toLowerCase(fromSplit[static_cast<std::size_t>(i)]) !=
                toLowerCase(toSplit[static_cast<std::size_t>(i)])) {
                break;
            }
        }

        if (i == 0) {
            return toOrig;
        } else if (i == length) {
            if (toLen > length) return joinParts(toSplit, static_cast<std::size_t>(i), u"\\");
            if (fromLen > length) return repeat(u"..\\", fromLen - 1 - i) + u"..";
            return U();
        }

        return repeat(u"..\\", fromLen - i) + joinParts(toSplit, static_cast<std::size_t>(i), u"\\");
    }

    // Trim any leading backslashes
    Index fromStart = 0;
    while (fromStart < len(from) && charCodeAt(from, fromStart) == CHAR_BACKWARD_SLASH) fromStart++;
    // Trim trailing backslashes (applicable to UNC paths only)
    Index fromEnd = len(from);
    while (fromEnd - 1 > fromStart && charCodeAt(from, fromEnd - 1) == CHAR_BACKWARD_SLASH) fromEnd--;
    const Index fromLen = fromEnd - fromStart;

    // Trim any leading backslashes
    Index toStart = 0;
    while (toStart < len(to) && charCodeAt(to, toStart) == CHAR_BACKWARD_SLASH) toStart++;
    // Trim trailing backslashes (applicable to UNC paths only)
    Index toEnd = len(to);
    while (toEnd - 1 > toStart && charCodeAt(to, toEnd - 1) == CHAR_BACKWARD_SLASH) toEnd--;
    const Index toLen = toEnd - toStart;

    // Compare paths to find the longest common path from root
    const Index length = fromLen < toLen ? fromLen : toLen;
    Index lastCommonSep = -1;
    Index i = 0;
    for (; i < length; i++) {
        const int fromCode = charCodeAt(from, fromStart + i);
        if (fromCode != charCodeAt(to, toStart + i))
            break;
        else if (fromCode == CHAR_BACKWARD_SLASH)
            lastCommonSep = i;
    }

    // We found a mismatch before the first common path separator was seen, so
    // return the original `to`.
    if (i != length) {
        if (lastCommonSep == -1) return toOrig;
    } else {
        if (toLen > length) {
            if (charCodeAt(to, toStart + i) == CHAR_BACKWARD_SLASH) {
                // We get here if `from` is the exact base path for `to`.
                // For example: from='C:\\foo\\bar'; to='C:\\foo\\bar\\baz'
                return slice(toOrig, toStart + i + 1);
            }
            if (i == 2) {
                // We get here if `from` is the device root.
                // For example: from='C:\\'; to='C:\\foo'
                return slice(toOrig, toStart + i);
            }
        }
        if (fromLen > length) {
            if (charCodeAt(from, fromStart + i) == CHAR_BACKWARD_SLASH) {
                // We get here if `to` is the exact base path for `from`.
                // For example: from='C:\\foo\\bar'; to='C:\\foo'
                lastCommonSep = i;
            } else if (i == 2) {
                // We get here if `to` is the device root.
                // For example: from='C:\\foo\\bar'; to='C:\\'
                lastCommonSep = 3;
            }
        }
        if (lastCommonSep == -1) lastCommonSep = 0;
    }

    U out;
    // Generate the relative path based on the path difference between `to` and
    // `from`
    for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
        if (i == fromEnd || charCodeAt(from, i) == CHAR_BACKWARD_SLASH) {
            out += out.empty() ? U(u"..") : U(u"\\..");
        }
    }

    toStart += lastCommonSep;

    // Lastly, append the rest of the destination (`to`) path that comes after
    // the common path parts
    if (len(out) > 0) return out + slice(toOrig, toStart, toEnd);

    if (charCodeAt(toOrig, toStart) == CHAR_BACKWARD_SLASH) ++toStart;
    return slice(toOrig, toStart, toEnd);
}

U win32ToNamespacedPath(const U& path, const ResolveEnv& env) {
    // Note: this will *probably* throw somewhere.
    if (len(path) == 0) return path;

    const U resolvedPath = win32Resolve({path}, env);

    if (len(resolvedPath) <= 2) return path;

    if (charCodeAt(resolvedPath, 0) == CHAR_BACKWARD_SLASH) {
        // Possible UNC root
        if (charCodeAt(resolvedPath, 1) == CHAR_BACKWARD_SLASH) {
            const int code = charCodeAt(resolvedPath, 2);
            if (code != CHAR_QUESTION_MARK && code != CHAR_DOT) {
                // Matched non-long UNC root, convert the path to a long UNC path
                return U(u"\\\\?\\UNC\\") + slice(resolvedPath, 2);
            }
        }
    } else if (isWindowsDeviceRoot(charCodeAt(resolvedPath, 0)) &&
               charCodeAt(resolvedPath, 1) == CHAR_COLON &&
               charCodeAt(resolvedPath, 2) == CHAR_BACKWARD_SLASH) {
        // Matched device root, convert the path to a long UNC path
        return U(u"\\\\?\\") + resolvedPath;
    }

    return resolvedPath;
}

U win32Dirname(const U& path) {
    const Index plen = len(path);
    if (plen == 0) return u".";
    Index rootEnd = -1;
    Index offset = 0;
    const int code = charCodeAt(path, 0);

    if (plen == 1) {
        // `path` contains just a path separator, exit early to avoid
        // unnecessary work or a dot.
        return isPathSeparator(code) ? path : U(u".");
    }

    // Try to match a root
    if (isPathSeparator(code)) {
        // Possible UNC root

        rootEnd = offset = 1;

        if (isPathSeparator(charCodeAt(path, 1))) {
            // Matched double path separator at beginning
            Index j = 2;
            Index last = j;
            // Match 1 or more non-path separators
            while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
            if (j < plen && j != last) {
                // Matched!
                last = j;
                // Match 1 or more path separators
                while (j < plen && isPathSeparator(charCodeAt(path, j))) j++;
                if (j < plen && j != last) {
                    // Matched!
                    last = j;
                    // Match 1 or more non-path separators
                    while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
                    if (j == plen) {
                        // We matched a UNC root only
                        return path;
                    }
                    if (j != last) {
                        // We matched a UNC root with leftovers

                        // Offset by 1 to include the separator after the UNC root to
                        // treat it as a "normal root" on top of a (UNC) root
                        rootEnd = offset = j + 1;
                    }
                }
            }
        }
        // Possible device root
    } else if (isWindowsDeviceRoot(code) && charCodeAt(path, 1) == CHAR_COLON) {
        rootEnd = plen > 2 && isPathSeparator(charCodeAt(path, 2)) ? 3 : 2;
        offset = rootEnd;
    }

    Index end = -1;
    bool matchedSlash = true;
    for (Index i = plen - 1; i >= offset; --i) {
        if (isPathSeparator(charCodeAt(path, i))) {
            if (!matchedSlash) {
                end = i;
                break;
            }
        } else {
            // We saw the first non-path separator
            matchedSlash = false;
        }
    }

    if (end == -1) {
        if (rootEnd == -1) return u".";
        end = rootEnd;
    }
    return slice(path, 0, end);
}

U win32Basename(const U& path, bool hasSuffix, const U& suffix) {
    Index start = 0;
    Index end = -1;
    bool matchedSlash = true;

    // Check for a drive letter prefix so as not to mistake the following
    // path separator as an extra separator at the end of the path that can be
    // disregarded
    if (len(path) >= 2 && isWindowsDeviceRoot(charCodeAt(path, 0)) &&
        charCodeAt(path, 1) == CHAR_COLON) {
        start = 2;
    }

    if (hasSuffix && len(suffix) > 0 && len(suffix) <= len(path)) {
        if (suffix == path) return U();
        Index extIdx = len(suffix) - 1;
        Index firstNonSlashEnd = -1;
        for (Index i = len(path) - 1; i >= start; --i) {
            const int code = charCodeAt(path, i);
            if (isPathSeparator(code)) {
                // If we reached a path separator that was not part of a set of path
                // separators at the end of the string, stop now
                if (!matchedSlash) {
                    start = i + 1;
                    break;
                }
            } else {
                if (firstNonSlashEnd == -1) {
                    // We saw the first non-path separator, remember this index in case
                    // we need it if the extension ends up not matching
                    matchedSlash = false;
                    firstNonSlashEnd = i + 1;
                }
                if (extIdx >= 0) {
                    // Try to match the explicit extension
                    if (code == charCodeAt(suffix, extIdx)) {
                        if (--extIdx == -1) {
                            // We matched the extension, so mark this as the end of our path
                            // component
                            end = i;
                        }
                    } else {
                        // Extension does not match, so our result is the entire path
                        // component
                        extIdx = -1;
                        end = firstNonSlashEnd;
                    }
                }
            }
        }

        if (start == end)
            end = firstNonSlashEnd;
        else if (end == -1)
            end = len(path);
        return slice(path, start, end);
    }
    for (Index i = len(path) - 1; i >= start; --i) {
        if (isPathSeparator(charCodeAt(path, i))) {
            // If we reached a path separator that was not part of a set of path
            // separators at the end of the string, stop now
            if (!matchedSlash) {
                start = i + 1;
                break;
            }
        } else if (end == -1) {
            // We saw the first non-path separator, mark this as the end of our
            // path component
            matchedSlash = false;
            end = i + 1;
        }
    }

    if (end == -1) return U();
    return slice(path, start, end);
}

U win32Extname(const U& path) {
    Index start = 0;
    Index startDot = -1;
    Index startPart = 0;
    Index end = -1;
    bool matchedSlash = true;
    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    int preDotState = 0;

    // Check for a drive letter prefix so as not to mistake the following
    // path separator as an extra separator at the end of the path that can be
    // disregarded

    if (len(path) >= 2 && charCodeAt(path, 1) == CHAR_COLON &&
        isWindowsDeviceRoot(charCodeAt(path, 0))) {
        start = startPart = 2;
    }

    for (Index i = len(path) - 1; i >= start; --i) {
        const int code = charCodeAt(path, i);
        if (isPathSeparator(code)) {
            // If we reached a path separator that was not part of a set of path
            // separators at the end of the string, stop now
            if (!matchedSlash) {
                startPart = i + 1;
                break;
            }
            continue;
        }
        if (end == -1) {
            // We saw the first non-path separator, mark this as the end of our
            // extension
            matchedSlash = false;
            end = i + 1;
        }
        if (code == CHAR_DOT) {
            // If this is our first dot, mark it as the start of our extension
            if (startDot == -1)
                startDot = i;
            else if (preDotState != 1)
                preDotState = 1;
        } else if (startDot != -1) {
            // We saw a non-dot and non-path separator before our dot, so we should
            // have a good chance at having a non-empty extension
            preDotState = -1;
        }
    }

    if (startDot == -1 || end == -1 ||
        // We saw a non-dot character immediately before the dot
        preDotState == 0 ||
        // The (right-most) trimmed path component is exactly '..'
        (preDotState == 1 && startDot == end - 1 && startDot == startPart + 1)) {
        return U();
    }
    return slice(path, startDot, end);
}

struct ParsedU {
    U root, dir, base, ext, name;
};

ParsedU win32Parse(const U& path) {
    ParsedU ret;
    if (len(path) == 0) return ret;

    const Index plen = len(path);
    Index rootEnd = 0;
    int code = charCodeAt(path, 0);

    if (plen == 1) {
        if (isPathSeparator(code)) {
            // `path` contains just a path separator, exit early to avoid
            // unnecessary work
            ret.root = ret.dir = path;
            return ret;
        }
        ret.base = ret.name = path;
        return ret;
    }
    // Try to match a root
    if (isPathSeparator(code)) {
        // Possible UNC root

        rootEnd = 1;
        if (isPathSeparator(charCodeAt(path, 1))) {
            // Matched double path separator at beginning
            Index j = 2;
            Index last = j;
            // Match 1 or more non-path separators
            while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
            if (j < plen && j != last) {
                // Matched!
                last = j;
                // Match 1 or more path separators
                while (j < plen && isPathSeparator(charCodeAt(path, j))) j++;
                if (j < plen && j != last) {
                    // Matched!
                    last = j;
                    // Match 1 or more non-path separators
                    while (j < plen && !isPathSeparator(charCodeAt(path, j))) j++;
                    if (j == plen) {
                        // We matched a UNC root only
                        rootEnd = j;
                    } else if (j != last) {
                        // We matched a UNC root with leftovers
                        rootEnd = j + 1;
                    }
                }
            }
        }
    } else if (isWindowsDeviceRoot(code) && charCodeAt(path, 1) == CHAR_COLON) {
        // Possible device root
        if (plen <= 2) {
            // `path` contains just a drive root, exit early to avoid
            // unnecessary work
            ret.root = ret.dir = path;
            return ret;
        }
        rootEnd = 2;
        if (isPathSeparator(charCodeAt(path, 2))) {
            if (plen == 3) {
                // `path` contains just a drive root, exit early to avoid
                // unnecessary work
                ret.root = ret.dir = path;
                return ret;
            }
            rootEnd = 3;
        }
    }
    if (rootEnd > 0) ret.root = slice(path, 0, rootEnd);

    Index startDot = -1;
    Index startPart = rootEnd;
    Index end = -1;
    bool matchedSlash = true;
    Index i = len(path) - 1;

    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    int preDotState = 0;

    // Get non-dir info
    for (; i >= rootEnd; --i) {
        code = charCodeAt(path, i);
        if (isPathSeparator(code)) {
            // If we reached a path separator that was not part of a set of path
            // separators at the end of the string, stop now
            if (!matchedSlash) {
                startPart = i + 1;
                break;
            }
            continue;
        }
        if (end == -1) {
            // We saw the first non-path separator, mark this as the end of our
            // extension
            matchedSlash = false;
            end = i + 1;
        }
        if (code == CHAR_DOT) {
            // If this is our first dot, mark it as the start of our extension
            if (startDot == -1)
                startDot = i;
            else if (preDotState != 1)
                preDotState = 1;
        } else if (startDot != -1) {
            // We saw a non-dot and non-path separator before our dot, so we should
            // have a good chance at having a non-empty extension
            preDotState = -1;
        }
    }

    if (end != -1) {
        if (startDot == -1 ||
            // We saw a non-dot character immediately before the dot
            preDotState == 0 ||
            // The (right-most) trimmed path component is exactly '..'
            (preDotState == 1 && startDot == end - 1 && startDot == startPart + 1)) {
            ret.base = ret.name = slice(path, startPart, end);
        } else {
            ret.name = slice(path, startPart, startDot);
            ret.base = slice(path, startPart, end);
            ret.ext = slice(path, startDot, end);
        }
    }

    // If the directory is the root, use the entire root as the `dir` including
    // the trailing slash if any (`C:\abc` -> `C:\`). Otherwise, strip out the
    // trailing slash (`C:\abc\def` -> `C:\abc`).
    if (startPart > 0 && startPart != rootEnd)
        ret.dir = slice(path, 0, startPart - 1);
    else
        ret.dir = ret.root;

    return ret;
}

// --- posix -------------------------------------------------------------------

// posixCwd(): process.cwd() in posix form. On Windows Node turns the
// backslashes into slashes and drops the drive ("C:/x" -> "/x").
U posixCwd(const ResolveEnv& env) {
    U cwd = toU16(env.cwd);
    if (env.hostIsWindows) {
        for (char16_t& c : cwd) if (c == u'\\') c = u'/';
        return slice(cwd, indexOf(cwd, u'/'));
    }
    return cwd;
}

U posixResolve(const std::vector<U>& args, const ResolveEnv& env) {
    if (args.empty() || (args.size() == 1 && (args[0].empty() || args[0] == u"."))) {
        const U cwd = posixCwd(env);
        if (charCodeAt(cwd, 0) == CHAR_FORWARD_SLASH) return cwd;
    }
    U resolvedPath;
    bool resolvedAbsolute = false;

    for (Index i = static_cast<Index>(args.size()) - 1; i >= 0 && !resolvedAbsolute; i--) {
        const U& path = args[static_cast<std::size_t>(i)];

        // Skip empty entries
        if (len(path) == 0) continue;

        resolvedPath = path + u"/" + resolvedPath;
        resolvedAbsolute = charCodeAt(path, 0) == CHAR_FORWARD_SLASH;
    }

    if (!resolvedAbsolute) {
        const U cwd = posixCwd(env);
        resolvedPath = cwd + u"/" + resolvedPath;
        resolvedAbsolute = charCodeAt(cwd, 0) == CHAR_FORWARD_SLASH;
    }

    // At this point the path should be resolved to a full absolute path, but
    // handle relative paths to be safe (might happen when process.cwd() fails)

    // Normalize the path
    resolvedPath = normalizeString(resolvedPath, !resolvedAbsolute, u'/', isPosixPathSeparator);

    if (resolvedAbsolute) return U(u"/") + resolvedPath;
    return len(resolvedPath) > 0 ? resolvedPath : U(u".");
}

U posixNormalize(const U& pathIn) {
    if (len(pathIn) == 0) return u".";

    const bool isAbsolute = charCodeAt(pathIn, 0) == CHAR_FORWARD_SLASH;
    const bool trailingSeparator = charCodeAt(pathIn, len(pathIn) - 1) == CHAR_FORWARD_SLASH;

    // Normalize the path
    U path = normalizeString(pathIn, !isAbsolute, u'/', isPosixPathSeparator);

    if (len(path) == 0) {
        if (isAbsolute) return u"/";
        return trailingSeparator ? U(u"./") : U(u".");
    }
    if (trailingSeparator) path += u"/";

    return isAbsolute ? U(u"/") + path : path;
}

bool posixIsAbsolute(const U& path) {
    return len(path) > 0 && charCodeAt(path, 0) == CHAR_FORWARD_SLASH;
}

U posixJoin(const std::vector<U>& args) {
    if (args.empty()) return u".";

    std::vector<U> path;
    for (const U& arg : args) {
        if (len(arg) > 0) path.push_back(arg);
    }

    if (path.empty()) return u".";

    return posixNormalize(joinParts(path, 0, u"/"));
}

U posixRelative(const U& fromIn, const U& toIn, const ResolveEnv& env) {
    if (fromIn == toIn) return U();

    // Trim leading forward slashes.
    const U from = posixResolve({fromIn}, env);
    const U to = posixResolve({toIn}, env);

    if (from == to) return U();

    const Index fromStart = 1;
    const Index fromEnd = len(from);
    const Index fromLen = fromEnd - fromStart;
    const Index toStart = 1;
    const Index toLen = len(to) - toStart;

    // Compare paths to find the longest common path from root
    const Index length = (fromLen < toLen ? fromLen : toLen);
    Index lastCommonSep = -1;
    Index i = 0;
    for (; i < length; i++) {
        const int fromCode = charCodeAt(from, fromStart + i);
        if (fromCode != charCodeAt(to, toStart + i))
            break;
        else if (fromCode == CHAR_FORWARD_SLASH)
            lastCommonSep = i;
    }
    if (i == length) {
        if (toLen > length) {
            if (charCodeAt(to, toStart + i) == CHAR_FORWARD_SLASH) {
                // We get here if `from` is the exact base path for `to`.
                // For example: from='/foo/bar'; to='/foo/bar/baz'
                return slice(to, toStart + i + 1);
            }
            if (i == 0) {
                // We get here if `from` is the root
                // For example: from='/'; to='/foo'
                return slice(to, toStart + i);
            }
        } else if (fromLen > length) {
            if (charCodeAt(from, fromStart + i) == CHAR_FORWARD_SLASH) {
                // We get here if `to` is the exact base path for `from`.
                // For example: from='/foo/bar/baz'; to='/foo/bar'
                lastCommonSep = i;
            } else if (i == 0) {
                // We get here if `to` is the root.
                // For example: from='/foo/bar'; to='/'
                lastCommonSep = 0;
            }
        }
    }

    U out;
    // Generate the relative path based on the path difference between `to`
    // and `from`.
    for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
        if (i == fromEnd || charCodeAt(from, i) == CHAR_FORWARD_SLASH) {
            out += out.empty() ? U(u"..") : U(u"/..");
        }
    }

    // Lastly, append the rest of the destination (`to`) path that comes after
    // the common path parts.
    return out + slice(to, toStart + lastCommonSep);
}

U posixDirname(const U& path) {
    if (len(path) == 0) return u".";
    const bool hasRoot = charCodeAt(path, 0) == CHAR_FORWARD_SLASH;
    Index end = -1;
    bool matchedSlash = true;
    for (Index i = len(path) - 1; i >= 1; --i) {
        if (charCodeAt(path, i) == CHAR_FORWARD_SLASH) {
            if (!matchedSlash) {
                end = i;
                break;
            }
        } else {
            // We saw the first non-path separator
            matchedSlash = false;
        }
    }

    if (end == -1) return hasRoot ? U(u"/") : U(u".");
    if (hasRoot && end == 1) return u"//";
    return slice(path, 0, end);
}

U posixBasename(const U& path, bool hasSuffix, const U& suffix) {
    Index start = 0;
    Index end = -1;
    bool matchedSlash = true;

    if (hasSuffix && len(suffix) > 0 && len(suffix) <= len(path)) {
        if (suffix == path) return U();
        Index extIdx = len(suffix) - 1;
        Index firstNonSlashEnd = -1;
        for (Index i = len(path) - 1; i >= 0; --i) {
            const int code = charCodeAt(path, i);
            if (code == CHAR_FORWARD_SLASH) {
                // If we reached a path separator that was not part of a set of path
                // separators at the end of the string, stop now
                if (!matchedSlash) {
                    start = i + 1;
                    break;
                }
            } else {
                if (firstNonSlashEnd == -1) {
                    // We saw the first non-path separator, remember this index in case
                    // we need it if the extension ends up not matching
                    matchedSlash = false;
                    firstNonSlashEnd = i + 1;
                }
                if (extIdx >= 0) {
                    // Try to match the explicit extension
                    if (code == charCodeAt(suffix, extIdx)) {
                        if (--extIdx == -1) {
                            // We matched the extension, so mark this as the end of our path
                            // component
                            end = i;
                        }
                    } else {
                        // Extension does not match, so our result is the entire path
                        // component
                        extIdx = -1;
                        end = firstNonSlashEnd;
                    }
                }
            }
        }

        if (start == end)
            end = firstNonSlashEnd;
        else if (end == -1)
            end = len(path);
        return slice(path, start, end);
    }
    for (Index i = len(path) - 1; i >= 0; --i) {
        if (charCodeAt(path, i) == CHAR_FORWARD_SLASH) {
            // If we reached a path separator that was not part of a set of path
            // separators at the end of the string, stop now
            if (!matchedSlash) {
                start = i + 1;
                break;
            }
        } else if (end == -1) {
            // We saw the first non-path separator, mark this as the end of our
            // path component
            matchedSlash = false;
            end = i + 1;
        }
    }

    if (end == -1) return U();
    return slice(path, start, end);
}

U posixExtname(const U& path) {
    Index startDot = -1;
    Index startPart = 0;
    Index end = -1;
    bool matchedSlash = true;
    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    int preDotState = 0;
    for (Index i = len(path) - 1; i >= 0; --i) {
        const int code = charCodeAt(path, i);
        if (code == CHAR_FORWARD_SLASH) {
            // If we reached a path separator that was not part of a set of path
            // separators at the end of the string, stop now
            if (!matchedSlash) {
                startPart = i + 1;
                break;
            }
            continue;
        }
        if (end == -1) {
            // We saw the first non-path separator, mark this as the end of our
            // extension
            matchedSlash = false;
            end = i + 1;
        }
        if (code == CHAR_DOT) {
            // If this is our first dot, mark it as the start of our extension
            if (startDot == -1)
                startDot = i;
            else if (preDotState != 1)
                preDotState = 1;
        } else if (startDot != -1) {
            // We saw a non-dot and non-path separator before our dot, so we should
            // have a good chance at having a non-empty extension
            preDotState = -1;
        }
    }

    if (startDot == -1 || end == -1 ||
        // We saw a non-dot character immediately before the dot
        preDotState == 0 ||
        // The (right-most) trimmed path component is exactly '..'
        (preDotState == 1 && startDot == end - 1 && startDot == startPart + 1)) {
        return U();
    }
    return slice(path, startDot, end);
}

ParsedU posixParse(const U& path) {
    ParsedU ret;
    if (len(path) == 0) return ret;
    const bool isAbsolute = charCodeAt(path, 0) == CHAR_FORWARD_SLASH;
    Index start;
    if (isAbsolute) {
        ret.root = u"/";
        start = 1;
    } else {
        start = 0;
    }
    Index startDot = -1;
    Index startPart = 0;
    Index end = -1;
    bool matchedSlash = true;
    Index i = len(path) - 1;

    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    int preDotState = 0;

    // Get non-dir info
    for (; i >= start; --i) {
        const int code = charCodeAt(path, i);
        if (code == CHAR_FORWARD_SLASH) {
            // If we reached a path separator that was not part of a set of path
            // separators at the end of the string, stop now
            if (!matchedSlash) {
                startPart = i + 1;
                break;
            }
            continue;
        }
        if (end == -1) {
            // We saw the first non-path separator, mark this as the end of our
            // extension
            matchedSlash = false;
            end = i + 1;
        }
        if (code == CHAR_DOT) {
            // If this is our first dot, mark it as the start of our extension
            if (startDot == -1)
                startDot = i;
            else if (preDotState != 1)
                preDotState = 1;
        } else if (startDot != -1) {
            // We saw a non-dot and non-path separator before our dot, so we should
            // have a good chance at having a non-empty extension
            preDotState = -1;
        }
    }

    if (end != -1) {
        const Index s = startPart == 0 && isAbsolute ? 1 : startPart;
        if (startDot == -1 ||
            // We saw a non-dot character immediately before the dot
            preDotState == 0 ||
            // The (right-most) trimmed path component is exactly '..'
            (preDotState == 1 && startDot == end - 1 && startDot == startPart + 1)) {
            ret.base = ret.name = slice(path, s, end);
        } else {
            ret.name = slice(path, s, startDot);
            ret.base = slice(path, s, end);
            ret.ext = slice(path, startDot, end);
        }
    }

    if (startPart > 0)
        ret.dir = slice(path, 0, startPart - 1);
    else if (isAbsolute)
        ret.dir = u"/";

    return ret;
}

std::vector<U> toU16All(const std::vector<std::string>& args) {
    std::vector<U> out;
    out.reserve(args.size());
    for (const std::string& a : args) out.push_back(toU16(a));
    return out;
}

ParsedPath toParsed(const ParsedU& p) {
    return ParsedPath{toU8(p.root), toU8(p.dir), toU8(p.base), toU8(p.ext), toU8(p.name)};
}

} // namespace

// --- Public UTF-8 interface ---------------------------------------------------

namespace win32 {
std::string resolve(const std::vector<std::string>& args, const ResolveEnv& env) {
    return toU8(win32Resolve(toU16All(args), env));
}
std::string normalize(const std::string& path) { return toU8(win32Normalize(toU16(path))); }
bool isAbsolute(const std::string& path) { return win32IsAbsolute(toU16(path)); }
std::string join(const std::vector<std::string>& args) { return toU8(win32Join(toU16All(args))); }
std::string relative(const std::string& from, const std::string& to, const ResolveEnv& env) {
    return toU8(win32Relative(toU16(from), toU16(to), env));
}
std::string toNamespacedPath(const std::string& path, const ResolveEnv& env) {
    return toU8(win32ToNamespacedPath(toU16(path), env));
}
std::string dirname(const std::string& path) { return toU8(win32Dirname(toU16(path))); }
std::string basename(const std::string& path, bool hasSuffix, const std::string& suffix) {
    return toU8(win32Basename(toU16(path), hasSuffix, toU16(suffix)));
}
std::string extname(const std::string& path) { return toU8(win32Extname(toU16(path))); }
ParsedPath parse(const std::string& path) { return toParsed(win32Parse(toU16(path))); }
std::string format(const FormatInput& in) { return formatWith(u'\\', in); }
} // namespace win32

namespace posix {
std::string resolve(const std::vector<std::string>& args, const ResolveEnv& env) {
    return toU8(posixResolve(toU16All(args), env));
}
std::string normalize(const std::string& path) { return toU8(posixNormalize(toU16(path))); }
bool isAbsolute(const std::string& path) { return posixIsAbsolute(toU16(path)); }
std::string join(const std::vector<std::string>& args) { return toU8(posixJoin(toU16All(args))); }
std::string relative(const std::string& from, const std::string& to, const ResolveEnv& env) {
    return toU8(posixRelative(toU16(from), toU16(to), env));
}
std::string toNamespacedPath(const std::string& path) {
    // Non-op on posix systems
    return path;
}
std::string dirname(const std::string& path) { return toU8(posixDirname(toU16(path))); }
std::string basename(const std::string& path, bool hasSuffix, const std::string& suffix) {
    return toU8(posixBasename(toU16(path), hasSuffix, toU16(suffix)));
}
std::string extname(const std::string& path) { return toU8(posixExtname(toU16(path))); }
ParsedPath parse(const std::string& path) { return toParsed(posixParse(toU16(path))); }
std::string format(const FormatInput& in) { return formatWith(u'/', in); }
} // namespace posix

} // namespace pathalg
} // namespace protojs
