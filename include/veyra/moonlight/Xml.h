// SPDX-License-Identifier: GPL-3.0-only
// A very small XML reader for the host's answers (serverinfo, applist, pair,
// launch). They are a few kilobytes of plain elements; this is deliberately not
// a general XML parser: no DTD, no namespaces, the five predefined entities and
// numeric character references only, bounded size and depth.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::moonlight::xml {

struct Node {
    std::string name;
    std::map<std::string, std::string> attributes;
    std::string text;                 // concatenated character data (not trimmed)
    std::vector<Node> children;

    // Pre-order, first match among the descendants (like scanning start elements).
    const Node* find(std::string_view elementName) const;
    std::vector<const Node*> findAll(std::string_view elementName) const;
};

constexpr size_t kMaxDocumentBytes = size_t(1) << 20;
constexpr int kMaxDepth = 32;

// The root element, or nothing when the document is malformed or too large.
std::optional<Node> parse(std::string_view document);

std::string decodeEntities(std::string_view text);

// Text of the first element called `tag`, empty when it is absent.
std::string textOf(const Node& root, std::string_view tag);

// Hex <-> bytes (lower/upper case accepted when decoding; invalid input gives nothing).
std::string toHex(const std::string& bytes);
std::optional<std::string> fromHex(std::string_view hex);

} // namespace veyra::moonlight::xml
