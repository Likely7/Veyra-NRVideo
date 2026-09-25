// G1.6: component smoke tests through Qt Quick Test (tests/qml/quick/tst_*.qml).
// The Veyra module loads from the source tree's qml/ as plain files, like the app.
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine) { engine->addImportPath(QStringLiteral(VEYRA_QML_DIR)); }
};

QUICK_TEST_MAIN_WITH_SETUP(veyra_qml_quick_tests, Setup)

#include "QuickSmokeTests.moc"
