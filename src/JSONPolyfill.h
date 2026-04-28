#ifndef PROTOJS_JSON_POLYFILL_H
#define PROTOJS_JSON_POLYFILL_H

namespace protojs {

/**
 * JSON.stringify / JSON.parse polyfill.
 *
 * ProtoInterpreter installs an empty `JSON` stub on the protoCore-side
 * global; QuickJS's native JSON is not plumbed through to it, so user
 * scripts would see `JSON.stringify === undefined` without this shim.
 *
 * The polyfill is prepended to every top-level eval (including worker
 * scripts) in non-module mode by `JSContextWrapper::eval`.  It must
 * stay self-contained — no cross-wrapper function references — because
 * the bytecode that backs the helpers is module-relative and goes
 * stale once its installer module's tables are released.
 *
 * Expects `this` to be the protoCore-side global at top-level eval,
 * which is true for the standard CLI (full-init) path.
 */
inline constexpr const char* kJSONPolyfillPrefix = R"JS(
if (typeof JSON === 'undefined') { this.JSON = {}; }
this.__protojs_jsonEscape = function(s) {
    // Iterate via charAt() rather than .length: in the current
    // protoCore-eval path, String.prototype.length is reported as
    // undefined so a `for (i < s.length; i++)` loop runs exactly
    // once.  charAt(i) returns "" past end, which is a safe sentinel.
    var out = '"';
    var i = 0;
    while (true) {
        var ch = s.charAt(i);
        if (ch === '') break;
        var c = s.charCodeAt(i);
        if (c === 34) out += '\\"';
        else if (c === 92) out += '\\\\';
        else if (c === 10) out += '\\n';
        else if (c === 13) out += '\\r';
        else if (c === 9)  out += '\\t';
        else if (c === 8)  out += '\\b';
        else if (c === 12) out += '\\f';
        else if (c < 32) {
            var hex = c.toString(16);
            out += '\\u' + ('0000' + hex).slice(-4);
        } else {
            out += ch;
        }
        i++;
    }
    return out + '"';
};
this.__protojs_stringify = function(v) {
    if (v === null || v === undefined) return 'null';
    var t = typeof v;
    if (t === 'boolean') return v ? 'true' : 'false';
    if (t === 'number') return (isFinite(v) ? String(v) : 'null');
    if (t === 'string') return __protojs_jsonEscape(v);
    if (Array.isArray(v)) {
        var parts = [];
        for (var i = 0; i < v.length; i++) parts.push(__protojs_stringify(v[i]));
        return '[' + parts.join(',') + ']';
    }
    if (t === 'object') {
        var parts = [];
        for (var k in v) {
            if (Object.prototype.hasOwnProperty.call(v, k)) {
                var sv = __protojs_stringify(v[k]);
                if (sv !== undefined) parts.push(__protojs_jsonEscape(k) + ':' + sv);
            }
        }
        return '{' + parts.join(',') + '}';
    }
    return 'null';
};
JSON.stringify = this.__protojs_stringify;
this.__protojs_parse = function(text) {
    if (typeof text !== 'string') text = String(text);
    var t = text.replace(/"(?:\\.|[^"\\])*"/g, '""');
    if (!/^[\s\d\-\+\.eE\[\]\{\},:tfnurla"]*$/.test(t)) {
        throw new SyntaxError('JSON.parse: invalid character');
    }
    return eval('(' + text + ')');
};
JSON.parse = this.__protojs_parse;
)JS";

} // namespace protojs

#endif // PROTOJS_JSON_POLYFILL_H
