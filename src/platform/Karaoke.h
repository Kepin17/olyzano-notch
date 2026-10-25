#pragma once

#include <QObject>

// The karaoke button: your microphone is played through the speakers or headphones next to the music, as `karaoke`
// in QML. Linux only (a PulseAudio / PipeWire loopback through `pactl`); elsewhere `supported` is false and the
// button stays hidden. The loop is removed when it is switched off, when Notch quits, and at the next start if
// Notch was killed with it on.
class Karaoke : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit Karaoke(QObject *parent = nullptr);
    ~Karaoke() override;

    bool supported() const { return m_supported; }
    bool active() const { return m_module >= 0; }
    QString error() const { return m_error; }

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void stop();

signals:
    void changed();

private:
    void removeStale();

    bool m_supported = false;
    int m_module = -1;
    QString m_error;
};
