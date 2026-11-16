#include "veyra/ui/UiLanguage.h"
#include <QCoreApplication>
#include <QDir>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class Setup final : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->addImportPath(QStringLiteral(VEYRA_VRR_SOURCE_ROOT "/qml"));
        const auto language = qEnvironmentVariable("VEYRA_TEST_UI_LANGUAGE", "zh-CN");
        if (!veyra::ui::i18n::apply(language)) qFatal("Could not load the test UI language");
        engine->setUiLanguage(language);
    }
};
int main(int argc, char** argv) {
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "vrr-profile", nullptr, &setup);
}
#include "QuickTests.moc"
