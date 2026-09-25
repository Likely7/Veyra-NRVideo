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

#include <memory>

#include "veyra/engine/EffectChain.h"
#include "veyra/engine/EnhancementSettings.h"
#include "veyra/ui/PlayerUiFacade.h"

namespace veyra::ui {

class QmlPlayerBridge : public QObject {
    Q_OBJECT
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
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY snapshotChanged)
    Q_PROPERTY(int selectedAudioTrack READ selectedAudioTrack WRITE setSelectedAudioTrack NOTIFY snapshotChanged)
    Q_PROPERTY(QString colorStatus READ colorStatus NOTIFY snapshotChanged)
    Q_PROPERTY(QString videoHdrStatus READ videoHdrStatus NOTIFY snapshotChanged)

    // --- export ------------------------------------------------------------
    Q_PROPERTY(bool exportRunning READ exportRunning NOTIFY exportChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportChanged)
    Q_PROPERTY(QString exportTarget READ exportTarget NOTIFY exportChanged)

public:
    // `engine` must outlive the bridge. `dataDirectory` holds ui-session.v1 and
    // the preset library; empty means the default user data directory.
    QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory = {},
                    QObject* parent = nullptr);
    ~QmlPlayerBridge() override;

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
    QVariantList audioTracks() const;
    int selectedAudioTrack() const;
    void setSelectedAudioTrack(int index);
    QString colorStatus() const;
    QString videoHdrStatus() const;

    bool exportRunning() const;
    QString exportStatus() const;
    QString exportTarget() const;

    // The native HWND the engine presents into. The UI creates it as a child
    // window of the QML window and hands it over; the bridge never draws QML
    // into it and Qt never touches its swapchain.
    void attachVideoWindow(qulonglong nativeHandle);

    // --- commands exposed to QML -------------------------------------------
    // Every one returns immediately; none touches the GPU or waits for the
    // engine.
    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openCaptureDialog();
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
    Q_INVOKABLE void quit();

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

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace veyra::ui
