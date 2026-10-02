// Interface language runtime (UiLanguage.h) against the real catalog.
#include "veyra/ui/UiLanguage.h"

#include <QCoreApplication>
#include <cstdio>

namespace {
int failures = 0;
void expect(const QString& got, const QString& want, const char* what) {
    if (got == want) return;
    ++failures;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.toUtf8().constData(), want.toUtf8().constData());
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace veyra::ui;

    expect(i18n::resolve(QStringLiteral("ja")), QStringLiteral("ja"), "explicit choice kept");
    if (!i18n::apply(QStringLiteral("en"))) { std::printf("FAIL catalog did not load\n"); return 1; }

    // Exact entry, through Qt's own translate() as qsTr()/tr() use it.
    expect(QCoreApplication::translate("Any", "导出完成"), QStringLiteral("Export finished"), "exact via translator");
    // Disambiguated entry, and the plain one beside it.
    expect(QCoreApplication::translate("Any", "关闭", "off"), QStringLiteral("Off"), "disambiguation");
    expect(QCoreApplication::translate("Any", "关闭"), QStringLiteral("Close"), "plain entry next to a disambiguated one");
    // std::format message from the engine: slots carried across.
    expect(i18n::text(QStringLiteral("主机搜索发生错误（5），详情见日志；可手填 IP。")),
           QStringLiteral("Host search hit an error (5); details in the log. You can enter the IP by hand."), "format pattern");
    // A message built from pieces joined by "；".
    expect(i18n::text(QStringLiteral("HDR 视频自动使用 HEVC Main10 编码；视频导出完成")),
           QStringLiteral("HDR video automatically uses HEVC Main10; Video export finished"), "segments");
    // A capture format label with a technical tag at the end.
    expect(i18n::text(QStringLiteral("1920 x 1080 @ 120.00 fps · NV12 · 原生 · 低延迟 [format 16]")),
           QStringLiteral("1920 x 1080 @ 120.00 fps · NV12 · Native · low latency [format 16]"), "tagged segment");
    // Unknown text and paths come back unchanged.
    expect(i18n::text(QStringLiteral("D:/视频/我的片子.mkv")), QStringLiteral("D:/视频/我的片子.mkv"), "path untouched");

    i18n::apply(QStringLiteral("zh-CN"));
    expect(QCoreApplication::translate("Any", "导出完成"), QStringLiteral("导出完成"), "source language");
    expect(i18n::text(QStringLiteral("导出完成")), QStringLiteral("导出完成"), "source language text()");

    std::printf("ui i18n tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
