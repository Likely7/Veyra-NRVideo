// Draws the Bilibili sign-in code as a PNG, as a data URL QML can show.
//
// This lives in its own translation unit on purpose. The encoder
// (third_party/qrcodegen) throws std::invalid_argument on bad input, and mixing
// that with the Qt translation units is what made the app abort at startup with
// 0xC0000409. Keeping the encoder behind a plain C++ interface means the Qt side
// never sees its headers or its exception semantics.
#include "veyra/media/QrImage.h"

#include "qrcodegen.hpp"

#include <QBuffer>
#include <QImage>
#include <QPainter>

#include <memory>

namespace veyra::media {

std::string qrPngBase64(const std::string& text, int edgePixels) {
    if (text.empty()) return {};
    const int edge = edgePixels < 96 ? 96 : (edgePixels > 1024 ? 1024 : edgePixels);

    // Byte mode at error level LOW: what the payload needs (a ~130 byte ASCII URL).
    // The encoder throws on bad input, so the call is guarded here rather than in
    // the Qt units.
    std::unique_ptr<qrcodegen::QrCode> owned;
    try {
        owned = std::make_unique<qrcodegen::QrCode>(
            qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::LOW));
    } catch (const std::exception&) {
        return {};
    }
    const qrcodegen::QrCode& code = *owned;
    const int modules = code.getSize();
    if (modules <= 0) return {};

    // Four light modules of quiet zone, as the standard requires for scanning.
    constexpr int kBorder = 4;
    const int total = modules + kBorder * 2;
    const int scale = edge / total > 0 ? edge / total : 1;
    const int pixels = total * scale;

    QImage image(pixels, pixels, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (int y = 0; y < modules; ++y) {
            for (int x = 0; x < modules; ++x) {
                if (!code.getModule(x, y)) continue;
                painter.drawRect((x + kBorder) * scale, (y + kBorder) * scale, scale, scale);
            }
        }
    }

    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    return png.toBase64().toStdString();
}

} // namespace veyra::media
