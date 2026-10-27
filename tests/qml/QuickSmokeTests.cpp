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
#include <QGuiApplication>
#include <QTest>
#include <QWindow>
#include <QScreen>
#include <QPixmap>

// Quick Test's keyClick targets its QQuickView host. Multi-window ownership
// tests need events delivered to the actual focused QML Window instead.
class ActiveWindowKeys : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool focused(QObject* window) { return QGuiApplication::focusWindow() == window; }
    Q_INVOKABLE bool savePresented(QObject* object,const QString& path) {
        auto* window=qobject_cast<QWindow*>(object);
        if(!window||!window->screen())return false;
        // Read already presented pixels, without triggering a fresh QML render.
        const auto image=window->screen()->grabWindow(window->winId());
        return !image.isNull()&&image.save(path);
    }
    Q_INVOKABLE bool press(int key) {
        auto* window = QGuiApplication::focusWindow();
        if (!window) return false;
        QTest::keyClick(window, Qt::Key(key));
        QCoreApplication::processEvents();
        return true;
    }
};

class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty(QStringLiteral("testWindowKeys"), new ActiveWindowKeys(engine));
        engine->rootContext()->setContextProperty(QStringLiteral("exportTestOutputDirectory"),qEnvironmentVariable("VEYRA_EXPORT_TEST_ARTIFACTS"));
        engine->addImportPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml")));
        // The registered inline NR card has the engine's creation context,
        // not the fixture item's lexical context. Supply its explicit UI-only
        // snapshot here. The separate real-player probes test the C++ bridge.
        QQmlComponent snapshot(engine);
        snapshot.setData(R"qml(import QtQml
QtObject {
    property var effectCapabilities: ({nr2:{available:true},nr3:{available:true},nr4:{available:true}})
    property var nrRuntimeChoices: [{id:"2",label:"RTX 20–50 · SF 310.9.16"},{id:"3",label:"RTX 50 · NVIDIA original"}]
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
    function observeHdrCoverage(on) {}
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
