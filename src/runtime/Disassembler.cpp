#include "Disassembler.h"

#include "BytecodeSpecialiser.h"
#include "ProtoBytecodeModule.h"
#include "ProtoCompileOnly.h"
#include "QuickJSBytecodeExport.h"
#include "QuickJSOpcodeEnum.h"
#include "../JSSymbols.h"
#include "quickjs.h"
#include "protoCore.h"

#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <vector>

namespace protojs {

namespace {

// Operand formats of quickjs-opcode.h, plus the formats of protoJS's own
// opcodes 244..250 (BytecodeSpecialiser.h).
enum Fmt : uint8_t {
#define FMT(f) F_##f,
#define DEF(id, size, n_pop, n_push, f)
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
    F_loc8_loc8,             // 244: two u8 locals
    F_loc8_loc8_rel32,       // 245: two u8 locals, i32 offset from the instruction's end
    F_loc8_varref_rel32,     // 246: u8 local, u16 closure var, i32 offset from the end
    F_group,                 // 249, 250: u16 group, u16 position
};

struct OpInfo {
    const char* name;
    uint8_t size;
    uint8_t fmt;
};

// Indexed by opcode byte. QuickJS's final opcodes are DEF entries only, in
// order (QuickJSOpcodeEnum.h); 244..250 are protoJS's.
const OpInfo* opTable() {
    static OpInfo table[256] = {};
    static bool ready = false;
    if (!ready) {
        static const OpInfo qjs[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) {#id, (uint8_t)(size), (uint8_t)F_##f},
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
#undef FMT
        };
        const size_t n = sizeof(qjs) / sizeof(qjs[0]);
        for (size_t i = 0; i < n && i < 256; ++i) table[i] = qjs[i];
        table[244] = {"proto_acc_loc8_loc8", 3, F_loc8_loc8};
        table[245] = {"proto_lt_loc8_loc8_jfalse", 7, F_loc8_loc8_rel32};
        table[246] = {"proto_lt_loc_var_jfalse", 8, F_loc8_varref_rel32};
        table[247] = {"proto_object_imm", 1, F_none};
        table[248] = {"proto_define_field_last", 5, F_atom};
        table[OP_PROTO_PUT_FIELD_GROUP] = {"proto_put_field_group", 5, F_group};
        table[OP_PROTO_PUT_FIELD_GROUP_END] = {"proto_put_field_group_end", 5, F_group};
        ready = true;
    }
    return table;
}

uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// A string as a JavaScript-style literal: quoted, escaped, cut after 60
// characters.
std::string quoted(const char* s, size_t len) {
    std::string r = "\"";
    const size_t limit = 60;
    for (size_t i = 0; i < len && i < limit; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
        else if (c == '\n') r += "\\n";
        else if (c == '\t') r += "\\t";
        else if (c < 0x20 || c == 0x7f) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            r += buf;
        } else r += (char)c;
    }
    r += '"';
    if (len > limit) r += "...";
    return r;
}

std::string atomName(JSContext* ctx, uint32_t atom) {
    const char* s = JS_AtomToCString(ctx, (JSAtom)atom);
    if (!s) return "?";
    std::string r(s);
    JS_FreeCString(ctx, s);
    return r;
}

std::string cstr(JSContext* ctx, const char* s) {
    std::string r = s ? s : "";
    if (s) JS_FreeCString(ctx, s);
    return r;
}

const char* closureTypeName(int t) {
    switch (t) {
        case 0: return "parent local";
        case 1: return "parent arg";
        case 2: return "parent closure var";
        case 3: return "global ref";
        case 4: return "global decl";
        case 5: return "global";
        case 6: return "module decl";
        case 7: return "module import";
        default: return "?";
    }
}

const char* specialObjectName(unsigned k) {
    static const char* names[] = {"arguments", "mapped arguments", "this function",
                                  "new.target", "home object", "var object", "import.meta"};
    return k < sizeof(names) / sizeof(names[0]) ? names[k] : "?";
}

const char* throwErrorName(unsigned k) {
    static const char* names[] = {"read-only variable", "redeclaration", "uninitialized variable",
                                  "delete super", "iterator has no throw"};
    return k < sizeof(names) / sizeof(names[0]) ? names[k] : "?";
}

struct Function {
    void* qjs;
    const ProtoBytecodeModule* mod;
    std::string path;  // "<script>", "0", "0.1": cpool indices from the top level
};

class Lister {
public:
    Lister(JSContext* ctx, proto::ProtoContext* pc, std::ostream& out)
        : ctx_(ctx), pc_(pc), out_(out) {}

    void list(void* qjs, const ProtoBytecodeModule& mod, const std::string& path) {
        std::vector<Function> nested;
        header(qjs, mod, path);
        regexpMarks_ = regexpBytecodeConstants(mod, protojs_bytecode_cpool_count(qjs));
        constants(qjs, mod, path, nested);
        closureVars(mod);
        code(qjs, mod, nested);
        for (const Function& f : nested) {
            out_ << "\n";
            list(f.qjs, *f.mod, f.path);
        }
    }

private:
    JSContext* ctx_;
    proto::ProtoContext* pc_;
    std::ostream& out_;
    std::vector<bool> regexpMarks_;  // of the function being listed

    std::string displayName(const ProtoBytecodeModule& mod, const std::string& path) {
        if (path.empty()) return "<script>";
        return "function " + path + " " + (mod.funcName.empty() ? "<anonymous>" : mod.funcName);
    }

    void header(void*, const ProtoBytecodeModule& mod, const std::string& path) {
        out_ << displayName(mod, path) << ": args " << mod.argCount() << ", locals "
             << mod.varCount() << ", closure vars " << mod.closureVarNames.size()
             << ", stack " << mod.stackSize();
        std::vector<const char*> flags;
        if (mod.isStrict) flags.push_back("strict");
        if (mod.isArrow) flags.push_back("arrow");
        if (mod.isAsync) flags.push_back("async");
        if (mod.isGenerator) flags.push_back("generator");
        if (!path.empty() && mod.isConstructor) flags.push_back("constructor");
        if (mod.usesArguments) flags.push_back("uses arguments");
        for (const char* f : flags) out_ << ", " << f;
        out_ << "\n";
    }

    std::string slotName(void* qjs, unsigned idx) {
        return cstr(ctx_, protojs_bytecode_var_name(ctx_, qjs, (uint16_t)idx));
    }

    // Constant indices that hold a compiled regular expression: the second
    // push before an OP_regexp (pattern, then QuickJS's regexp bytecode).
    std::vector<bool> regexpBytecodeConstants(const ProtoBytecodeModule& mod, int n) {
        std::vector<bool> marks(n > 0 ? (size_t)n : 0, false);
        const uint8_t* buf = mod.buf(pc_);
        const int len = mod.bufLen(pc_);
        const OpInfo* ops = opTable();
        int prev = -1;
        for (int pc = 0; pc < len && ops[buf[pc]].size;) {
            if (buf[pc] == OP_regexp && prev >= 0) {
                unsigned k = buf[prev] == OP_push_const8 ? buf[prev + 1]
                           : buf[prev] == OP_push_const ? u32(buf + prev + 1) : (unsigned)-1;
                if (k < marks.size()) marks[k] = true;
            }
            prev = pc;
            pc += ops[buf[pc]].size;
        }
        return marks;
    }

    void constants(void* qjs, const ProtoBytecodeModule& mod, const std::string& path,
                   std::vector<Function>& nested) {
        const int n = protojs_bytecode_cpool_count(qjs);
        const JSValue* cpool = static_cast<const JSValue*>(protojs_bytecode_cpool(qjs));
        if (mod.argCount() + mod.varCount() > 0) {
            out_ << "  slots:\n";
            for (unsigned i = 0; i < mod.argCount() + mod.varCount(); ++i) {
                const bool arg = i < mod.argCount();
                const unsigned k = arg ? i : i - mod.argCount();
                std::string name = slotName(qjs, i);
                out_ << "    " << (arg ? "arg " : "loc ") << std::left << std::setw(4) << k
                     << std::right << (name.empty() ? "<unnamed>" : name) << "\n";
            }
        }
        if (n <= 0 || !cpool) return;
        out_ << "  constants:\n";
        const std::vector<bool> regexps = regexpBytecodeConstants(mod, n);
        const proto::ProtoString* idKey = JSSymbols::bytecodeId(pc_);
        const std::vector<ProtoBytecodeModule>& table = mod.functionTable()->nestedFunctions;
        for (int i = 0; i < n; ++i) {
            JSValue v = cpool[i];
            out_ << "    #" << std::left << std::setw(4) << i << std::right;
            void* inner = protojs_get_function_bytecode(ctx_, &v);
            if (inner) {
                const ProtoBytecodeModule* im = nullptr;
                const proto::ProtoObject* ph =
                    mod.constantPool ? mod.constantPool->getAt(pc_, i) : nullptr;
                const proto::ProtoObject* id =
                    (ph && idKey) ? ph->getAttribute(pc_, idKey, false) : nullptr;
                if (id && id != PROTO_NONE) {
                    const long long k = id->asLong(pc_);
                    if (k >= 0 && (size_t)k < table.size()) im = &table[(size_t)k];
                }
                const std::string childPath = path.empty() ? std::to_string(i) : path + "." + std::to_string(i);
                if (im) {
                    out_ << "function  " << displayName(*im, childPath) << "\n";
                    nested.push_back({inner, im, childPath});
                } else {
                    out_ << "function  (not loaded)\n";
                }
                continue;
            }
            if (regexps[(size_t)i]) {
                out_ << "regexp    compiled pattern (QuickJS regexp bytecode)\n";
            } else if (JS_IsString(v)) {
                size_t len = 0;
                const char* s = JS_ToCStringLen(ctx_, &len, v);
                out_ << "string    " << (s ? quoted(s, len) : std::string("?")) << "\n";
                if (s) JS_FreeCString(ctx_, s);
            } else if (JS_IsBigInt(ctx_, v)) {
                out_ << "bigint    " << cstr(ctx_, JS_ToCString(ctx_, v)) << "n\n";
            } else if (JS_IsNumber(v)) {
                out_ << "number    " << cstr(ctx_, JS_ToCString(ctx_, v)) << "\n";
            } else if (JS_IsObject(v)) {
                out_ << "object    " << cstr(ctx_, JS_ToCString(ctx_, v)) << "\n";
            } else {
                out_ << "value     " << cstr(ctx_, JS_ToCString(ctx_, v)) << "\n";
            }
        }
    }

    void closureVars(const ProtoBytecodeModule& mod) {
        if (mod.closureVarNames.empty()) return;
        out_ << "  closure vars:\n";
        for (size_t i = 0; i < mod.closureVarNames.size(); ++i) {
            const int t = i < mod.closureVarTypes.size() ? mod.closureVarTypes[i] : -1;
            out_ << "    #" << std::left << std::setw(4) << i << std::right
                 << (mod.closureVarNames[i].empty() ? "<unnamed>" : mod.closureVarNames[i])
                 << "  (" << closureTypeName(t);
            if (t >= 0 && t <= 2 && i < mod.closureVarIndices.size())
                out_ << " " << mod.closureVarIndices[i];
            if (i < mod.closureVarIsConst.size() && mod.closureVarIsConst[i]) out_ << ", const";
            else if (i < mod.closureVarIsLexical.size() && mod.closureVarIsLexical[i]) out_ << ", lexical";
            out_ << ")\n";
        }
    }

    std::string localName(void* qjs, const ProtoBytecodeModule& mod, unsigned k) {
        std::string n = slotName(qjs, mod.argCount() + k);
        return n.empty() ? "loc " + std::to_string(k) : n;
    }
    std::string argName(void* qjs, unsigned k) {
        std::string n = slotName(qjs, k);
        return n.empty() ? "arg " + std::to_string(k) : n;
    }
    std::string varRefName(const ProtoBytecodeModule& mod, unsigned k) {
        if (k < mod.closureVarNames.size() && !mod.closureVarNames[k].empty())
            return mod.closureVarNames[k];
        return "closure var " + std::to_string(k);
    }
    std::string constName(void* qjs, const std::vector<Function>& nested, unsigned k) {
        const int n = protojs_bytecode_cpool_count(qjs);
        const JSValue* cpool = static_cast<const JSValue*>(protojs_bytecode_cpool(qjs));
        if ((int)k >= n || !cpool) return "?";
        JSValue v = cpool[k];
        if (protojs_get_function_bytecode(ctx_, &v)) {
            for (const Function& f : nested) {
                const std::string last = f.path.substr(f.path.rfind('.') == std::string::npos ? 0 : f.path.rfind('.') + 1);
                if (last == std::to_string(k)) return displayName(*f.mod, f.path);
            }
            return "function";
        }
        if (k < regexpMarks_.size() && regexpMarks_[k]) return "compiled pattern";
        if (JS_IsString(v)) {
            size_t len = 0;
            const char* s = JS_ToCStringLen(ctx_, &len, v);
            std::string r = s ? quoted(s, len) : "?";
            if (s) JS_FreeCString(ctx_, s);
            return r;
        }
        std::string r = cstr(ctx_, JS_ToCString(ctx_, v));
        return JS_IsBigInt(ctx_, v) ? r + "n" : r;
    }

    void code(void* qjs, const ProtoBytecodeModule& mod, const std::vector<Function>& nested) {
        const uint8_t* buf = mod.buf(pc_);
        const int len = mod.bufLen(pc_);
        const OpInfo* ops = opTable();
        out_ << "  code:\n";
        const int width = len >= 1000 ? 5 : (len >= 100 ? 4 : 3);
        int pc = 0;
        while (pc < len) {
            const uint8_t op = buf[pc];
            const OpInfo& info = ops[op];
            std::ostringstream line;
            line << "    " << std::setw(width) << pc << "  ";
            if (!info.name || info.size == 0 || pc + info.size > len) {
                line << "unknown opcode " << (unsigned)op;
                out_ << line.str() << "\n";
                break;  // alignment is lost
            }
            std::string operands, comment;
            decode(qjs, mod, nested, buf, pc, info, operands, comment);
            line << std::left << std::setw(26) << info.name << std::right;
            std::string text = line.str() + operands;
            if (!comment.empty()) {
                const size_t col = 4 + width + 2 + 26 + 12;
                if (text.size() < col) text.append(col - text.size(), ' ');
                else text += "  ";
                text += "; " + comment;
            }
            while (!text.empty() && text.back() == ' ') text.pop_back();
            out_ << text << "\n";
            pc += info.size;
        }
    }

    void decode(void* qjs, const ProtoBytecodeModule& mod, const std::vector<Function>& nested,
                const uint8_t* buf, int pc, const OpInfo& info, std::string& operands,
                std::string& comment) {
        const uint8_t* p = buf + pc + 1;
        const uint8_t op = buf[pc];
        std::ostringstream o, c;
        auto target = [&](long t) { c << "-> " << t; };
        switch (info.fmt) {
            case F_none:
                break;
            case F_none_int: {
                // push_minus1 .. push_7: the value is implied by the opcode.
                c << (int)op - (int)OP_push_0;
                break;
            }
            case F_none_loc: {
                // get_locN / put_locN / set_locN
                const unsigned k = (unsigned)((op - OP_get_loc0) % 4);
                c << localName(qjs, mod, k);
                break;
            }
            case F_none_arg: {
                const unsigned k = (unsigned)((op - OP_get_arg0) % 4);
                c << argName(qjs, k);
                break;
            }
            case F_none_var_ref: {
                const unsigned k = (unsigned)((op - OP_get_var_ref0) % 4);
                c << varRefName(mod, k);
                break;
            }
            case F_u8:
                o << (unsigned)p[0];
                if (op == OP_special_object) c << specialObjectName(p[0]);
                else if (op == OP_for_of_next) c << "depth " << (unsigned)p[0];
                else if (op == OP_iterator_call)
                    c << ((p[0] & 1) ? "throw" : "return") << ((p[0] & 2) ? ", no argument" : "");
                break;
            case F_i8:
                o << (int)(int8_t)p[0];
                break;
            case F_loc8:
                o << (unsigned)p[0];
                c << localName(qjs, mod, p[0]);
                break;
            case F_const8:
                o << (unsigned)p[0];
                c << constName(qjs, nested, p[0]);
                break;
            case F_label8:
                o << (int)(int8_t)p[0];
                target(pc + 1 + (int8_t)p[0]);
                break;
            case F_u16:
                o << u16(p);
                if (op == OP_apply) c << ((u16(p) & 1) ? "construct" : "call");
                else if (op == OP_rest) c << "rest from arg " << u16(p);
                break;
            case F_i16:
                o << (int16_t)u16(p);
                break;
            case F_label16:
                o << (int16_t)u16(p);
                target(pc + 1 + (int16_t)u16(p));
                break;
            case F_npop:
                o << u16(p);
                if (op == OP_array_from)
                    c << u16(p) << (u16(p) == 1 ? " element" : " elements");
                else
                    c << u16(p) << (u16(p) == 1 ? " arg" : " args");
                break;
            case F_npopx:
                c << (int)op - (int)OP_call0 << ((int)op - (int)OP_call0 == 1 ? " arg" : " args");
                break;
            case F_npop_u16:
                o << u16(p) << " " << u16(p + 2);
                c << u16(p) << (u16(p) == 1 ? " arg" : " args") << ", scope " << u16(p + 2);
                break;
            case F_loc:
                o << u16(p);
                c << localName(qjs, mod, u16(p));
                break;
            case F_arg:
                o << u16(p);
                c << argName(qjs, u16(p));
                break;
            case F_var_ref:
                o << u16(p);
                c << varRefName(mod, u16(p));
                break;
            case F_u32:
                o << u32(p);
                break;
            case F_i32:
                o << (int32_t)u32(p);
                break;
            case F_const:
                o << u32(p);
                c << constName(qjs, nested, u32(p));
                break;
            case F_label:
                o << (int32_t)u32(p);
                target(pc + 1 + (int32_t)u32(p));
                break;
            case F_atom:
                if (op == OP_push_atom_value) {
                    const std::string v = atomName(ctx_, u32(p));
                    c << quoted(v.data(), v.size());
                } else {
                    c << atomName(ctx_, u32(p));
                }
                break;
            case F_atom_u8:
                o << (unsigned)p[4];
                c << atomName(ctx_, u32(p));
                if (op == OP_throw_error) c << ", " << throwErrorName(p[4]);
                break;
            case F_atom_u16:
                o << u16(p + 4);
                c << atomName(ctx_, u32(p));
                break;
            case F_atom_label_u8:
            case F_atom_label_u16:
                o << (int32_t)u32(p + 4);
                c << atomName(ctx_, u32(p)) << ", ";
                target(pc + 5 + (int32_t)u32(p + 4));
                break;
            case F_label_u16:
                o << (int32_t)u32(p) << " " << u16(p + 4);
                target(pc + 1 + (int32_t)u32(p));
                break;
            case F_loc8_loc8:
                o << (unsigned)p[0] << " " << (unsigned)p[1];
                c << localName(qjs, mod, p[0]) << " += " << localName(qjs, mod, p[1]);
                break;
            case F_loc8_loc8_rel32:
                o << (unsigned)p[0] << " " << (unsigned)p[1] << " " << (int32_t)u32(p + 2);
                c << "if !(" << localName(qjs, mod, p[0]) << " < " << localName(qjs, mod, p[1]) << ") ";
                target(pc + 7 + (int32_t)u32(p + 2));
                break;
            case F_loc8_varref_rel32:
                o << (unsigned)p[0] << " " << u16(p + 1) << " " << (int32_t)u32(p + 3);
                c << "if !(" << localName(qjs, mod, p[0]) << " < " << varRefName(mod, u16(p + 1)) << ") ";
                target(pc + 8 + (int32_t)u32(p + 3));
                break;
            case F_group: {
                const uint16_t g = u16(p), pos = u16(p + 2);
                o << g << " " << pos;
                if (g < mod.putFieldGroups.size() && pos < mod.putFieldGroups[g].atoms.size())
                    c << atomName(ctx_, mod.putFieldGroups[g].atoms[pos]) << ", write " << pos + 1
                      << " of " << mod.putFieldGroups[g].atoms.size();
                break;
            }
            default:
                break;
        }
        operands = o.str();
        comment = c.str();
    }
};

}  // namespace

void disassembleModule(JSContext* ctx, void* quickjsBytecode, const ProtoBytecodeModule& module,
                       proto::ProtoContext* pContext, std::ostream& out) {
    Lister(ctx, pContext, out).list(quickjsBytecode, module, "");
}

int disassembleSource(JSContext* ctx, proto::ProtoContext* parent, const std::string& source,
                      const std::string& filename, std::ostream& out, std::ostream& err) {
    // The flags of JSContextWrapper::eval, the path `protojs file.js` takes.
    void* bytecode = compileToBytecodeWithFlags(ctx, source.c_str(), source.size(),
                                                filename.c_str(), JS_EVAL_TYPE_GLOBAL, nullptr);
    if (!bytecode) {
        JSValue exc = JS_GetException(ctx);
        err << "protojs --disasm: " << filename << ": " << cstr(ctx, JS_ToCString(ctx, exc)) << "\n";
        JS_FreeValue(ctx, exc);
        return 1;
    }
    proto::ProtoContext frame(parent->space, parent, nullptr, nullptr, nullptr, nullptr);
    ProtoBytecodeModule module;
    if (!loadBytecode(ctx, bytecode, &frame, &module)) {
        err << "protojs --disasm: " << filename << ": loadBytecode failed\n";
        return 1;
    }
    // The module's objects are reachable only from this frame's young
    // generation while it is listed; nothing here polls a safepoint.
    disassembleModule(ctx, bytecode, module, &frame, out);
    return 0;
}

}  // namespace protojs
