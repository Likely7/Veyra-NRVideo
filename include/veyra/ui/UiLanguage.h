#pragma once
// Interface language (设置 → 界面语言). Simplified Chinese is the source language:
// every user-visible string in QML (qsTr) and C++ (tr) is written in it and is
// itself the lookup key. i18n/catalog.json, compiled into the executable, maps each
// source string to zh-TW, en and ja; a missing entry shows the Chinese source.
//
// Logs stay in the source language on purpose: field logs are read by the developers.
#include <QString>
#include <string>

namespace veyra::ui::i18n {

// zh-CN (source), zh-TW, en, ja. "auto" (or empty) follows the Windows display
// language: Traditional Chinese for zh-TW / zh-HK / zh-MO, Simplified for other
// Chinese, Japanese for ja, English for everything else.
QString resolve(const QString& preference);
// Installs the translator for `code` on the application (zh-CN removes it).
// Returns false when the catalog has no column for it; the source text then shows.
bool apply(const QString& code);
QString current();

// Text that did not pass through tr()/qsTr(): engine status and error messages and
// fixed tables. Exact catalog match first, then the catalog's message patterns
// (std::format "{}" slots and "…：" prefixes), then piece by piece across "；",
// "\n" and " · ". Text with no match comes back unchanged.
QString text(const QString& source);
QString text(const std::wstring& source);

} // namespace veyra::ui::i18n
