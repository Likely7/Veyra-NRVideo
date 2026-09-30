// G1.6: component smoke tests through Qt Quick Test (tests/qml/quick/tst_*.qml).
// The test source and Veyra module are resolved from the executable's staging
// directory. This prevents a staging run from silently reading the source tree.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->addImportPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml")));
        // ProPage is private to the production shell's directory, not exported
        // by Veyra/qmldir. Register it only here to test its real inline controls.
        qmlRegisterType(QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("qml/Veyra/ProPage.qml"))), "VeyraTest", 1, 0, "ProPage");
    }
};

int main(int argc, char** argv) {
    const QString applicationDirectory = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    const QString sourceDirectory = QDir(applicationDirectory).filePath(QStringLiteral("qml-tests"));
    if (!QFileInfo::exists(QDir(sourceDirectory).filePath(QStringLiteral("tst_components.qml")))) {
        fprintf(stderr, "missing staged Qt Quick Test source directory: %s\n", qPrintable(sourceDirectory));
        return 2;
    }
    const QByteArray sourceDirectoryBytes = sourceDirectory.toLocal8Bit();
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "veyra_qml_quick_tests", sourceDirectoryBytes.constData(), &setup);
}

#include "QuickSmokeTests.moc"
