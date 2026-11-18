// A small JSON reader for the Bilibili web API.
//
// The module talks to api.bilibili.com and reads a handful of fields out of
// responses that mix objects, arrays, strings, numbers and booleans. The earlier
// substring helpers only understood `"key":"value"` and `"key":123`, so every
// field that was an object (`data.owner`, `data.stat`), an array (`data.result`,
// `data.item`) or a boolean (`data.isLogin`) silently read as "missing". That is
// why search returned nothing and a signed-in account still looked anonymous.
//
// This is a complete reader: it parses the whole document into a tree once, then
// the callers navigate it. It handles nested objects/arrays, the standard escape
// sequences, \uXXXX (including surrogate pairs) and bare true/false/null.
//
// Deliberately small and dependency-free: veyra_media does not link Qt and the
// build should not grow a JSON library for one API client.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace veyra::media::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Typed accessors. They return the fallback when the value has another type,
    // so callers can chain them without checking every level by hand.
    bool asBool(bool fallback = false) const;
    long long asInt(long long fallback = 0) const;
    double asDouble(double fallback = 0.0) const;
    const std::string& asString() const;

    // Object member lookup. A missing key yields a shared null value, so
    // `root["data"]["item"]` is safe even when an intermediate level is absent.
    const Value& operator[](const std::string& key) const;

    // Array element lookup. Out-of-range yields a shared null value.
    const Value& operator[](size_t index) const;

    size_t size() const;                 // array length, or object member count
    const std::vector<Value>& items() const;   // array elements
    const std::map<std::string, std::string>& members() const;   // object keys

    // Convenience for the common "array of objects" shape: returns the elements
    // of `*this` when it is an array, otherwise an empty vector.
    std::vector<const Value*> elements() const;

private:
    friend class Parser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

// Parses `text`. On failure returns a null Value and, when `error` is given,
// fills it with a short reason. `text` may carry a UTF-8 BOM.
Value parse(const std::string& text, std::string* error = nullptr);

} // namespace veyra::media::json
