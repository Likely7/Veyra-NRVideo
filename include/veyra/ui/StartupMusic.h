// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QString>
#include <memory>

namespace veyra::ui {
// Startup music (2026-11-15): one file, played once independently of the startup intro
// (DuskIntro.qml) through the Media Foundation media engine in audio-only mode, Windows' own
// MP3 decoder. The intro takes its clock from position(), so its beats stay on the music.
// The user authorizes the file in runtime packages only, excluded from Git/source archives: read from
// media/startup-music.mp3 next to the program; without it nothing plays and the intro runs on
// its own clock.
class StartupMusic : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    // idle, loading, playing, paused, ended or error.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
public:
    explicit StartupMusic(QString path, QObject* parent = nullptr);
    ~StartupMusic() override;
    bool available() const;
    QString state() const;
    bool muted() const;
    void setMuted(bool muted);
    // Opens the file ahead of play(), so the first sound is not late. volume: 0..1.
    Q_INVOKABLE void prepare(double volume);
    Q_INVOKABLE void play();
    // For good: the music does not resume (a source opened).
    Q_INVOKABLE void pause();
    // Seconds of the music presented so far while it plays, else -1.
    Q_INVOKABLE double position() const;
    // Media engine events, delivered on this object's thread.
    void engineEvent(unsigned long event, unsigned long long param1, unsigned long param2);
signals:
    void stateChanged();
    void mutedChanged();
private:
    void setState(const QString& state);
    void release();
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
