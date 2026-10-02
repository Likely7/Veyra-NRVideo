#include "veyra/ui/UiLanguage.h"
#include "veyra/Log.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QTranslator>

#include <algorithm>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

namespace veyra::ui::i18n {
namespace {

struct Pattern {
    QRegularExpression match;
    QStringList pieces;   // translation split at its "{}" slots: pieces.size() == slot count + 1
};

// Looks strings up by source text, whatever the context: QML gives the component
// name and C++ the class name, and one table serves both.
class CatalogTranslator final : public QTranslator {
public:
    QHash<QString, QString> exact;
    std::vector<Pattern> patterns;
    // A disambiguation ("off" for 关闭 as a setting, against 关闭 = close a window)
    // is its own entry: key = source U+0004 disambiguation; the plain source otherwise.
    QString translate(const char*, const char* source, const char* disambiguation, int) const override {
        if (!source) return {};
        const QString key = QString::fromUtf8(source);
        if (disambiguation && *disambiguation) {
            const auto it = exact.constFind(key + QChar(0x4) + QString::fromUtf8(disambiguation));
            if (it != exact.constEnd()) return *it;
        }
        const auto it = exact.constFind(key);
        return it == exact.constEnd() ? QString() : *it;
    }
    bool isEmpty() const override { return exact.isEmpty(); }
};

std::mutex lock;                                  // text() runs on worker threads too
std::unique_ptr<CatalogTranslator> active;
QString currentCode = QStringLiteral("zh-CN");
QHash<QString, QString> cache;

bool hasHan(const QString& s) {
    for (const QChar c : s) if (c.unicode() >= 0x3400 && c.unicode() <= 0x9fff) return true;
    return false;
}

// "主机搜索发生错误（{}），详情见日志" -> ^主机搜索发生错误（(.+?)），详情见日志$
// std::format specs ({:.1f}, {:08X}) are slots too.
QRegularExpression patternOf(const QString& source, int& slotCount) {
    static const QRegularExpression slot(QStringLiteral("\\{[^{}]*\\}"));
    QString rx = QStringLiteral("^");
    qsizetype at = 0;
    slotCount = 0;
    auto it = slot.globalMatch(source);
    while (it.hasNext()) {
        const auto m = it.next();
        rx += QRegularExpression::escape(source.mid(at, m.capturedStart() - at)) + QStringLiteral("(.+?)");
        at = m.capturedEnd();
        ++slotCount;
    }
    rx += QRegularExpression::escape(source.mid(at)) + QStringLiteral("$");
    return QRegularExpression(rx, QRegularExpression::DotMatchesEverythingOption);
}

QStringList piecesOf(const QString& translation) {
    static const QRegularExpression slot(QStringLiteral("\\{[^{}]*\\}"));
    return translation.split(slot);
}

std::unique_ptr<CatalogTranslator> load(const QString& code) {
    QFile file(QStringLiteral(":/i18n/catalog.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        veyra::log::warn("i18n", "catalog resource missing; interface stays in the source language");
        return nullptr;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    auto t = std::make_unique<CatalogTranslator>();
    for (const auto value : doc.object().value(QStringLiteral("entries")).toArray()) {
        const auto e = value.toObject();
        const QString source = e.value(QStringLiteral("zh")).toString();
        const QString translated = e.value(code).toString();
        if (source.isEmpty() || translated.isEmpty()) continue;
        const QString context = e.value(QStringLiteral("ctx")).toString();
        if (!context.isEmpty()) { t->exact.insert(source + QChar(0x4) + context, translated); continue; }
        t->exact.insert(source, translated);
        const bool formatted = source.contains(QLatin1Char('{')) && source.contains(QLatin1Char('}'));
        // A message that ends in a colon is usually followed by a path or a reason.
        const bool prefix = source.endsWith(QStringLiteral("：")) || source.endsWith(QStringLiteral(": "));
        if (formatted || prefix) {
            int slotCount = 0;
            const QString shape = prefix && !formatted ? source + QStringLiteral("{}") : source;
            const QString target = prefix && !formatted ? translated + QStringLiteral("{}") : translated;
            auto rx = patternOf(shape, slotCount);
            auto pieces = piecesOf(target);
            if (slotCount > 0 && pieces.size() == slotCount + 1 && rx.isValid()) t->patterns.push_back({std::move(rx), std::move(pieces)});
        }
    }
    // Longer patterns first: a short "…：{}" must not swallow a longer specific message.
    std::stable_sort(t->patterns.begin(), t->patterns.end(), [](const Pattern& a, const Pattern& b) {
        return a.match.pattern().size() > b.match.pattern().size();
    });
    veyra::log::info("i18n", std::format("catalog {} entries={} patterns={}", code.toStdString(), t->exact.size(), t->patterns.size()));
    return t->exact.isEmpty() ? nullptr : std::move(t);
}

QString translateWhole(const CatalogTranslator& t, const QString& s) {
    if (const auto it = t.exact.constFind(s); it != t.exact.constEnd()) return *it;
    for (const auto& p : t.patterns) {
        const auto m = p.match.match(s);
        if (!m.hasMatch()) continue;
        QString out = p.pieces.front();
        for (int i = 1; i < p.pieces.size(); ++i) {
            const QString filled = m.captured(i);
            // A slot holding Chinese text of its own (a nested message) is translated too.
            out += hasHan(filled) ? translateWhole(t, filled) : filled;
            out += p.pieces[i];
        }
        return out;
    }
    return {};
}

QString translatePieces(const CatalogTranslator& t, const QString& s, int depth) {
    if (const QString whole = translateWhole(t, s); !whole.isEmpty()) return whole;
    if (depth > 2) return s;
    // A technical tag after the words ("低延迟 [format 16]") stays as it is.
    static const QRegularExpression tagged(QStringLiteral("^(.*?)(\\s*\\[[^\\[\\]]*\\])$"));
    if (const auto m = tagged.match(s); m.hasMatch() && hasHan(m.captured(1)))
        return translatePieces(t, m.captured(1), depth + 1) + m.captured(2);
    static const QString separators[] = {QStringLiteral("\n"), QStringLiteral("；"), QStringLiteral(" · ")};
    for (const auto& sep : separators) {
        if (!s.contains(sep)) continue;
        QStringList parts = s.split(sep);
        for (auto& part : parts) {
            const QString trimmed = part.trimmed();
            if (!hasHan(trimmed)) continue;
            const QString done = translatePieces(t, trimmed, depth + 1);
            part.replace(trimmed, done);
        }
        // Full-width separators read oddly between Latin sentences.
        const QString glue = sep == QStringLiteral("；") && currentCode == QStringLiteral("en") ? QStringLiteral("; ") : sep;
        return parts.join(glue);
    }
    return s;
}

} // namespace

QString resolve(const QString& preference) {
    static const QStringList known{QStringLiteral("zh-CN"), QStringLiteral("zh-TW"), QStringLiteral("en"), QStringLiteral("ja")};
    if (known.contains(preference)) return preference;
    const QLocale system = QLocale::system();
    if (system.language() == QLocale::Chinese) {
        const auto territory = system.territory();
        const bool traditional = system.script() == QLocale::TraditionalChineseScript || territory == QLocale::Taiwan ||
                                 territory == QLocale::HongKong || territory == QLocale::Macao;
        return traditional ? QStringLiteral("zh-TW") : QStringLiteral("zh-CN");
    }
    if (system.language() == QLocale::Japanese) return QStringLiteral("ja");
    return QStringLiteral("en");
}

bool apply(const QString& code) {
    std::lock_guard guard(lock);
    if (active) { QCoreApplication::removeTranslator(active.get()); active.reset(); }
    cache.clear();
    currentCode = QStringLiteral("zh-CN");
    if (code == QStringLiteral("zh-CN")) { veyra::log::info("i18n", "interface language zh-CN (source)"); return true; }
    active = load(code);
    if (!active) return false;
    QCoreApplication::installTranslator(active.get());
    currentCode = code;
    veyra::log::info("i18n", std::format("interface language {}", code.toStdString()));
    return true;
}

QString current() {
    std::lock_guard guard(lock);
    return currentCode;
}

QString text(const QString& source) {
    std::lock_guard guard(lock);
    if (!active || source.isEmpty() || !hasHan(source)) return source;
    if (const auto it = cache.constFind(source); it != cache.constEnd()) return *it;
    const QString out = translatePieces(*active, source, 0);
    if (cache.size() > 4096) cache.clear();
    cache.insert(source, out);
    return out;
}

QString text(const std::wstring& source) {
    return text(QString::fromWCharArray(source.c_str(), int(source.size())));
}

} // namespace veyra::ui::i18n
