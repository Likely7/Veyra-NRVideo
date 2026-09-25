#pragma once
// The QML front end's only view of the player.
//
// Design rules this file follows, all of them deliberate:
//
//   * QML never sees EngineController, D3D12, decoded frames or the audio
//     clock. It sees plain values and calls plain commands.
//   * The video is NOT part of the QML scene graph. It stays a native child
//     HWND with its own flip-model swapchain (measured in S3: 499/499 presents
//     at 0.205 ms while the QML scene animated at full panel rate). QML is drawn
//     around and above it. Compositing video into QML would cost exactly the
//     latency that is this product's advantage over OBS and PotPlayer.
//   * One timer drives updates, and it publishes only what changed: the facade
//     hands out a revision counter, so a 60 Hz tick does not mean 60 property
//     writes. This is what keeps the UI from becoming the latency bottleneck.
//
// Threading: every method here is called on the UI thread. Commands go through
// the facade, never executed inline, so a slow engine cannot block a frame of
// QML animation.
//
// Scope note: this exposes what the engine can actually do today. Playback
// speed is reported but has no setter (the engine has none), and export
// resolution is absent (the engine derives it from the NR size policy). A
// control that cannot change anything is worse than no control, so they are
// not offered until the engine side exists.
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "veyra/engine/EffectChain.h"
#include "veyra/engine/ExportJobManager.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/ui/PlayerUiFacade.h"

namespace veyra::ui {

class QmlPlayerBridge : public QObject {
    Q_OBJECT
    // --- professional-page readouts ---------------------------------------
    // Every one of these reports an engine value. Where the engine has no
    // measurement the getter says so (e.g. displayFpsKnown stays false, because
    // a display rate is only real if the system display event can be read - the
    // design requires exactly this distinction rather than reusing submit FPS).
    Q_PROPERTY(QString outputSummary READ outputSummary NOTIFY snapshotChanged)
    Q_PROPERTY(double displayFps READ displayFps NOTIFY snapshotChanged)
    Q_PROPERTY(bool displayFpsKnown READ displayFpsKnown NOTIFY snapshotChanged)
    Q_PROPERTY(double queuedFrames READ queuedFrames NOTIFY snapshotChanged)
    Q_PROPERTY(int skippedFrames READ skippedFrames NOTIFY snapshotChanged)
    Q_PROPERTY(QString flowBackend READ flowBackend NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList stageTimings READ stageTimings NOTIFY snapshotChanged)
    Q_PROPERTY(double stageBudgetMs READ stageBudgetMs NOTIFY snapshotChanged)
    // Total measured cost of the chain, or 0 when nothing has been measured.
    Q_PROPERTY(double chainTotalMs READ chainTotalMs NOTIFY snapshotChanged)
    Q_PROPERTY(double scheduleP95Ms READ scheduleP95Ms NOTIFY snapshotChanged)

    // --- frame generation settings ----------------------------------------
    Q_PROPERTY(QString fgBackendName READ fgBackendName WRITE setFgBackendName NOTIFY settingsChanged)
    Q_PROPERTY(int fgMaxMultiplier READ fgMaxMultiplier NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantList fgMultiplierChoices READ fgMultiplierChoices NOTIFY snapshotChanged)
    Q_PROPERTY(bool fgStrict READ fgStrict WRITE setFgStrict NOTIFY settingsChanged)
    Q_PROPERTY(bool fgLowQueue READ fgLowQueue WRITE setFgLowQueue NOTIFY settingsChanged)

    // --- colour settings ---------------------------------------------------
    Q_PROPERTY(double colorExposure READ colorExposure WRITE setColorExposure NOTIFY settingsChanged)
    Q_PROPERTY(double colorContrast READ colorContrast WRITE setColorContrast NOTIFY settingsChanged)
    Q_PROPERTY(double colorSaturation READ colorSaturation WRITE setColorSaturation NOTIFY settingsChanged)
    Q_PROPERTY(double colorTemperature READ colorTemperature WRITE setColorTemperature NOTIFY settingsChanged)

    // --- shell preferences -------------------------------------------------
    // Kept by the facade so they survive a restart, and read by the shell.
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY settingsChanged)
    Q_PROPERTY(QString defaultPage READ defaultPage WRITE setDefaultPage NOTIFY settingsChanged)
    Q_PROPERTY(QString defaultPageLabel READ defaultPageLabel NOTIFY settingsChanged)
    // Used by the shell to open on the saved page. Set from the command line in
    // tests so a screenshot does not depend on clicking the dock.
    Q_PROPERTY(QString initialPage READ initialPage WRITE setInitialPage NOTIFY settingsChanged)
    // The page the shell is showing, pushed by the shell so a command can tell
    // where the user is: opening a file from home goes to the player, and
    // opening one from the professional page stays there.
    Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY settingsChanged)

    // --- settings-page reporting -------------------------------------------
    Q_PROPERTY(QString remotePlayState READ remotePlayState NOTIFY snapshotChanged)
    // The loaded runtime components, as the engine reports them. A hand-written
    // list here could drift from what the package actually contains.
    Q_PROPERTY(QVariantList componentList READ componentList NOTIFY snapshotChanged)

    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

    // --- playback ----------------------------------------------------------
    Q_PROPERTY(QString statusText READ statusText NOTIFY snapshotChanged)
    Q_PROPERTY(bool running READ running NOTIFY snapshotChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY snapshotChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY snapshotChanged)
    Q_PROPERTY(bool isImage READ isImage NOTIFY snapshotChanged)
    Q_PROPERTY(bool isCapture READ isCapture NOTIFY snapshotChanged)
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY snapshotChanged)
    Q_PROPERTY(double position READ position NOTIFY snapshotChanged)
    Q_PROPERTY(double duration READ duration NOTIFY snapshotChanged)
    Q_PROPERTY(double progress READ progress NOTIFY snapshotChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY snapshotChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY snapshotChanged)

    // --- source ------------------------------------------------------------
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY snapshotChanged)
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY snapshotChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY snapshotChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY snapshotChanged)
    Q_PROPERTY(double sourceFps READ sourceFps NOTIFY snapshotChanged)
    Q_PROPERTY(int sourceRotation READ sourceRotation NOTIFY snapshotChanged)
    // The film's display aspect, so the window can snap to it in cinema mode
    // exactly as the prototype's fitAspect() does. 0 when nothing is open.
    Q_PROPERTY(double sourceAspect READ sourceAspect NOTIFY snapshotChanged)

    // --- live performance --------------------------------------------------
    // Submit FPS only. There is deliberately no "display FPS": the user asked
    // for it to be left out (MSI Afterburner reads the real panel rate, and a
    // number we cannot measure must not be shown as if we measured it).
    Q_PROPERTY(double submitFps READ submitFps NOTIFY snapshotChanged)
    Q_PROPERTY(bool submitFpsKnown READ submitFpsKnown NOTIFY snapshotChanged)
    Q_PROPERTY(double lateMs READ lateMs NOTIFY snapshotChanged)
    Q_PROPERTY(double lateP95Ms READ lateP95Ms NOTIFY snapshotChanged)
    Q_PROPERTY(QString metricsSummary READ metricsSummary NOTIFY snapshotChanged)
    Q_PROPERTY(bool nrActive READ nrActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool srActive READ srActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool fgActive READ fgActive NOTIFY snapshotChanged)
    Q_PROPERTY(bool captureRecovering READ captureRecovering NOTIFY snapshotChanged)
    Q_PROPERTY(double captureFps READ captureFps NOTIFY snapshotChanged)
    Q_PROPERTY(double captureDropped READ captureDropped NOTIFY snapshotChanged)
    Q_PROPERTY(QString backendWarning READ backendWarning NOTIFY snapshotChanged)

    // --- settings (what the UI shows) --------------------------------------
    Q_PROPERTY(bool nrEnabled READ nrEnabled WRITE setNrEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool srEnabled READ srEnabled WRITE setSrEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int fgMultiplier READ fgMultiplier WRITE setFgMultiplier NOTIFY settingsChanged)
    Q_PROPERTY(bool lowLatency READ lowLatency WRITE setLowLatency NOTIFY settingsChanged)
    Q_PROPERTY(bool nrTemporal READ nrTemporal WRITE setNrTemporal NOTIFY settingsChanged)
    Q_PROPERTY(bool videoHdr READ videoHdr WRITE setVideoHdr NOTIFY settingsChanged)
    Q_PROPERTY(int nrStyle READ nrStyle WRITE setNrStyle NOTIFY settingsChanged)
    Q_PROPERTY(double nrIntensity READ nrIntensity WRITE setNrIntensity NOTIFY settingsChanged)
    Q_PROPERTY(double nrTone READ nrTone WRITE setNrTone NOTIFY settingsChanged)
    Q_PROPERTY(double nrStructure READ nrStructure WRITE setNrStructure NOTIFY settingsChanged)
    Q_PROPERTY(double nrSkin READ nrSkin WRITE setNrSkin NOTIFY settingsChanged)
    Q_PROPERTY(bool nrAutoMask READ nrAutoMask WRITE setNrAutoMask NOTIFY settingsChanged)
    Q_PROPERTY(bool nrUiCorrection READ nrUiCorrection WRITE setNrUiCorrection NOTIFY settingsChanged)
    Q_PROPERTY(int videoSrQuality READ videoSrQuality WRITE setVideoSrQuality NOTIFY settingsChanged)
    Q_PROPERTY(bool protectionEnabled READ protectionEnabled WRITE setProtectionEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool colorEnabled READ colorEnabled WRITE setColorEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int audioOffsetMs READ audioOffsetMs WRITE setAudioOffsetMs NOTIFY settingsChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY snapshotChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY snapshotChanged)
    Q_PROPERTY(double playbackSpeed READ playbackSpeed NOTIFY snapshotChanged)
    // True while the engine has not yet applied what the UI asked for. The UI
    // shows the pending value and marks it, rather than lying about the state.
    Q_PROPERTY(bool applying READ applying NOTIFY snapshotChanged)

    // --- effect chain ------------------------------------------------------
    // The chain as data. List mode renders it; node mode renders the same
    // objects with positions. Both edit one chain, so switching modes never
    // loses work.
    Q_PROPERTY(QVariantList chain READ chain NOTIFY chainChanged)
    Q_PROPERTY(QVariantList effectCatalog READ effectCatalog CONSTANT)
    Q_PROPERTY(QString chainError READ chainError NOTIFY chainChanged)
    Q_PROPERTY(bool chainValid READ chainValid NOTIFY chainChanged)
    Q_PROPERTY(int nodeMode READ nodeMode WRITE setNodeMode NOTIFY chainChanged)

    // --- dialogs the UI opens ----------------------------------------------
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    // The name of the preset currently in effect, or "自定义" when the
    // settings no longer match any stored preset. Derived, never guessed.
    Q_PROPERTY(QString currentPresetName READ currentPresetName NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY snapshotChanged)
    Q_PROPERTY(int selectedAudioTrack READ selectedAudioTrack WRITE setSelectedAudioTrack NOTIFY snapshotChanged)
    // The last capture session the user actually ran, for the home page's
    // "continue" row. Absent until one exists: nothing is invented.
    Q_PROPERTY(bool hasCaptureSession READ hasCaptureSession NOTIFY snapshotChanged)
    Q_PROPERTY(QString captureSessionSummary READ captureSessionSummary NOTIFY snapshotChanged)
    Q_PROPERTY(QString colorStatus READ colorStatus NOTIFY snapshotChanged)
    Q_PROPERTY(QString videoHdrStatus READ videoHdrStatus NOTIFY snapshotChanged)

    // --- export ------------------------------------------------------------
    // Backed by the real ExportJobManager: state, progress and encoded counts come
    // from its snapshot, not from a counter this file keeps.
    Q_PROPERTY(bool exportRunning READ exportRunning NOTIFY exportChanged)
    Q_PROPERTY(bool exportPaused READ exportPaused NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportChanged)
    Q_PROPERTY(QString exportTarget READ exportTarget NOTIFY exportChanged)
    Q_PROPERTY(int exportEncoded READ exportEncoded NOTIFY exportChanged)
    Q_PROPERTY(int exportGenerated READ exportGenerated NOTIFY exportChanged)
    Q_PROPERTY(bool exportHevc READ exportHevc WRITE setExportHevc NOTIFY exportChanged)
    Q_PROPERTY(int exportBitrateMbps READ exportBitrateMbps WRITE setExportBitrateMbps NOTIFY exportChanged)
    // The preset the export will use. The design requires export to select a
    // preset (list or node) rather than assembling effects separately.
    Q_PROPERTY(QString exportPresetName READ exportPresetName NOTIFY exportChanged)
    // The super-resolution target, which is what actually decides the export size
    // in this engine (0 = source, then Qhd/Uhd4K/Uhd8K). A separate "export
    // resolution" field does not exist, so the UI exposes the real control instead
    // of a picker that would change nothing.
    Q_PROPERTY(int srTargetIndex READ srTargetIndex WRITE setSrTargetIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString srTargetLabel READ srTargetLabel NOTIFY settingsChanged)
    // Presets as a picker model: one preset concept, list and node together.
    Q_PROPERTY(QVariantList presetChoices READ presetChoices NOTIFY presetsChanged)

public:
    // `engine` must outlive the bridge. `dataDirectory` holds ui-session.v1 and
    // the preset library; empty means the default user data directory.
    QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory = {},
                    QObject* parent = nullptr);
    ~QmlPlayerBridge() override;

    QString outputSummary() const;
    double displayFps() const;
    bool displayFpsKnown() const;
    double queuedFrames() const;
    int skippedFrames() const;
    QString flowBackend() const;
    QVariantList stageTimings() const;
    double stageBudgetMs() const;
    double chainTotalMs() const;
    double scheduleP95Ms() const;

    QString fgBackendName() const;
    void setFgBackendName(const QString& value);
    int fgMaxMultiplier() const;
    QVariantList fgMultiplierChoices() const;
    bool fgStrict() const;
    void setFgStrict(bool value);
    bool fgLowQueue() const;
    void setFgLowQueue(bool value);

    double colorExposure() const;
    void setColorExposure(double v);
    double colorContrast() const;
    void setColorContrast(double v);
    double colorSaturation() const;
    void setColorSaturation(double v);
    double colorTemperature() const;
    void setColorTemperature(double v);

    bool reducedMotion() const;
    void setReducedMotion(bool value);
    QString defaultPage() const;
    void setDefaultPage(const QString& value);
    QString defaultPageLabel() const;
    QString initialPage() const;
    void setInitialPage(const QString& value);
    QString currentPage() const;
    void setCurrentPage(const QString& value);
    QString remotePlayState() const;
    QVariantList componentList() const;
    QString appName() const;
    QString version() const;

    // --- property accessors (Q_PROPERTY READ targets) -----------------------
    // Split into read-only and read-write so a reviewer can see at a glance
    // which values the UI can command and which it can only display.
    QString statusText() const;
    bool running() const;
    bool paused() const;
    bool failed() const;
    bool isImage() const;
    bool isCapture() const;
    bool hasSource() const;
    double position() const;
    double duration() const;
    double progress() const;
    QString positionText() const;
    QString durationText() const;

    QString sourceName() const;
    QString sourceSummary() const;
    int sourceWidth() const;
    int sourceHeight() const;
    double sourceFps() const;
    int sourceRotation() const;
    double sourceAspect() const;

    double submitFps() const;
    bool submitFpsKnown() const;
    double lateMs() const;
    double lateP95Ms() const;
    QString metricsSummary() const;
    bool nrActive() const;
    bool srActive() const;
    bool fgActive() const;
    bool captureRecovering() const;
    double captureFps() const;
    double captureDropped() const;
    QString backendWarning() const;

    bool applying() const;
    double volume() const;
    void setVolume(double value);
    bool muted() const;
    void setMuted(bool value);
    double playbackSpeed() const;

    bool nrEnabled() const;
    void setNrEnabled(bool value);
    bool srEnabled() const;
    void setSrEnabled(bool value);
    int fgMultiplier() const;
    void setFgMultiplier(int value);
    bool lowLatency() const;
    void setLowLatency(bool value);
    bool nrTemporal() const;
    void setNrTemporal(bool value);
    bool videoHdr() const;
    void setVideoHdr(bool value);
    int nrStyle() const;
    void setNrStyle(int value);
    double nrIntensity() const;
    void setNrIntensity(double value);
    double nrTone() const;
    void setNrTone(double value);
    double nrStructure() const;
    void setNrStructure(double value);
    double nrSkin() const;
    void setNrSkin(double value);
    bool nrAutoMask() const;
    void setNrAutoMask(bool value);
    bool nrUiCorrection() const;
    void setNrUiCorrection(bool value);
    int videoSrQuality() const;
    void setVideoSrQuality(int value);
    bool protectionEnabled() const;
    void setProtectionEnabled(bool value);
    bool colorEnabled() const;
    void setColorEnabled(bool value);
    int audioOffsetMs() const;
    void setAudioOffsetMs(int value);

    QVariantList chain() const;
    QVariantList effectCatalog() const;
    QString chainError() const;
    bool chainValid() const;
    int nodeMode() const;
    void setNodeMode(int mode);

    QVariantList recentFiles() const;
    QVariantList presets() const;
    QString currentPresetName() const;
    QVariantList audioTracks() const;
    int selectedAudioTrack() const;
    void setSelectedAudioTrack(int index);
    bool hasCaptureSession() const;
    QString captureSessionSummary() const;
    QString colorStatus() const;
    QString videoHdrStatus() const;

    bool exportRunning() const;
    bool exportPaused() const;
    double exportProgress() const;
    QString exportStatus() const;
    QString exportTarget() const;
    int exportEncoded() const;
    int exportGenerated() const;
    bool exportHevc() const;
    void setExportHevc(bool value);
    int exportBitrateMbps() const;
    void setExportBitrateMbps(int value);
    QString exportPresetName() const;
    int srTargetIndex() const;
    QString srTargetLabel() const;
    void setSrTargetIndex(int index);
    QVariantList presetChoices() const;

    // The native HWND the engine presents into. The UI creates it as a child
    // window of the QML window and hands it over; the bridge never draws QML
    // into it and Qt never touches its swapchain.
    void attachVideoWindow(qulonglong nativeHandle);

    // Run immediately before the engine opens a source. The presenter reads the
    // host window's client size once, when it initialises, and builds the
    // swapchain from it; a window that is still 0x0 at that moment yields a 1x1
    // surface that never recovers. The UI supplies this hook so the geometry is
    // settled at the one moment that matters, instead of racing a timer.
    void setPreOpenHook(std::function<void()> hook);

    // --- commands exposed to QML -------------------------------------------
    // Every one returns immediately; none touches the GPU or waits for the
    // engine.
    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openCaptureDialog();
    // Reopens the last capture session with the same device and format.
    Q_INVOKABLE void resumeCaptureSession();
    Q_INVOKABLE void openPs5Dialog();
    Q_INVOKABLE void openScreenCaptureDialog();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void seekBy(double seconds);
    Q_INVOKABLE void stepFrame(int direction);
    Q_INVOKABLE void takeScreenshot();
    Q_INVOKABLE void chooseExportPath();
    Q_INVOKABLE void startExport();
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void pauseExport(bool paused);
    Q_INVOKABLE void quit();
    // Returns this page's controls to the engine defaults (the design's
    // "重置本页"); it does not touch presets or other pages.
    Q_INVOKABLE void resetCurrentPage();

    // Chain editing. The validator decides whether an edit is allowed where it
    // was asked (frame generation is pinned last, RTX Video HDR immediately
    // before it) and reports why when it is not.
    Q_INVOKABLE int addEffect(const QString& type);
    Q_INVOKABLE bool removeEffect(int index);
    Q_INVOKABLE bool moveEffect(int from, int to);
    Q_INVOKABLE bool setEffectEnabled(int index, bool enabled);
    // Node-mode positions are UI state and persist with the chain.
    Q_INVOKABLE bool setEffectPosition(int index, double x, double y);
    Q_INVOKABLE QVariantMap effectDescriptor(const QString& type) const;

    // Presets. A preset carries selected parts, so there is one preset concept,
    // not several.
    Q_INVOKABLE bool applyPresetIndex(int index);
    Q_INVOKABLE bool savePresetAs(const QString& name, int contentsMask, bool nodeMode);
    Q_INVOKABLE bool deletePreset(int index);
    Q_INVOKABLE bool renamePreset(int index, const QString& name);
    Q_INVOKABLE bool duplicatePreset(int index);

    Q_INVOKABLE void refreshRecentFiles();
    Q_INVOKABLE void clearRecentFiles();
    Q_INVOKABLE QString formatTime(double seconds) const;

    // Diagnostics for the settings page: what the engine actually reports,
    // never a summary written by hand.
    Q_INVOKABLE QString diagnosticsReport() const;
    Q_INVOKABLE void openProjectPage();
    Q_INVOKABLE void copyDiagnostics();

signals:
    // Emitted only when the underlying snapshot actually changed, so QML
    // bindings do not re-evaluate 60 times a second for nothing.
    void snapshotChanged();
    void settingsChanged();
    void chainChanged();
    void recentFilesChanged();
    void presetsChanged();
    void exportChanged();
    // User-facing notices the UI shows as a transient message: a rejected
    // chain edit, an export that finished, a failed open.
    void notice(const QString& text, bool isError);
    // The UI moves to a page by name when a command implies it.
    void navigate(const QString& page);

private:
    // The settings the UI is showing: the facade's pending copy, which the
    // engine has been asked to apply. Every settings property reads this, so
    // they can never disagree with each other.
    engine::EnhancementSettings settings() const;
    // Reads the export job's snapshot and emits exportChanged.
    void pollExport();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace veyra::ui
