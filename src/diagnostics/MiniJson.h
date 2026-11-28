// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A small JSON value with a strict parser and a writer, enough for the
// diagnostic snapshot, events and reports. Not a general-purpose library.
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace veyra::diagnostics::json {

struct Value;
using Array = std::vector<Value>;
// Insertion order is kept so written reports read in a deliberate order.
struct Object {
    std::vector<std::pair<std::string, Value>> items;
    Value& operator[](std::string_view key);
    const Value* find(std::string_view key) const;
    bool contains(std::string_view key) const { return find(key) != nullptr; }
};

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v = nullptr;
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v(b) {}
    Value(double d) : v(d) {}
    Value(int i) : v(double(i)) {}
    Value(unsigned i) : v(double(i)) {}
    Value(int64_t i) : v(double(i)) {}
    Value(uint64_t i) : v(double(i)) {}
    Value(long i) : v(double(i)) {}
    Value(unsigned long i) : v(double(i)) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(std::string_view s) : v(std::string(s)) {}
    Value(Array a) : v(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : v(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool isBool() const { return std::holds_alternative<bool>(v); }
    bool isNumber() const { return std::holds_alternative<double>(v); }
    bool isString() const { return std::holds_alternative<std::string>(v); }
    bool isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(v); }
    bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(v); }
    bool boolean(bool fallback = false) const { return isBool() ? std::get<bool>(v) : fallback; }
    double number(double fallback = 0) const { return isNumber() ? std::get<double>(v) : fallback; }
    const std::string& string() const { static const std::string empty; return isString() ? std::get<std::string>(v) : empty; }
    const Array& array() const { static const Array empty; return isArray() ? *std::get<std::shared_ptr<Array>>(v) : empty; }
    const Object& object() const { static const Object empty; return isObject() ? *std::get<std::shared_ptr<Object>>(v) : empty; }
    Array& mutableArray() { if (!isArray()) v = std::make_shared<Array>(); return *std::get<std::shared_ptr<Array>>(v); }
    Object& mutableObject() { if (!isObject()) v = std::make_shared<Object>(); return *std::get<std::shared_ptr<Object>>(v); }
    // obj["a"]["b"] style lookups that never throw.
    const Value& operator[](std::string_view key) const {
        static const Value none;
        if (const auto* found = object().find(key)) return *found;
        return none;
    }
};

inline Value& Object::operator[](std::string_view key) {
    for (auto& item : items) if (item.first == key) return item.second;
    items.emplace_back(std::string(key), Value());
    return items.back().second;
}
inline const Value* Object::find(std::string_view key) const {
    for (const auto& item : items) if (item.first == key) return &item.second;
    return nullptr;
}

namespace detail {
struct Parser {
    std::string_view s;
    size_t i = 0;
    int depth = 0;
    [[noreturn]] void fail(const char* what) const { throw std::runtime_error(std::string("JSON: ") + what + " at " + std::to_string(i)); }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool lit(std::string_view word) { if (s.substr(i, word.size()) == word) { i += word.size(); return true; } return false; }
    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    }
    uint32_t hex4() {
        if (i + 4 > s.size()) fail("short escape");
        uint32_t c = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            c <<= 4;
            if (h >= '0' && h <= '9') c |= uint32_t(h - '0');
            else if (h >= 'a' && h <= 'f') c |= uint32_t(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') c |= uint32_t(h - 'A' + 10);
            else fail("bad escape");
        }
        return c;
    }
    std::string str() {
        if (s[i] != '"') fail("expected string");
        ++i;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            const char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) fail("bad escape");
            const char e = s[i++];
            switch (e) {
            case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
            case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                uint32_t c1 = hex4();
                if (c1 >= 0xD800 && c1 < 0xDC00 && lit("\\u")) {
                    const uint32_t c2 = hex4();
                    c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                }
                utf8(out, c1);
                break;
            }
            default: fail("bad escape");
            }
        }
        if (i >= s.size()) fail("unterminated string");
        ++i;
        return out;
    }
    Value value() {
        if (++depth > 200) fail("too deep");
        ws();
        if (i >= s.size()) fail("unexpected end");
        Value result;
        const char c = s[i];
        if (c == '{') {
            ++i;
            Object object;
            ws();
            if (i < s.size() && s[i] == '}') ++i;
            else for (;;) {
                ws();
                std::string key = str();
                ws();
                if (i >= s.size() || s[i++] != ':') fail("expected ':'");
                object.items.emplace_back(std::move(key), value());
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                fail("expected ',' or '}'");
            }
            result = Value(std::move(object));
        } else if (c == '[') {
            ++i;
            Array array;
            ws();
            if (i < s.size() && s[i] == ']') ++i;
            else for (;;) {
                array.push_back(value());
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                fail("expected ',' or ']'");
            }
            result = Value(std::move(array));
        } else if (c == '"') {
            result = Value(str());
        } else if (lit("true")) result = Value(true);
        else if (lit("false")) result = Value(false);
        else if (lit("null")) result = Value(nullptr);
        else {
            const size_t start = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
            double d = 0;
            const auto r = std::from_chars(s.data() + start, s.data() + i, d);
            if (r.ec != std::errc() || i == start) fail("bad number");
            result = Value(d);
        }
        --depth;
        return result;
    }
};
inline void escape(std::string& out, std::string_view text) {
    out += '"';
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
            else out += char(c);
        }
    }
    out += '"';
}
inline void number(std::string& out, double d) {
    if (!std::isfinite(d)) { out += "null"; return; }
    if (std::abs(d) < 9e15 && d == std::floor(d)) { out += std::to_string(int64_t(d)); return; }
    char b[32];
    const auto r = std::to_chars(b, b + sizeof(b), d, std::chars_format::general, 6);
    out.append(b, r.ptr);
}
inline void write(std::string& out, const Value& value, int indent, int level) {
    const auto newline = [&](int l) { if (indent >= 0) { out += '\n'; out.append(size_t(l * indent), ' '); } };
    if (value.isNull()) out += "null";
    else if (value.isBool()) out += value.boolean() ? "true" : "false";
    else if (value.isNumber()) number(out, value.number());
    else if (value.isString()) escape(out, value.string());
    else if (value.isArray()) {
        const auto& a = value.array();
        if (a.empty()) { out += "[]"; return; }
        out += '[';
        // Short arrays of numbers stay on one line.
        const bool flat = a.size() <= 64 && std::all_of(a.begin(), a.end(), [](const Value& x) { return x.isNumber() || x.isNull(); });
        for (size_t k = 0; k < a.size(); ++k) {
            if (k) out += ',';
            if (!flat) newline(level + 1);
            write(out, a[k], flat ? -1 : indent, level + 1);
        }
        if (!flat) newline(level);
        out += ']';
    } else {
        const auto& o = value.object();
        if (o.items.empty()) { out += "{}"; return; }
        out += '{';
        for (size_t k = 0; k < o.items.size(); ++k) {
            if (k) out += ',';
            newline(level + 1);
            escape(out, o.items[k].first);
            out += indent >= 0 ? ": " : ":";
            write(out, o.items[k].second, indent, level + 1);
        }
        newline(level);
        out += '}';
    }
}
} // namespace detail

inline Value parse(std::string_view text) {
    detail::Parser p{text};
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) p.i = 3;   // BOM
    Value v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing data");
    return v;
}
inline std::string dump(const Value& value, int indent = 1) {
    std::string out;
    detail::write(out, value, indent, 0);
    return out;
}

}
