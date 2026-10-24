// Renders the Bilibili sign-in code to a PNG data URL.
//
// The encoder itself lives in src/media/QrImage.cpp, a translation unit with no Qt
// headers: qrcodegen throws std::invalid_argument, and compiling it next to the Qt
// units made the process abort at startup (0xC0000409). Callers here only see a
// plain string.
#pragma once
#include <string>

namespace veyra::media {

// Returns a base64-encoded PNG (no data: prefix) of `text` as a QR symbol, or an
// empty string when the payload cannot be encoded. `edgePixels` is the target size
// of one symbol edge; the actual size snaps to a whole number of modules.
std::string qrPngBase64(const std::string& text, int edgePixels);

} // namespace veyra::media
