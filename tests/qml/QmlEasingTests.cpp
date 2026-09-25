// G0.6: the Theme easing curves, read back from a real QML NumberAnimation, against the
// design's CSS timing functions (prototypes/ui-redesign-2026-09-25/*.css, inside .vy):
//   --spring      linear(0,.009,.035 2.1%,.141 4.4%,.723 12.9%,.938 16.7%,1.017 20.2%,
//                        1.043 24%,1.035 28.4%,.998 38.5%,.99 44.1%,1.001 60.7%,1)
//   --spring-soft linear(0,.02,.08 3.2%,.3 7.5%,.84 17%,1.01 25%,1.025 31%,1.004 45%,1)
//   --out         cubic-bezier(.2,.8,.2,1)
// Reading the curve through QML matters: easing.bezierCurve silently falls back to
// linear when its length is not a multiple of 6, which is exactly the bug this catches.
#include <QCoreApplication>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QVariant>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {
using Stops = std::vector<std::pair<double, double>>;  // (input %, output)

// CSS linear(): points without a position are spread evenly between their neighbours.
Stops css_linear(std::vector<std::pair<double, double>> raw) {
    // raw: (output, position or -1)
    const size_t n = raw.size();
    raw.front().second = raw.front().second < 0 ? 0 : raw.front().second;
    raw.back().second = raw.back().second < 0 ? 100 : raw.back().second;
    for (size_t i = 1; i < n; ++i) {
        if (raw[i].second >= 0) continue;
        size_t j = i;
        while (raw[j].second < 0) ++j;
        const double a = raw[i - 1].second, b = raw[j].second;
        for (size_t k = i; k < j; ++k) raw[k].second = a + (b - a) * double(k - i + 1) / double(j - i + 1);
    }
    Stops s;
    for (auto& [v, p] : raw) s.push_back({p / 100.0, v});
    return s;
}

double eval_linear(const Stops& s, double t) {
    for (size_t i = 1; i < s.size(); ++i)
        if (t <= s[i].first) {
            const double span = s[i].first - s[i - 1].first;
            const double f = span > 0 ? (t - s[i - 1].first) / span : 1;
            return s[i - 1].second + (s[i].second - s[i - 1].second) * f;
        }
    return s.back().second;
}

double eval_bezier(double x1, double y1, double x2, double y2, double x) {
    auto bx = [&](double u) { return 3 * x1 * u * (1 - u) * (1 - u) + 3 * x2 * u * u * (1 - u) + u * u * u; };
    auto by = [&](double u) { return 3 * y1 * u * (1 - u) * (1 - u) + 3 * y2 * u * u * (1 - u) + u * u * u; };
    double lo = 0, hi = 1;
    for (int i = 0; i < 60; ++i) { const double m = (lo + hi) / 2; (bx(m) < x ? lo : hi) = m; }
    return by((lo + hi) / 2);
}

int failures = 0;

void compare(QQmlEngine& engine, const char* property, auto reference) {
    QQmlComponent c(&engine);
    c.setData(QByteArray("import QtQuick\nimport Veyra\nNumberAnimation { easing.bezierCurve: Theme.") + property + " }",
              QUrl(QStringLiteral("inline.qml")));
    std::unique_ptr<QObject> anim(c.create());
    if (!anim) {
        std::printf("FAIL %s: %s\n", property, qPrintable(c.errorString()));
        ++failures;
        return;
    }
    const auto curve = anim->property("easing").value<QEasingCurve>();
    double worst = 0, at = 0;
    for (int i = 0; i <= 200; ++i) {
        const double t = i / 200.0;
        const double e = std::abs(curve.valueForProgress(t) - reference(t));
        if (e > worst) { worst = e; at = t; }
    }
    const bool ok = worst <= 0.01;
    std::printf("%s %s: type=%d max error %.4f at t=%.3f\n", ok ? "PASS" : "FAIL", property, int(curve.type()), worst, at);
    if (!ok) ++failures;
}
}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VEYRA_QML_DIR));

    const Stops spring = css_linear({{0, -1}, {.009, -1}, {.035, 2.1}, {.141, 4.4}, {.723, 12.9}, {.938, 16.7},
                                     {1.017, 20.2}, {1.043, 24}, {1.035, 28.4}, {.998, 38.5}, {.99, 44.1},
                                     {1.001, 60.7}, {1, -1}});
    const Stops soft = css_linear({{0, -1}, {.02, -1}, {.08, 3.2}, {.3, 7.5}, {.84, 17}, {1.01, 25},
                                   {1.025, 31}, {1.004, 45}, {1, -1}});
    compare(engine, "spring", [&](double t) { return eval_linear(spring, t); });
    compare(engine, "springSoft", [&](double t) { return eval_linear(soft, t); });
    compare(engine, "easeOut", [](double t) { return eval_bezier(.2, .8, .2, 1, t); });
    std::printf("failures=%d\n", failures);
    return failures ? 1 : 0;
}
