#include "JsonParser.h"
#include <cctype>
#include <sstream>

namespace protojs {

const JsonValue* JsonValue::get(const std::string& key) const {
    if (!isObject()) return nullptr;
    const auto& obj = asObject();
    auto it = obj.find(key);
    if (it == obj.end()) return nullptr;
    return &it->second;
}

std::string JsonValue::getString(const std::string& key) const {
    const JsonValue* v = get(key);
    if (!v || !v->isString()) return "";
    return v->asString();
}

double JsonValue::getNumber(const std::string& key, double defaultVal) const {
    const JsonValue* v = get(key);
    if (!v || !v->isNumber()) return defaultVal;
    return v->asNumber();
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : json_(s), pos_(0) {}

    JsonValue parse() {
        skipWhitespace();
        if (pos_ >= json_.size()) return JsonValue();
        return parseValue();
    }

private:
    const std::string& json_;
    size_t pos_;

    void skipWhitespace() {
        while (pos_ < json_.size() && (json_[pos_] == ' ' || json_[pos_] == '\t' || json_[pos_] == '\n' || json_[pos_] == '\r'))
            pos_++;
    }

    char peek() const { return pos_ < json_.size() ? json_[pos_] : '\0'; }
    char get() { return pos_ < json_.size() ? json_[pos_++] : '\0'; }

    JsonValue parseValue() {
        skipWhitespace();
        char c = peek();
        if (c == '{') return JsonValue(parseObject());
        if (c == '[') return JsonValue(parseArray());
        if (c == '"') return JsonValue(parseString());
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        if (c == 't') { expect("true"); return JsonValue(true); }
        if (c == 'f') { expect("false"); return JsonValue(false); }
        if (c == 'n') { expect("null"); return JsonValue(); }
        return JsonValue();
    }

    JsonValue::Object parseObject() {
        JsonValue::Object obj;
        get(); // '{'
        skipWhitespace();
        if (peek() == '}') { get(); return obj; }
        for (;;) {
            skipWhitespace();
            if (peek() != '"') break;
            std::string key = parseString();
            skipWhitespace();
            if (get() != ':') break;
            skipWhitespace();
            JsonValue val = parseValue();
            obj[std::move(key)] = std::move(val);
            skipWhitespace();
            char c = get();
            if (c == '}') break;
            if (c != ',') break;
        }
        return obj;
    }

    JsonValue::Array parseArray() {
        JsonValue::Array arr;
        get(); // '['
        skipWhitespace();
        if (peek() == ']') { get(); return arr; }
        for (;;) {
            arr.push_back(parseValue());
            skipWhitespace();
            char c = get();
            if (c == ']') break;
            if (c != ',') break;
        }
        return arr;
    }

    std::string parseString() {
        get(); // '"'
        std::string s;
        while (pos_ < json_.size()) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                if (e == 'u') {
                    // \uXXXX - simplified: skip 4 hex chars
                    for (int i = 0; i < 4 && pos_ < json_.size(); i++) get();
                    s += '?';
                } else if (e == 'n') s += '\n';
                else if (e == 'r') s += '\r';
                else if (e == 't') s += '\t';
                else if (e == '"' || e == '\\' || e == '/') s += e;
                else s += e;
            } else
                s += c;
        }
        return s;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') get();
        while (pos_ < json_.size()) {
            char c = peek();
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E' || c == '+' || (c == '-' && start < pos_))
                get();
            else
                break;
        }
        std::string numStr = json_.substr(start, pos_ - start);
        double n = 0;
        try { n = std::stod(numStr); } catch (...) {}
        return JsonValue(n);
    }

    void expect(const char* word) {
        while (*word && pos_ < json_.size() && get() == *word) word++;
    }
};

} // anonymous namespace

JsonValue JsonParse(const std::string& json) {
    Parser p(json);
    return p.parse();
}

} // namespace protojs
