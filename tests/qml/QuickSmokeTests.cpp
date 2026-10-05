// G1.6: component smoke tests through Qt Quick Test (tests/qml/quick/tst_*.qml).
// The test source and Veyra module are resolved from the executable's staging
// directory. This prevents a staging run from silently reading the source tree.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QtQml/qqml.h>
#include <QtQuickTest/quicktest.h>

class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty(QStringLiteral("exportTestOutputDirectory"),qEnvironmentVariable("VEYRA_EXPORT_TEST_ARTIFACTS"));
        engine->addImportPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml")));
        // The registered inline NR card has the engine's creation context,
        // not the fixture item's lexical context. Supply its explicit UI-only
        // snapshot here. The separate real-player probes test the C++ bridge.
        QQmlComponent snapshot(engine);
        snapshot.setData(R"qml(import QtQml
QtObject {
    property var effectCapabilities: ({nr0:{available:true}})
    property var nrRuntimeChoices: [{id:"0",label:"RTX 50 · Lecram"}]
    property var nrLayers: [{index:1}]
    property int nrMotionSource: 1
    property string nrAutoStatus: ""
    property var nodeTimings: ({})
    property var preferences: ({exportStopsPlayback:false})
    property real playbackRate: 1
    property real duration: 100
    property bool hasSource: true
    property bool isCapture: false
    signal settingsChanged()
    signal chainChanged()
    signal snapshotChanged()
    function nrAutoSelectionReason(index) { return "Auto is unavailable in this four-layer UI fixture" }
    function resetNrLayer(index) {}
    function logUi(category, text) {}
})qml", QUrl());
        auto* model = snapshot.create(engine->rootContext());
        if (!model) qFatal("Cannot create explicit component snapshot: %s", qPrintable(snapshot.errorString()));
        model->setParent(engine);
        engine->rootContext()->setContextProperty(QStringLiteral("veyra"), model);
        // ProPage is private to the production shell's directory, not exported
        // by Veyra/qmldir. Register it only here to test its real inline controls.
        qmlRegisterType(QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("qml/Veyra/ProPage.qml"))), "VeyraTest", 1, 0, "ProPage");
        qmlRegisterType(QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("qml/Veyra/ExportPage.qml"))), "VeyraTest", 1, 0, "ExportPage");
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
