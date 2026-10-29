// Implementation of the small JSON reader used by the Bilibili API client.
#include "veyra/media/BiliJson.h"

#include <cmath>
#include <cstdlib>

namespace veyra::media::json {
namespace {

const Value& nullValue() {
    static const Value value;
    return value;
}

// Appends one code point as UTF-8.
void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

unsigned hexValue(char c) {
    if (c >= '0' && c <= '9') return unsigned(c - '0');
    if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
    return unsigned(c - 'A' + 10);
}

} // namespace

bool Value::asBool(bool fallback) const {
    if (type_ == Type::Bool) return bool_;
    // Bilibili sometimes encodes a flag as 0/1 rather than true/false.
    if (type_ == Type::Number) return number_ != 0.0;
    return fallback;
}

long long Value::asInt(long long fallback) const {
    if (type_ == Type::Number) return static_cast<long long>(number_);
    // A few endpoints hand back numbers as quoted strings.
    if (type_ == Type::String && !string_.empty()) {
        try {
            return std::stoll(string_);
        } catch (...) {
            return fallback;
        }
    }
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    return fallback;
}

double Value::asDouble(double fallback) const {
    if (type_ == Type::Number) return number_;
    if (type_ == Type::String && !string_.empty()) {
        try {
            return std::stod(string_);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

const std::string& Value::asString() const {
    static const std::string empty;
    return type_ == Type::String ? string_ : empty;
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ != Type::Object) return nullValue();
    const auto it = object_.find(key);
    return it == object_.end() ? nullValue() : it->second;
}

const Value& Value::operator[](size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) return nullValue();
    return array_[index];
}

size_t Value::size() const {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

const std::vector<Value>& Value::items() const {
    static const std::vector<Value> empty;
    return type_ == Type::Array ? array_ : empty;
}

const std::map<std::string, std::string>& Value::members() const {
    static const std::map<std::string, std::string> empty;
    return empty;
}

std::vector<const Value*> Value::elements() const {
    std::vector<const Value*> out;
    if (type_ != Type::Array) return out;
    out.reserve(array_.size());
    for (const auto& element : array_) out.push_back(&element);
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
class Parser {
public:
    Parser(const std::string& text, std::string* error) : text_(text), error_(error) {}

    bool run(Value& out) {
        // A UTF-8 BOM is not part of the document.
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF
            && static_cast<unsigned char>(text_[1]) == 0xBB
            && static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
        skipSpace();
        if (!parseValue(out, 0)) return false;
        skipSpace();
        return true;
    }

private:
    // Nesting deeper than this is not something the Bilibili API produces, and
    // the bound keeps a malformed document from exhausting the stack.
    static constexpr int kMaxDepth = 128;

    bool fail(const char* reason) {
        if (error_ && error_->empty()) {
            *error_ = std::string(reason) + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    void skipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool literal(const char* word) {
        const size_t length = std::char_traits<char>::length(word);
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        return true;
    }

    bool parseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (pos_ >= text_.size()) return fail("unexpected end of input");
        switch (text_[pos_]) {
        case '{': return parseObject(out, depth);
        case '[': return parseArray(out, depth);
        case '"': {
            std::string s;
            if (!parseString(s)) return false;
            out.type_ = Value::Type::String;
            out.string_ = std::move(s);
            return true;
        }
        case 't':
            if (!literal("true")) return fail("bad literal");
            out.type_ = Value::Type::Bool;
            out.bool_ = true;
            return true;
        case 'f':
            if (!literal("false")) return fail("bad literal");
            out.type_ = Value::Type::Bool;
            out.bool_ = false;
            return true;
        case 'n':
            if (!literal("null")) return fail("bad literal");
            out.type_ = Value::Type::Null;
            return true;
        default:
            return parseNumber(out);
        }
    }

    bool parseObject(Value& out, int depth) {
        ++pos_;   // '{'
        out.type_ = Value::Type::Object;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
        for (;;) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
            std::string key;
            if (!parseString(key)) return false;
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skipSpace();
            Value child;
            if (!parseValue(child, depth + 1)) return false;
            out.object_[std::move(key)] = std::move(child);
            skipSpace();
            if (pos_ >= text_.size()) return fail("unterminated object");
            if (text_[pos_] == ',') { ++pos_; continue; }
            if (text_[pos_] == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(Value& out, int depth) {
        ++pos_;   // '['
        out.type_ = Value::Type::Array;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
        for (;;) {
            skipSpace();
            Value child;
            if (!parseValue(child, depth + 1)) return false;
            out.array_.push_back(std::move(child));
            skipSpace();
            if (pos_ >= text_.size()) return fail("unterminated array");
            if (text_[pos_] == ',') { ++pos_; continue; }
            if (text_[pos_] == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    // Reads one \uXXXX escape at pos_ (which points at the 'u').
    bool parseUnicodeEscape(std::string& out) {
        if (pos_ + 4 >= text_.size()) return fail("truncated \\u escape");
        unsigned cp = 0;
        for (int i = 1; i <= 4; ++i) {
            if (!isHex(text_[pos_ + i])) return fail("bad \\u escape");
            cp = (cp << 4) | hexValue(text_[pos_ + i]);
        }
        pos_ += 4;
        // A high surrogate pairs with the low surrogate that must follow.
        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 < text_.size()
            && text_[pos_ + 1] == '\\' && text_[pos_ + 2] == 'u') {
            unsigned low = 0;
            bool valid = true;
            for (int i = 3; i <= 6; ++i) {
                if (!isHex(text_[pos_ + i])) { valid = false; break; }
                low = (low << 4) | hexValue(text_[pos_ + i]);
            }
            if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                pos_ += 6;
            }
        }
        appendUtf8(out, cp);
        return true;
    }

    bool parseString(std::string& out) {
        ++pos_;   // opening quote
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') { ++pos_; return true; }
            if (c != '\\') { out.push_back(c); ++pos_; continue; }
            ++pos_;
            if (pos_ >= text_.size()) return fail("truncated escape");
            const char e = text_[pos_];
            switch (e) {
            case '"': out.push_back('"'); ++pos_; break;
            case '\\': out.push_back('\\'); ++pos_; break;
            case '/': out.push_back('/'); ++pos_; break;
            case 'b': out.push_back('\b'); ++pos_; break;
            case 'f': out.push_back('\f'); ++pos_; break;
            case 'n': out.push_back('\n'); ++pos_; break;
            case 'r': out.push_back('\r'); ++pos_; break;
            case 't': out.push_back('\t'); ++pos_; break;
            case 'u':
                // pos_ points at 'u'; parseUnicodeEscape reads from there.
                if (!parseUnicodeEscape(out)) return false;
                ++pos_;
                break;
            default:
                return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(Value& out) {
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++pos_;
            else break;
        }
        if (pos_ == start) return fail("unexpected character");
        const std::string token = text_.substr(start, pos_ - start);
        try {
            out.type_ = Value::Type::Number;
            out.number_ = std::stod(token);
        } catch (...) {
            return fail("bad number");
        }
        return true;
    }

    const std::string& text_;
    std::string* error_;
    size_t pos_ = 0;
};

Value parse(const std::string& text, std::string* error) {
    if (error) error->clear();
    Parser parser(text, error);
    Value root;
    if (!parser.run(root)) return Value{};
    return root;
}

} // namespace veyra::media::json
