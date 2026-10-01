// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/Xml.h"

#include <cctype>
#include <cstdint>

namespace veyra::moonlight::xml {

namespace {

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.';
}

void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) out.push_back(char(code));
    else if (code < 0x800) { out.push_back(char(0xC0 | (code >> 6))); out.push_back(char(0x80 | (code & 0x3F))); }
    else if (code < 0x10000) {
        out.push_back(char(0xE0 | (code >> 12))); out.push_back(char(0x80 | ((code >> 6) & 0x3F))); out.push_back(char(0x80 | (code & 0x3F)));
    } else if (code <= 0x10FFFF) {
        out.push_back(char(0xF0 | (code >> 18))); out.push_back(char(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((code >> 6) & 0x3F))); out.push_back(char(0x80 | (code & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::optional<Node> parseDocument() {
        if (s_.size() > kMaxDocumentBytes) return std::nullopt;
        skipMisc();
        Node root;
        if (!parseElement(root, 0)) return std::nullopt;
        return root;
    }

private:
    bool atEnd() const { return i_ >= s_.size(); }
    bool startsWith(std::string_view prefix) const { return s_.substr(i_, prefix.size()) == prefix; }
    void skipSpace() { while (!atEnd() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_; }

    // Whitespace, the XML declaration, comments and a DOCTYPE before/after the root.
    void skipMisc() {
        for (;;) {
            skipSpace();
            if (startsWith("<?")) {
                const auto end = s_.find("?>", i_);
                if (end == std::string_view::npos) { i_ = s_.size(); return; }
                i_ = end + 2;
            } else if (startsWith("<!--")) {
                const auto end = s_.find("-->", i_ + 4);
                if (end == std::string_view::npos) { i_ = s_.size(); return; }
                i_ = end + 3;
            } else if (startsWith("<!DOCTYPE")) {
                const auto end = s_.find('>', i_);
                if (end == std::string_view::npos) { i_ = s_.size(); return; }
                i_ = end + 1;
            } else return;
        }
    }

    bool parseName(std::string& name) {
        const size_t start = i_;
        while (!atEnd() && isNameChar(s_[i_])) ++i_;
        if (i_ == start) return false;
        name.assign(s_.substr(start, i_ - start));
        return true;
    }

    bool parseAttributes(Node& node, bool& selfClosing) {
        selfClosing = false;
        for (;;) {
            skipSpace();
            if (atEnd()) return false;
            if (s_[i_] == '>') { ++i_; return true; }
            if (startsWith("/>")) { i_ += 2; selfClosing = true; return true; }
            std::string key;
            if (!parseName(key)) return false;
            skipSpace();
            if (atEnd() || s_[i_] != '=') return false;
            ++i_;
            skipSpace();
            if (atEnd() || (s_[i_] != '"' && s_[i_] != '\'')) return false;
            const char quote = s_[i_++];
            const auto end = s_.find(quote, i_);
            if (end == std::string_view::npos) return false;
            node.attributes[key] = decodeEntities(s_.substr(i_, end - i_));
            i_ = end + 1;
        }
    }

    bool parseElement(Node& node, int depth) {
        if (depth > kMaxDepth || atEnd() || s_[i_] != '<') return false;
        ++i_;
        if (!parseName(node.name)) return false;
        bool selfClosing = false;
        if (!parseAttributes(node, selfClosing)) return false;
        if (selfClosing) return true;
        for (;;) {
            if (atEnd()) return false;
            if (startsWith("</")) {
                i_ += 2;
                std::string closing;
                if (!parseName(closing) || closing != node.name) return false;
                skipSpace();
                if (atEnd() || s_[i_] != '>') return false;
                ++i_;
                return true;
            }
            if (startsWith("<!--")) {
                const auto end = s_.find("-->", i_ + 4);
                if (end == std::string_view::npos) return false;
                i_ = end + 3;
            } else if (startsWith("<![CDATA[")) {
                const auto end = s_.find("]]>", i_ + 9);
                if (end == std::string_view::npos) return false;
                node.text.append(s_.substr(i_ + 9, end - (i_ + 9)));
                i_ = end + 3;
            } else if (startsWith("<?")) {
                const auto end = s_.find("?>", i_);
                if (end == std::string_view::npos) return false;
                i_ = end + 2;
            } else if (s_[i_] == '<') {
                Node child;
                if (!parseElement(child, depth + 1)) return false;
                node.children.push_back(std::move(child));
            } else {
                const auto end = s_.find('<', i_);
                const auto stop = end == std::string_view::npos ? s_.size() : end;
                node.text.append(decodeEntities(s_.substr(i_, stop - i_)));
                i_ = stop;
            }
        }
    }

    std::string_view s_;
    size_t i_ = 0;
};

void collect(const Node& node, std::string_view name, std::vector<const Node*>& out) {
    for (const auto& child : node.children) {
        if (child.name == name) out.push_back(&child);
        collect(child, name, out);
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

const Node* Node::find(std::string_view elementName) const {
    for (const auto& child : children) {
        if (child.name == elementName) return &child;
        if (const Node* deeper = child.find(elementName)) return deeper;
    }
    return nullptr;
}

std::vector<const Node*> Node::findAll(std::string_view elementName) const {
    std::vector<const Node*> out;
    collect(*this, elementName, out);
    return out;
}

std::optional<Node> parse(std::string_view document) { return Parser(document).parseDocument(); }

std::string decodeEntities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out.push_back(text[i]); continue; }
        const auto semi = text.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) { out.push_back('&'); continue; }
        const auto entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (entity.size() > 1 && entity[0] == '#') {
            uint32_t code = 0;
            bool ok = true;
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            for (size_t k = hex ? 2 : 1; k < entity.size() && ok; ++k) {
                const int d = hex ? hexDigit(entity[k]) : (entity[k] >= '0' && entity[k] <= '9' ? entity[k] - '0' : -1);
                if (d < 0) ok = false; else code = code * (hex ? 16 : 10) + uint32_t(d);
                if (code > 0x10FFFF) ok = false;
            }
            if (ok && (hex ? entity.size() > 2 : true)) appendUtf8(out, code); else { out.push_back('&'); continue; }
        } else { out.push_back('&'); continue; }
        i = semi;
    }
    return out;
}

std::string textOf(const Node& root, std::string_view tag) {
    if (root.name == tag) return root.text;
    const Node* node = root.find(tag);
    return node ? node->text : std::string();
}

std::string toHex(const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) { out.push_back(digits[c >> 4]); out.push_back(digits[c & 15]); }
    return out;
}

std::optional<std::string> fromHex(std::string_view hex) {
    if (hex.size() % 2) return std::nullopt;
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexDigit(hex[i]), lo = hexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(char((hi << 4) | lo));
    }
    return out;
}

} // namespace veyra::moonlight::xml
