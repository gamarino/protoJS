#ifndef PROTOJS_JSONPARSER_H
#define PROTOJS_JSONPARSER_H

#include <string>
#include <map>
#include <vector>
#include <variant>

namespace protojs {

// Minimal JSON value for npm registry parsing (objects, arrays, strings, numbers, bool, null).
struct JsonValue {
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Data = std::variant<std::monostate, Object, Array, std::string, double, bool>;
    Data data;

    JsonValue() = default;
    explicit JsonValue(Object o) : data(std::move(o)) {}
    explicit JsonValue(Array a) : data(std::move(a)) {}
    explicit JsonValue(std::string s) : data(std::move(s)) {}
    explicit JsonValue(double n) : data(n) {}
    explicit JsonValue(bool b) : data(b) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(data); }
    bool isObject() const { return std::holds_alternative<Object>(data); }
    bool isArray() const { return std::holds_alternative<Array>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isBool() const { return std::holds_alternative<bool>(data); }

    Object& asObject() { return std::get<Object>(data); }
    const Object& asObject() const { return std::get<Object>(data); }
    Array& asArray() { return std::get<Array>(data); }
    const Array& asArray() const { return std::get<Array>(data); }
    std::string& asString() { return std::get<std::string>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    double asNumber() const { return std::get<double>(data); }
    bool asBool() const { return std::get<bool>(data); }

    // Safe getters for object keys (return null JsonValue if missing or wrong type).
    const JsonValue* get(const std::string& key) const;
    std::string getString(const std::string& key) const;
    double getNumber(const std::string& key, double defaultVal = 0) const;
};

// Parse JSON string; returns null JsonValue on parse error.
JsonValue JsonParse(const std::string& json);

} // namespace protojs

#endif // PROTOJS_JSONPARSER_H
