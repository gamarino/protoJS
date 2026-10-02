// util.inspect, util.format and util.formatWithOptions.
//
// A port of the parts of Node.js's lib/internal/util/inspect.js that protoJS
// can support (Node.js v22, MIT licence, Copyright Joyent, Inc. and other Node
// contributors). The build embeds this file in the binary
// (protojs_embed_javascript in CMakeLists.txt); src/modules/util/UtilModule.cpp
// evaluates it once per JavaScript context, on the first call of a util
// function, and keeps the returned object on the native global.
//
// The source is one function expression: its value is what evaluating the file
// produces, and calling it returns { inspect, format, formatWithOptions }.
//
// What is not supported, and why, is listed in docs/API_REFERENCE.md
// (util): colours, numericSeparator, showProxy, the boxed-primitive,
// typed-array and Promise forms, and the error forms that need `err.stack`
// (protoJS errors carry no stack).
(function () {
  'use strict';

  var inspectDefaultOptions = {
    showHidden: false,
    depth: 2,
    colors: false,
    customInspect: true,
    showProxy: false,
    maxArrayLength: 100,
    maxStringLength: 10000,
    breakLength: 80,
    compact: 3,
    sorted: false,
    getters: false,
    numericSeparator: false
  };

  var kObjectType = 0;
  var kArrayType = 1;       // an array element: printed without its index
  var kArrayExtrasType = 2; // a named property of an array

  // Built-in constructors: an object whose toString comes from one of their
  // prototypes is printed by inspect for %s, as Node does.
  var builtInObjects = ['Object', 'Function', 'Array', 'Number', 'Boolean',
    'String', 'Symbol', 'Date', 'Promise', 'RegExp', 'Error', 'EvalError',
    'RangeError', 'ReferenceError', 'SyntaxError', 'TypeError', 'URIError',
    'AggregateError', 'ArrayBuffer', 'DataView', 'Map', 'BigInt', 'Set',
    'WeakMap', 'WeakSet', 'Proxy', 'Reflect', 'SharedArrayBuffer',
    'Uint8Array', 'Int8Array', 'Uint16Array', 'Int16Array', 'Uint32Array',
    'Int32Array', 'Float32Array', 'Float64Array', 'Uint8ClampedArray',
    'BigUint64Array', 'BigInt64Array'];

  var hasOwn = function (o, k) {
    return Object.prototype.hasOwnProperty.call(o, k);
  };

  var keyStrRegExp = /^[a-zA-Z_][a-zA-Z_0-9]*$/;

  function isIdentifierKey(key) {
    return keyStrRegExp.test(key);
  }

  function isArrayIndex(key) {
    if (key === '0') return true;
    var c = key.charCodeAt(0);
    if (!(c >= 49 && c <= 57)) return false;
    for (var i = 1; i < key.length; i++) {
      c = key.charCodeAt(i);
      if (c < 48 || c > 57) return false;
    }
    return true;
  }

  // ---- Strings ------------------------------------------------------------

  var meta = ['\\x00', '\\x01', '\\x02', '\\x03', '\\x04', '\\x05', '\\x06',
    '\\x07', '\\b', '\\t', '\\n', '\\x0B', '\\f', '\\r', '\\x0E', '\\x0F',
    '\\x10', '\\x11', '\\x12', '\\x13', '\\x14', '\\x15', '\\x16', '\\x17',
    '\\x18', '\\x19', '\\x1A', '\\x1B', '\\x1C', '\\x1D', '\\x1E', '\\x1F'];

  function hex4(code) {
    var h = code.toString(16);
    while (h.length < 4) h = '0' + h;
    return '\\u' + h;
  }

  // Quote a string the way Node does: single quotes, unless the string
  // contains single quotes and no double quotes (then double quotes), or
  // contains both and no backtick or `${` (then backticks).
  function strEscape(str) {
    var quote = 39; // '
    if (str.indexOf("'") !== -1) {
      if (str.indexOf('"') === -1) {
        quote = 34;
      } else if (str.indexOf('`') === -1 && str.indexOf('${') === -1) {
        quote = 96;
      }
    }
    var q = String.fromCharCode(quote);
    var result = '';
    var last = 0;
    for (var i = 0; i < str.length; i++) {
      var c = str.charCodeAt(i);
      var rep = null;
      if (c === quote || c === 92) {
        rep = '\\' + str.charAt(i);
      } else if (c < 32) {
        rep = meta[c];
      } else if (c > 126 && c < 160) {
        rep = '\\x' + c.toString(16).toUpperCase();
      } else if (c >= 0xD800 && c <= 0xDFFF) {
        if (c <= 0xDBFF && i + 1 < str.length) {
          var d = str.charCodeAt(i + 1);
          if (d >= 0xDC00 && d <= 0xDFFF) {
            i++;
            continue;
          }
        }
        rep = hex4(c);
      }
      if (rep !== null) {
        result += str.slice(last, i) + rep;
        last = i + 1;
      }
    }
    if (last === 0) return q + str + q;
    return q + result + str.slice(last) + q;
  }

  // ---- Numbers ------------------------------------------------------------

  function formatNumber(n) {
    if (n === 0 && 1 / n < 0) return '-0';
    return '' + n;
  }

  function formatBigInt(n) {
    return n.toString() + 'n';
  }

  // ---- Primitives ---------------------------------------------------------

  function formatPrimitive(ctx, value) {
    var type = typeof value;
    if (type === 'string') {
      var trailer = '';
      if (value.length > ctx.maxStringLength) {
        var remaining = value.length - ctx.maxStringLength;
        value = value.slice(0, ctx.maxStringLength);
        trailer = '... ' + remaining + ' more character' +
          (remaining > 1 ? 's' : '');
      }
      // A long string is split at its line breaks, one quoted piece per line.
      if (ctx.compact !== true && value.length > 16 &&
          value.length > ctx.breakLength - ctx.indentationLvl - 4) {
        var parts = [];
        var start = 0;
        for (var i = 0; i < value.length; i++) {
          if (value.charCodeAt(i) === 10) {
            parts.push(value.slice(start, i + 1));
            start = i + 1;
          }
        }
        if (start < value.length) parts.push(value.slice(start));
        if (parts.length > 1) {
          var pieces = [];
          for (var j = 0; j < parts.length; j++) pieces.push(strEscape(parts[j]));
          return pieces.join(' +\n' + ' '.repeat(ctx.indentationLvl + 2)) + trailer;
        }
      }
      return strEscape(value) + trailer;
    }
    if (type === 'number') return formatNumber(value);
    if (type === 'bigint') return formatBigInt(value);
    if (type === 'boolean') return '' + value;
    if (type === 'undefined') return 'undefined';
    // Symbol
    return value.toString();
  }

  // ---- Objects ------------------------------------------------------------

  // The name of the nearest constructor on the prototype chain, or null for an
  // object with a null prototype.
  function isInstanceof(object, proto) {
    try {
      return object instanceof proto;
    } catch (e) {
      return false;
    }
  }

  function getConstructorName(obj) {
    var o = obj;
    var guard = 0;
    while (o !== null && o !== undefined && guard++ < 1000) {
      var desc;
      try {
        desc = Object.getOwnPropertyDescriptor(o, 'constructor');
      } catch (e) {
        desc = undefined;
      }
      if (desc !== undefined && typeof desc.value === 'function' &&
          typeof desc.value.name === 'string' && desc.value.name !== '' &&
          isInstanceof(obj, desc.value)) {
        return desc.value.name;
      }
      o = Object.getPrototypeOf(o);
      if (o === null) return guard === 1 ? null : 'Object';
    }
    // Node prints V8's internal class name here ("<Complex prototype>");
    // protoJS has none to offer.
    return 'Object';
  }

  function getPrefix(constructor, fallback, size) {
    if (constructor === null) {
      return '[' + fallback + (size === undefined ? '' : size) +
        ': null prototype] ';
    }
    return constructor + (size === undefined ? '' : size) + ' ';
  }

  function getKeys(ctx, value) {
    var keys;
    var symbols = Object.getOwnPropertySymbols(value);
    if (ctx.showHidden) {
      keys = Object.getOwnPropertyNames(value);
      for (var i = 0; i < symbols.length; i++) keys.push(symbols[i]);
    } else {
      keys = Object.keys(value);
      for (var j = 0; j < symbols.length; j++) {
        if (Object.prototype.propertyIsEnumerable.call(value, symbols[j])) {
          keys.push(symbols[j]);
        }
      }
    }
    return keys;
  }

  function isError(value) {
    return value instanceof Error ||
      Object.prototype.toString.call(value) === '[object Error]';
  }

  function getFunctionBase(value, constructor) {
    var source = '';
    try {
      source = Function.prototype.toString.call(value);
    } catch (e) {
      source = '';
    }
    var name = typeof value.name === 'string' ? value.name : '';
    if (source.slice(0, 5) === 'class' && source.slice(-1) === '}') {
      var base = 'class ' + ((hasOwn(value, 'name') && value.name) || '(anonymous)');
      if (constructor !== 'Function' && constructor !== null) {
        base += ' [' + constructor + ']';
      }
      if (constructor !== null) {
        var superName = Object.getPrototypeOf(value).name;
        if (superName) base += ' extends ' + superName;
      } else {
        base += ' extends [null prototype]';
      }
      return '[' + base + ']';
    }
    var type = 'Function';
    if (constructor === 'GeneratorFunction' || constructor === 'AsyncFunction' ||
        constructor === 'AsyncGeneratorFunction') {
      type = constructor;
    }
    var out = '[' + type;
    if (constructor === null) out += ' (null prototype)';
    if (name === '') out += ' (anonymous)';
    else out += ': ' + name;
    out += ']';
    if (constructor !== type && constructor !== null) {
      out += ' ' + constructor;
    }
    return out;
  }

  function errorToString(err) {
    var name = err.name === undefined ? 'Error' : '' + err.name;
    var msg = err.message === undefined ? '' : '' + err.message;
    if (name === '') return msg;
    if (msg === '') return name;
    return name + ': ' + msg;
  }

  function improveStack(stack, constructor, name) {
    var len = name.length;
    if (constructor === null ||
        (name.slice(-5) === 'Error' && stack.slice(0, len) === name &&
         (stack.length === len || stack.charAt(len) === ':' ||
          stack.charAt(len) === '\n'))) {
      var fallback = 'Error';
      if (constructor === null) {
        var start = /^([A-Z][a-z_ A-Z0-9[\]()-]+)(?::|\n {4}at)/.exec(stack) ||
          /^([a-z_A-Z0-9-]*Error)$/.exec(stack);
        fallback = (start && start[1]) || '';
        len = fallback.length;
        fallback = fallback || 'Error';
      }
      var prefix = getPrefix(constructor, fallback).slice(0, -1);
      if (name !== prefix) {
        if (prefix.indexOf(name) !== -1) {
          if (len === 0) stack = prefix + ': ' + stack;
          else stack = prefix + stack.slice(len);
        } else {
          stack = prefix + ' [' + name + ']' + stack.slice(len);
        }
      }
    }
    return stack;
  }

  // protoJS errors carry no stack, so this prints "[Name: message]".
  function formatError(ctx, err, constructor, keys) {
    var name = err.name != null ? '' + err.name : 'Error';
    var stack = typeof err.stack === 'string' && err.stack !== '' ?
      err.stack : errorToString(err);
    if (!ctx.showHidden && keys.length !== 0) {
      ['name', 'message', 'stack'].forEach(function (key) {
        var index = keys.indexOf(key);
        if (index !== -1 && (typeof err[key] !== 'string' ||
                             stack.indexOf(err[key]) !== -1)) {
          keys.splice(index, 1);
        }
      });
    }
    if ('cause' in err && keys.indexOf('cause') === -1) keys.push('cause');
    stack = improveStack(stack, constructor, name);
    var pos = (err.message && stack.indexOf(err.message)) || -1;
    if (pos !== -1) pos += err.message.length;
    if (stack.indexOf('\n    at', pos) === -1) {
      stack = '[' + stack + ']';
    }
    if (ctx.indentationLvl !== 0) {
      stack = stack.split('\n').join('\n' + ' '.repeat(ctx.indentationLvl));
    }
    return stack;
  }

  function formatValue(ctx, value, recurseTimes) {
    if (typeof value !== 'object' && typeof value !== 'function') {
      return formatPrimitive(ctx, value);
    }
    if (value === null) return 'null';

    if (ctx.customInspect) {
      var custom = value[inspectCustom];
      if (typeof custom === 'function' && custom !== inspect) {
        var depth = ctx.depth === null ? null : ctx.depth - recurseTimes;
        var ret = custom.call(value, depth, Object.assign({}, ctx, { depth: depth }), inspect);
        if (ret !== value) {
          if (typeof ret !== 'string') return formatValue(ctx, ret, recurseTimes);
          return ret.split('\n').join('\n' + ' '.repeat(ctx.indentationLvl));
        }
      }
    }

    if (ctx.seen.indexOf(value) !== -1) {
      var index = 1;
      var found = -1;
      for (var i = 0; i < ctx.circular.length; i++) {
        if (ctx.circular[i] === value) found = i;
      }
      if (found === -1) {
        ctx.circular.push(value);
        index = ctx.circular.length;
      } else {
        index = found + 1;
      }
      return '[Circular *' + index + ']';
    }
    return formatRaw(ctx, value, recurseTimes);
  }

  function formatRaw(ctx, value, recurseTimes) {
    var constructor = getConstructorName(value);
    var keys = getKeys(ctx, value);
    var base = '';
    var formatter = formatObjectEntries;
    var braces;
    var noIterator = true;
    var extrasType = kObjectType;

    if (Array.isArray(value)) {
      var prefix = (constructor !== 'Array' || constructor === null) ?
        getPrefix(constructor, 'Array', '(' + value.length + ')') : '';
      keys = keys.filter(function (k) {
        return typeof k !== 'string' || !isArrayIndex(k);
      });
      if (!ctx.showHidden) {
        keys = keys.filter(function (k) { return k !== 'length'; });
      }
      braces = [prefix + '[', ']'];
      if (value.length === 0 && keys.length === 0) return braces[0] + ']';
      extrasType = kArrayExtrasType;
      formatter = formatArray;
      noIterator = false;
    } else if (value instanceof Set) {
      keys = keys.filter(function (k) { return k !== 'size'; });
      braces = [getPrefix(constructor, 'Set', '(' + value.size + ')') + '{', '}'];
      if (value.size === 0 && keys.length === 0) return braces[0] + '}';
      formatter = formatSet;
      noIterator = false;
    } else if (value instanceof Map) {
      keys = keys.filter(function (k) { return k !== 'size'; });
      braces = [getPrefix(constructor, 'Map', '(' + value.size + ')') + '{', '}'];
      if (value.size === 0 && keys.length === 0) return braces[0] + '}';
      formatter = formatMap;
      noIterator = false;
    }

    if (noIterator) {
      braces = ['{', '}'];
      if (typeof value === 'function') {
        base = getFunctionBase(value, constructor);
        if (ctx.showHidden) {
          keys = keys.filter(function (k) { return k !== 'arguments' && k !== 'caller'; });
        }
        if (keys.length === 0) return base;
      } else if (value instanceof RegExp) {
        base = RegExp.prototype.toString.call(value);
        keys = keys.filter(function (k) { return k !== 'lastIndex'; });
        var rprefix = getPrefix(constructor, 'RegExp');
        if (rprefix !== 'RegExp ') base = rprefix + base;
        if (keys.length === 0) return base;
      } else if (value instanceof Date) {
        var t = value.getTime();
        base = t !== t ? 'Invalid Date' : value.toISOString();
        var dprefix = getPrefix(constructor, 'Date');
        if (dprefix !== 'Date ') base = dprefix + base;
        if (keys.length === 0) return base;
      } else if (isError(value)) {
        base = formatError(ctx, value, constructor, keys);
        if (keys.length === 0) return base;
      } else {
        if (constructor === 'Object') {
          if (keys.length === 0) return '{}';
        } else {
          braces[0] = getPrefix(constructor, 'Object') + '{';
          if (keys.length === 0) return braces[0] + '}';
        }
      }
    }

    if (ctx.depth !== null && recurseTimes > ctx.depth) {
      var name = getPrefix(constructor, 'Object').slice(0, -1);
      if (Array.isArray(value)) name = getPrefix(constructor, 'Array').slice(0, -1);
      return constructor !== null ? '[' + name + ']' : name;
    }
    recurseTimes += 1;
    ctx.seen.push(value);
    ctx.currentDepth = recurseTimes;
    var output = formatter(ctx, value, recurseTimes);
    for (var i = 0; i < keys.length; i++) {
      output.push(formatProperty(ctx, value, recurseTimes, keys[i], extrasType));
    }
    ctx.seen.pop();

    var refIndex = ctx.circular.indexOf(value);
    if (refIndex !== -1) {
      var reference = '<ref *' + (refIndex + 1) + '>';
      base = base === '' ? reference : reference + ' ' + base;
    }

    if (ctx.sorted) {
      if (extrasType === kObjectType) output.sort();
    }
    return reduceToSingleString(ctx, output, base, braces, extrasType, recurseTimes, value);
  }

  function formatObjectEntries() {
    return [];
  }

  function formatArray(ctx, value, recurseTimes) {
    var valLen = value.length;
    var len = Math.min(Math.max(0, ctx.maxArrayLength), valLen);
    var remaining = valLen - len;
    var output = [];
    var holes = 0;
    var flushHoles = function () {
      if (holes > 0) {
        output.push('<' + holes + ' empty item' + (holes > 1 ? 's' : '') + '>');
        holes = 0;
      }
    };
    for (var i = 0; i < len; i++) {
      if (!hasOwn(value, i)) {
        holes++;
        continue;
      }
      flushHoles();
      output.push(formatProperty(ctx, value, recurseTimes, i, kArrayType));
    }
    flushHoles();
    if (remaining > 0) output.push(remainingText(remaining));
    return output;
  }

  function remainingText(remaining) {
    return '... ' + remaining + ' more item' + (remaining > 1 ? 's' : '');
  }

  function formatSet(ctx, value, recurseTimes) {
    var maxLength = Math.min(Math.max(0, ctx.maxArrayLength), value.size);
    var output = [];
    ctx.indentationLvl += 2;
    value.forEach(function (v) {
      if (output.length < maxLength) output.push(formatValue(ctx, v, recurseTimes));
    });
    if (value.size > maxLength) output.push(remainingText(value.size - maxLength));
    ctx.indentationLvl -= 2;
    return output;
  }

  function formatMap(ctx, value, recurseTimes) {
    var maxLength = Math.min(Math.max(0, ctx.maxArrayLength), value.size);
    var output = [];
    ctx.indentationLvl += 2;
    value.forEach(function (v, k) {
      if (output.length < maxLength) {
        output.push(formatValue(ctx, k, recurseTimes) + ' => ' +
          formatValue(ctx, v, recurseTimes));
      }
    });
    if (value.size > maxLength) output.push(remainingText(value.size - maxLength));
    ctx.indentationLvl -= 2;
    return output;
  }

  function formatProperty(ctx, value, recurseTimes, key, type) {
    var name;
    var str;
    var desc = Object.getOwnPropertyDescriptor(value, key) ||
      { value: value[key], enumerable: true };
    if (desc.value !== undefined) {
      ctx.indentationLvl += 2;
      str = formatValue(ctx, desc.value, recurseTimes);
      ctx.indentationLvl -= 2;
    } else if (desc.get !== undefined) {
      str = desc.set !== undefined ? '[Getter/Setter]' : '[Getter]';
    } else if (desc.set !== undefined) {
      str = '[Setter]';
    } else {
      str = 'undefined';
    }
    if (type === kArrayType) return str;
    if (typeof key === 'symbol') {
      name = '[' + key.toString() + ']';
    } else if (key === '__proto__') {
      name = "['__proto__']";
    } else if (desc.enumerable === false) {
      name = '[' + (isIdentifierKey(key) ? key : strEscape(key)) + ']';
    } else if (isIdentifierKey(key)) {
      name = key;
    } else {
      name = strEscape(key);
    }
    return name + ': ' + str;
  }

  function isBelowBreakLength(ctx, output, start, base) {
    var totalLength = output.length + start;
    if (totalLength + output.length > ctx.breakLength) return false;
    for (var i = 0; i < output.length; i++) {
      totalLength += output[i].length;
      if (totalLength > ctx.breakLength) return false;
    }
    return base === '' || base.indexOf('\n') === -1;
  }

  function groupArrayElements(ctx, output, value) {
    var totalLength = 0;
    var maxLength = 0;
    var i = 0;
    var outputLength = output.length;
    if (ctx.maxArrayLength < output.length) outputLength--;
    var separatorSpace = 2;
    var dataLen = new Array(outputLength);
    for (; i < outputLength; i++) {
      var len = output[i].length;
      dataLen[i] = len;
      totalLength += len + separatorSpace;
      if (maxLength < len) maxLength = len;
    }
    var actualMax = maxLength + separatorSpace;
    if (actualMax * 3 + ctx.indentationLvl < ctx.breakLength &&
        (totalLength / actualMax > 5 || maxLength <= 6)) {
      var approxCharHeights = 2.5;
      var averageBias = Math.sqrt(actualMax - totalLength / output.length);
      var biasedMax = Math.max(actualMax - 3 - averageBias, 1);
      var columns = Math.min(
        Math.round(Math.sqrt(approxCharHeights * biasedMax * outputLength) / biasedMax),
        Math.floor((ctx.breakLength - ctx.indentationLvl) / actualMax),
        ctx.compact * 4,
        15);
      if (columns <= 1) return output;
      var tmp = [];
      var maxLineLength = [];
      for (var c = 0; c < columns; c++) {
        var lineLength = 0;
        for (var j = c; j < output.length; j += columns) {
          if (dataLen[j] > lineLength) lineLength = dataLen[j];
        }
        maxLineLength.push(lineLength + separatorSpace);
      }
      var padStart = true;
      if (value !== undefined) {
        for (var k = 0; k < output.length; k++) {
          if (typeof value[k] !== 'number' && typeof value[k] !== 'bigint') {
            padStart = false;
            break;
          }
        }
      }
      for (var r = 0; r < outputLength; r += columns) {
        var max = Math.min(r + columns, outputLength);
        var str = '';
        var m = r;
        for (; m < max - 1; m++) {
          var cell = output[m] + ', ';
          var padding = maxLineLength[m - r];
          str += padStart ? cell.padStart(padding, ' ') : cell.padEnd(padding, ' ');
        }
        if (padStart) {
          str += output[m].padStart(maxLineLength[m - r] - separatorSpace, ' ');
        } else {
          str += output[m];
        }
        tmp.push(str);
      }
      if (ctx.maxArrayLength < output.length) tmp.push(output[outputLength]);
      output = tmp;
    }
    return output;
  }

  function reduceToSingleString(ctx, output, base, braces, extrasType, recurseTimes, value) {
    if (ctx.compact !== true) {
      if (typeof ctx.compact === 'number' && ctx.compact >= 1) {
        var entries = output.length;
        if (extrasType === kArrayExtrasType && entries > 6) {
          output = groupArrayElements(ctx, output, value);
        }
        if (ctx.currentDepth - recurseTimes < ctx.compact && entries === output.length) {
          var start = output.length + ctx.indentationLvl + braces[0].length + base.length + 10;
          if (isBelowBreakLength(ctx, output, start, base)) {
            var joined = output.join(', ');
            if (joined.indexOf('\n') === -1) {
              return (base ? base + ' ' : '') + braces[0] + ' ' + joined + ' ' + braces[1];
            }
          }
        }
      }
      var indentation = '\n' + ' '.repeat(ctx.indentationLvl);
      return (base ? base + ' ' : '') + braces[0] + indentation + '  ' +
        output.join(',' + indentation + '  ') + indentation + braces[1];
    }
    if (isBelowBreakLength(ctx, output, 0, base)) {
      return braces[0] + (base ? ' ' + base : '') + ' ' + output.join(', ') + ' ' + braces[1];
    }
    var ind = ' '.repeat(ctx.indentationLvl);
    var ln = base === '' && braces[0].length === 1 ? ' ' : (base ? ' ' + base : '') + '\n' + ind + '  ';
    return braces[0] + ln + output.join(',\n' + ind + '  ') + ' ' + braces[1];
  }

  // ---- inspect --------------------------------------------------------------

  var inspectCustom = Symbol.for('nodejs.util.inspect.custom');

  function inspect(value, opts) {
    var ctx = {
      seen: [],
      circular: [],
      indentationLvl: 0,
      currentDepth: 0
    };
    var k;
    for (k in inspectDefaultOptions) ctx[k] = inspectDefaultOptions[k];
    if (arguments.length > 1) {
      // The legacy signature: inspect(value, showHidden, depth, colors).
      if (typeof opts === 'boolean') {
        ctx.showHidden = opts;
        if (arguments.length > 2 && arguments[2] !== undefined) ctx.depth = arguments[2];
      } else if (opts !== null && typeof opts === 'object') {
        var optKeys = Object.keys(opts);
        for (var i = 0; i < optKeys.length; i++) {
          k = optKeys[i];
          if (hasOwn(inspectDefaultOptions, k)) ctx[k] = opts[k];
        }
      }
    }
    if (ctx.depth === Infinity) ctx.depth = null;
    if (ctx.maxArrayLength === null) ctx.maxArrayLength = Infinity;
    if (ctx.maxStringLength === null) ctx.maxStringLength = Infinity;
    return formatValue(ctx, value, 0);
  }

  inspect.custom = inspectCustom;
  inspect.defaultOptions = inspectDefaultOptions;

  // ---- format ---------------------------------------------------------------

  var circularErrorMessage;

  function firstErrorLine(error) {
    return ('' + error.message).split('\n')[0];
  }

  function tryStringify(arg) {
    try {
      return JSON.stringify(arg);
    } catch (err) {
      if (circularErrorMessage === undefined) {
        try {
          var a = {};
          a.a = a;
          JSON.stringify(a);
        } catch (circularError) {
          circularErrorMessage = firstErrorLine(circularError);
        }
      }
      if (err && err.name === 'TypeError' &&
          firstErrorLine(err) === circularErrorMessage) {
        return '[Circular]';
      }
      throw err;
    }
  }

  // Whether `value` prints through inspect for %s: true unless it has its own
  // toString or Symbol.toPrimitive, or inherits one from something other than
  // a built-in prototype.
  function hasBuiltInToString(value) {
    var hasOwnToString = hasOwn;
    var hasOwnToPrimitive = hasOwn;
    var returnFalse = function () { return false; };
    if (typeof value.toString !== 'function') {
      if (typeof value[Symbol.toPrimitive] !== 'function') return true;
      if (hasOwn(value, Symbol.toPrimitive)) return false;
      hasOwnToString = returnFalse;
    } else if (hasOwn(value, 'toString')) {
      return false;
    } else if (typeof value[Symbol.toPrimitive] !== 'function') {
      hasOwnToPrimitive = returnFalse;
    } else if (hasOwn(value, Symbol.toPrimitive)) {
      return false;
    }
    var pointer = value;
    do {
      pointer = Object.getPrototypeOf(pointer);
    } while (pointer !== null && !hasOwnToString(pointer, 'toString') &&
             !hasOwnToPrimitive(pointer, Symbol.toPrimitive));
    if (pointer === null) return true;
    var descriptor = Object.getOwnPropertyDescriptor(pointer, 'constructor');
    return descriptor !== undefined && typeof descriptor.value === 'function' &&
      builtInObjects.indexOf(descriptor.value.name) !== -1;
  }

  function formatWithOptionsInternal(inspectOptions, args) {
    var first = args[0];
    var a = 0;
    var str = '';
    var join = '';

    if (typeof first === 'string') {
      if (args.length === 1) return first;
      var tempStr;
      var lastPos = 0;
      for (var i = 0; i < first.length - 1; i++) {
        if (first.charCodeAt(i) === 37) { // '%'
          var nextChar = first.charCodeAt(++i);
          if (a + 1 !== args.length) {
            switch (nextChar) {
              case 115: { // 's'
                var tempArg = args[++a];
                if (typeof tempArg === 'number') {
                  tempStr = formatNumber(tempArg);
                } else if (typeof tempArg === 'bigint') {
                  tempStr = formatBigInt(tempArg);
                } else if (typeof tempArg === 'symbol') {
                  tempStr = tempArg.toString();
                } else if (typeof tempArg !== 'object' || tempArg === null ||
                           !hasBuiltInToString(tempArg)) {
                  tempStr = String(tempArg);
                } else {
                  tempStr = inspect(tempArg, Object.assign({}, inspectOptions,
                    { depth: 0, colors: false, compact: 3 }));
                }
                break;
              }
              case 106: // 'j'
                tempStr = '' + tryStringify(args[++a]);
                break;
              case 100: { // 'd'
                var tempNum = args[++a];
                if (typeof tempNum === 'bigint') {
                  tempStr = formatBigInt(tempNum);
                } else if (typeof tempNum === 'symbol') {
                  tempStr = 'NaN';
                } else {
                  tempStr = formatNumber(Number(tempNum));
                }
                break;
              }
              case 79: // 'O'
                tempStr = inspect(args[++a], inspectOptions);
                break;
              case 111: // 'o'
                tempStr = inspect(args[++a], Object.assign({}, inspectOptions,
                  { showHidden: true, showProxy: true, depth: 4 }));
                break;
              case 105: { // 'i'
                var tempInteger = args[++a];
                if (typeof tempInteger === 'bigint') {
                  tempStr = formatBigInt(tempInteger);
                } else if (typeof tempInteger === 'symbol') {
                  tempStr = 'NaN';
                } else {
                  tempStr = formatNumber(parseInt(tempInteger));
                }
                break;
              }
              case 102: { // 'f'
                var tempFloat = args[++a];
                if (typeof tempFloat === 'symbol') {
                  tempStr = 'NaN';
                } else {
                  tempStr = formatNumber(parseFloat(tempFloat));
                }
                break;
              }
              case 99: // 'c'
                a += 1;
                tempStr = '';
                break;
              case 37: // '%'
                str += first.slice(lastPos, i);
                lastPos = i + 1;
                continue;
              default: // Not a format specifier.
                continue;
            }
            if (lastPos !== i - 1) str += first.slice(lastPos, i - 1);
            str += tempStr;
            lastPos = i + 1;
          } else if (nextChar === 37) {
            str += first.slice(lastPos, i);
            lastPos = i + 1;
          }
        }
      }
      if (lastPos !== 0) {
        a++;
        join = ' ';
        if (lastPos < first.length) str += first.slice(lastPos);
      }
    }

    while (a < args.length) {
      var value = args[a];
      str += join;
      str += typeof value !== 'string' ? inspect(value, inspectOptions) : value;
      join = ' ';
      a++;
    }
    return str;
  }

  function format() {
    var args = [];
    for (var i = 0; i < arguments.length; i++) args.push(arguments[i]);
    return formatWithOptionsInternal(undefined, args);
  }

  function formatWithOptions(inspectOptions) {
    if (inspectOptions === null || typeof inspectOptions !== 'object') {
      var shown = inspectOptions === null ? 'null' :
        typeof inspectOptions === 'symbol' ? 'type symbol (' + inspectOptions.toString() + ')' :
        typeof inspectOptions === 'undefined' ? 'undefined' :
        'type ' + typeof inspectOptions + ' (' + inspect(inspectOptions) + ')';
      var err = new TypeError('The "inspectOptions" argument must be of type object. ' +
        'Received ' + shown);
      err.code = 'ERR_INVALID_ARG_TYPE';
      throw err;
    }
    var args = [];
    for (var i = 1; i < arguments.length; i++) args.push(arguments[i]);
    return formatWithOptionsInternal(inspectOptions, args);
  }

  return {
    inspect: inspect,
    format: format,
    formatWithOptions: formatWithOptions
  };
})
