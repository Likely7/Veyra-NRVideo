// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <deque>
#include <QJsonObject>
#include <memory>
#include <windows.h>
#include <imm.h>

#include "veyra/moonlight/InputRouter.h"
#include "veyra/remoteplay/KeyboardMousePad.h"

namespace veyra::engine { class EngineController; }

namespace veyra::ui {

// One foreground Raw Input registration for native PC input and mapped PS5 input.
class StreamInputCapture final : public QObject, public QAbstractNativeEventFilter, private moonlight::InputSink {
    Q_OBJECT
    Q_PROPERTY(bool keyboardEnabled READ keyboardEnabled WRITE setKeyboardEnabled NOTIFY mappingChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY mappingChanged)
    Q_PROPERTY(QVariantList drivingBindings READ drivingBindings NOTIFY mappingChanged)
    Q_PROPERTY(QString driveKeyLabel READ driveKeyLabel NOTIFY mappingChanged)
    Q_PROPERTY(bool driving READ driving NOTIFY drivingChanged)
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY mappingChanged)
    Q_PROPERTY(QString profile READ profile NOTIFY mappingChanged)
    Q_PROPERTY(QString unlockLabel READ unlockLabel NOTIFY mappingChanged)
    Q_PROPERTY(QVariantMap tuning READ tuning NOTIFY mappingChanged)
    Q_PROPERTY(int learning READ learning NOTIFY mappingChanged)
    Q_PROPERTY(QString feedback READ feedback NOTIFY mappingChanged)
    Q_PROPERTY(bool ps5Captured READ ps5Captured NOTIFY stateChanged)
    Q_PROPERTY(bool previewing READ previewing NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap padState READ padState NOTIFY padStateChanged)

public:
    explicit StreamInputCapture(engine::EngineController& engine, QString profilePath,
                                std::function<uint64_t()> ps5Target);
    ~StreamInputCapture() override;

    void setWindow(HWND topLevel);
    bool captured() const { return route_ == Route::Moonlight; }
    bool capture(bool on);
    bool capturePs5(bool on);

    bool keyboardEnabled() const { return keyboardEnabled_; }
    void setKeyboardEnabled(bool enabled);
    QVariantList bindings() const;
    QVariantList drivingBindings() const;
    QString driveKeyLabel() const;
    bool driving() const { return mapper_.driving(); }
    Q_INVOKABLE void selectDriving(bool on);
    Q_INVOKABLE void learnDriveKey(QObject* cancelButton);
    QVariantMap tuning() const;
    QStringList profiles() const {return profiles_.keys();}
    QString profile() const {return profile_;}
    QString unlockLabel() const;
    Q_INVOKABLE void selectProfile(const QString& name,bool create=false);
    Q_INVOKABLE void useTemplate(int index);
    Q_INVOKABLE bool toggleAction(int action) const;
    Q_INVOKABLE void setToggleAction(int action,bool on);
    int learning() const { return learning_; }
    QString feedback() const { return feedback_; }
    bool ps5Captured() const { return route_ == Route::Ps5; }
    bool previewing() const { return route_ == Route::Preview; }
    QVariantMap padState() const;
    Q_INVOKABLE void learn(int action, QObject* cancelButton = nullptr, bool drivingLayout = false);
    Q_INVOKABLE void clearBinding(int action, bool drivingLayout = false);
    Q_INVOKABLE void cancelLearning();
    Q_INVOKABLE void applyDefaults(bool drivingLayout = false);
    Q_INVOKABLE void resetMouseTuning();
    Q_INVOKABLE void setTuning(const QString& key, const QVariant& value);
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

private:
    using Mapper = remoteplay::KeyboardMousePad;
    enum class Route { None, Moonlight, Ps5, Preview };
    void key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) override;
    void mouseMove(int dx, int dy) override;
    void mouseButton(int button, bool down) override;
    void scroll(int delta, bool horizontal) override;
    bool begin(Route route);
    void end();
    void clipToWindow();
    void tick();
    void emitPad();
    void digital(int key, bool down);
    void bind(int key, int modifiers);
    void applyMouseTuning(const Mapper::Tuning& tuning);
    void suspendIme();
    void restoreIme();
    void readProfile(const QJsonObject& json);
    QJsonObject activeProfile() const;
    void save();
    void load();

    engine::EngineController& engine_;
    moonlight::InputRouter router_;
    QString profilePath_;
    std::function<uint64_t()> ps5Target_;
    HWND window_ = nullptr,imeWindow_=nullptr;
    HIMC oldIme_=nullptr;
    POINT savedCursor_{};
    bool restoreCursor_=false;
    std::deque<int> wheelQueue_;
    int64_t wheelGapUs_=0;
    struct Ps5MouseInput;
    std::unique_ptr<Ps5MouseInput> ps5MouseInput_;
    QJsonObject profiles_;
    QString profile_=QStringLiteral("Default");
    double previewSpeed_=0;
    Route route_ = Route::None;
    Mapper mapper_;
    std::shared_ptr<remoteplay::MouseStick> mouse_;
    QTimer timer_;
    remoteplay::ControllerState previewState_{};
    uint64_t target_ = 0;
    bool keyboardEnabled_ = false;
    QPointer<QObject> cancelButton_;
    int learning_ = -1;
    int learningModifier_ = 0;
    bool learningDriving_ = false;
    int wheelKey_ = 0;
    int64_t wheelReleaseUs_ = 0;
    QString feedback_;
};

} // namespace veyra::ui
