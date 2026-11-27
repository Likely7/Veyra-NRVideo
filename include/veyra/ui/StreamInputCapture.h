// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <atomic>
#include <thread>
#include <QJsonObject>
#include <memory>
#include <windows.h>
#include <imm.h>

#include "veyra/moonlight/InputRouter.h"
#include "veyra/remoteplay/KeyboardMousePad.h"
#include "veyra/remoteplay/MappedController.h"

namespace veyra::engine { class EngineController; }

namespace veyra::ui {

// One foreground Raw Input registration for native PC input and mapped PS5 input.
class StreamInputCapture final : public QObject, public QAbstractNativeEventFilter, private moonlight::InputSink {
    Q_OBJECT
    Q_PROPERTY(bool keyboardEnabled READ keyboardEnabled WRITE setKeyboardEnabled NOTIFY mappingChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY mappingChanged)
    Q_PROPERTY(QVariantList drivingBindings READ drivingBindings NOTIFY mappingChanged)
    Q_PROPERTY(QVariantList alternateBindings READ alternateBindings NOTIFY mappingChanged)
    Q_PROPERTY(QVariantList drivingAlternateBindings READ drivingAlternateBindings NOTIFY mappingChanged)
    Q_PROPERTY(QString driveKeyLabel READ driveKeyLabel NOTIFY mappingChanged)
    Q_PROPERTY(bool driving READ driving NOTIFY drivingChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY mappingChanged)
    Q_PROPERTY(QString profile READ profile NOTIFY mappingChanged)
    Q_PROPERTY(bool canRemoveProfile READ canRemoveProfile NOTIFY mappingChanged)
    Q_PROPERTY(QString unlockLabel READ unlockLabel NOTIFY mappingChanged)
    Q_PROPERTY(QString quickKeyLabel READ quickKeyLabel NOTIFY mappingChanged)
    Q_PROPERTY(bool quickReferenceEnabled MEMBER quickReferenceEnabled_)
    Q_PROPERTY(bool quickReferenceOpen READ quickReferenceOpen WRITE setQuickReferenceOpen)
    Q_PROPERTY(QStringList actionNotes READ actionNotes NOTIFY mappingChanged)
    Q_PROPERTY(QStringList drivingActionNotes READ drivingActionNotes NOTIFY mappingChanged)
    Q_PROPERTY(QVariantMap tuning READ tuning NOTIFY mappingChanged)
    Q_PROPERTY(int learning READ learning NOTIFY mappingChanged)
    Q_PROPERTY(int learningSlot READ learningSlot NOTIFY mappingChanged)
    Q_PROPERTY(QVariantMap bindingConflict READ bindingConflict NOTIFY mappingChanged)
    Q_PROPERTY(QString feedback READ feedback NOTIFY mappingChanged)
    Q_PROPERTY(bool ps5Captured READ ps5Captured NOTIFY stateChanged)
    Q_PROPERTY(bool xboxCaptured READ xboxCaptured NOTIFY stateChanged)
    Q_PROPERTY(bool ps5KeyboardEnabled READ ps5KeyboardEnabled NOTIFY mappingChanged)
    Q_PROPERTY(bool xboxKeyboardEnabled READ xboxKeyboardEnabled NOTIFY mappingChanged)
    Q_PROPERTY(bool xboxPlatform READ xboxPlatform NOTIFY mappingChanged)
    Q_PROPERTY(bool xboxOnlyProfile READ xboxOnlyProfile NOTIFY mappingChanged)
    Q_PROPERTY(bool nativeXbox READ nativeXbox WRITE setNativeXbox NOTIFY mappingChanged)
    Q_PROPERTY(bool previewing READ previewing NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap padState READ padState NOTIFY padStateChanged)
    Q_PROPERTY(QString aimStatus READ aimStatus NOTIFY aimStatusChanged)
    Q_PROPERTY(bool aimTesting READ aimTesting NOTIFY aimStatusChanged)
    Q_PROPERTY(bool aimArmed READ aimArmed NOTIFY aimStatusChanged)
    Q_PROPERTY(bool lowLatencyActive READ lowLatencyActive NOTIFY aimStatusChanged)

public:
    explicit StreamInputCapture(engine::EngineController& engine, QString profilePath,
                                std::function<uint64_t()> ps5Target, std::function<uint64_t()> xboxTarget = {});
    ~StreamInputCapture() override;

    void setWindow(HWND topLevel, HWND videoViewport = nullptr);
    bool captured() const { return route_ == Route::Moonlight; }
    bool capture(bool on);
    bool capturePs5(bool on);
    bool captureXbox(bool on);

    bool keyboardEnabled() const { return xboxPlatform_ ? xboxKeys_ : keyboardEnabled_; }
    bool ps5KeyboardEnabled() const { return keyboardEnabled_; }
    bool xboxKeyboardEnabled() const { return xboxKeys_; }
    bool xboxPlatform() const { return xboxPlatform_; }
    bool xboxOnlyProfile() const { return xboxOnlyProfile_; }
    bool nativeXbox() const { return nativeXbox_; }
    void setNativeXbox(bool enabled);
    Q_INVOKABLE void selectPlatform(bool xbox);
    Q_INVOKABLE void setXboxOnlyProfile(bool only);
    void setKeyboardEnabled(bool enabled);
    QVariantList bindings() const;
    QVariantList drivingBindings() const;
    QVariantList alternateBindings() const;
    QVariantList drivingAlternateBindings() const;
    QString driveKeyLabel() const;
    bool driving() const { return mapper_.driving(); }
    Q_INVOKABLE void selectDriving(bool on);
    Q_INVOKABLE void learnDriveKey(QObject* learningControls);
    Q_INVOKABLE void learnQuickKey(QObject* learningControls);
    Q_INVOKABLE void setActionNote(int action, bool driving, const QString& note);
    QVariantMap tuning() const;
    QString aimStatus() const { return aimStatus_; }
    bool aimTesting() const { return bool(aimTest_); }
    bool aimArmed() const { return armedAimMode_ != 0; }
    bool lowLatencyActive() const { return lowLatencyActive_; }
    QStringList profiles() const;
    QString profile() const {return profile_;}
    QString unlockLabel() const;
    QString quickKeyLabel() const;
    bool quickReferenceOpen() const { return quickReferenceOpen_.load(); }
    void setQuickReferenceOpen(bool open) { quickReferenceOpen_.store(open); }
    QStringList actionNotes() const { return notes_[0]; }
    QStringList drivingActionNotes() const { return notes_[1]; }
    Q_INVOKABLE void selectProfile(const QString& name,bool create=false);
    bool canRemoveProfile() const;
    Q_INVOKABLE void renameProfile(const QString& name);
    Q_INVOKABLE void removeProfile();
    Q_INVOKABLE void useTemplate(int index);
    Q_INVOKABLE bool toggleAction(int action) const;
    Q_INVOKABLE void setToggleAction(int action,bool on);
    int learning() const { return learning_; }
    int learningSlot() const { return learningSlot_; }
    QString feedback() const { return feedback_; }
    bool ps5Captured() const { return route_ == Route::Ps5; }
    bool xboxCaptured() const { return route_ == Route::Xbox; }
    bool previewing() const { return route_ == Route::Preview; }
    QVariantMap padState() const;
    Q_INVOKABLE void learn(int action, QObject* learningControls = nullptr, bool drivingLayout = false, int slot = 0);
    Q_INVOKABLE void learnPanelBinding(int action, QObject* learningControls, bool drivingLayout, int slot);
    QVariantMap bindingConflict() const;
    Q_INVOKABLE void resolveBindingConflict(bool move);
    Q_INVOKABLE void clearBinding(int action, bool drivingLayout = false, int slot = 0);
    Q_INVOKABLE void cancelLearning();
    Q_INVOKABLE void applyDefaults(bool drivingLayout = false);
    Q_INVOKABLE void resetMouseTuning();
    Q_INVOKABLE void setTuning(const QString& key, const QVariant& value);
    Q_INVOKABLE void selectAimAlgorithm(bool physical);
    Q_INVOKABLE void armAimTest(int mode);
    Q_INVOKABLE void preview(bool on);
    // Invoked while opening another panel or changing source.
    Q_INVOKABLE void releasePs5();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void capturedChanged(bool captured);
    void releaseRequested();
    void quitRequested();
    void statsRequested();
    void mappingChanged();
    void stateChanged();
    void padStateChanged();
    void drivingChanged();
    void quickReferenceRequested();
    void quickReferenceScrolled(int delta);
    void applicationHotkeyRequested(const QString& chord);
    void aimStatusChanged();

private:
    using Mapper = remoteplay::KeyboardMousePad;
    static constexpr int LearnQuickKey = Mapper::ActionCount + 1;
    static constexpr int DefaultQuickKey = VK_F2;
    enum class Route { None, Moonlight, Ps5, Xbox, Preview };
    void key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) override;
    void mouseMove(int dx, int dy) override;
    void mouseButton(int button, bool down) override;
    void scroll(int delta, bool horizontal) override;
    bool begin(Route route);
    void end();
    void clipToWindow();
    void tick();
    void tickAimTest();
    void setAimStatus(QString);
    void digital(int key, bool down);
    void bind(int key, int modifiers);
    bool quickKeyAvailable(int chord) const;
    bool unlockAvailable(int setting) const;
    int fallbackQuickKey() const;
    void applyMouseTuning(const Mapper::Tuning& tuning);
    void suspendIme(HWND window);
    void restoreIme();
    QVariantList bindingLabels(bool driving, int slot) const;
    void readProfile(const QJsonObject& json);
    QJsonObject activeProfile() const;
    bool save();
    void load();

    engine::EngineController& engine_;
    moonlight::InputRouter router_;
    QString profilePath_;
    std::function<uint64_t()> ps5Target_;
    std::function<uint64_t()> xboxTarget_;
    HWND window_ = nullptr, viewport_ = nullptr, previewWindow_ = nullptr, learningWindow_ = nullptr;
    HWND imeWindow_ = nullptr;
    HIMC oldIme_=nullptr;
    POINT savedCursor_{};
    bool restoreCursor_=false;
    struct ConsoleInputWorker;
    std::unique_ptr<ConsoleInputWorker> consoleInput_;
    std::shared_ptr<remoteplay::MappedController> mapped_;
    std::jthread previewSender_;
    std::shared_ptr<remoteplay::AimTest> aimTest_;
    int armedAimMode_ = 0;
    bool measuredFg_ = false, lowLatencyActive_ = false;
    int64_t aimStageAt_ = 0;
    QString aimStatus_;
    QJsonObject profiles_;
    QString profile_=QStringLiteral("默认");
    double previewSpeed_=0;
    Route route_ = Route::None;
    Mapper mapper_;
    QTimer timer_;
    remoteplay::ControllerState previewState_{};
    uint64_t target_ = 0;
    bool keyboardEnabled_ = false;
    bool xboxKeys_ = false, xboxPlatform_ = false, xboxOnlyProfile_ = false, nativeXbox_ = false;
    QString ps5Profile_, xboxProfile_;
    bool quickReferenceEnabled_ = false;
    std::atomic_bool quickReferenceOpen_{false};
    int quickKey_ = DefaultQuickKey;
    int quickKeyDown_ = 0;
    std::array<QStringList, 2> notes_;
    QPointer<QObject> learningControls_;
    int learning_ = -1;
    int learningSlot_ = 0;
    int learningModifier_ = 0;
    bool learningDriving_ = false;
    bool panelLearning_ = false;
    struct BindingConflict {
        int binding = 0;
        int source = -1;
        int action = -1;
        int slot = 0;
        bool driving = false;
    } conflict_;
    remoteplay::MappedController::Diagnostics inputDiagnostics_;
    QString feedback_;
};

} // namespace veyra::ui
