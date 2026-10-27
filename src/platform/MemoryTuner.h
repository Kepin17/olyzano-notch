#pragma once

#include <QObject>

// Keeping the memory down on Linux: glibc is told to use few malloc arenas (each thread otherwise gets its own 64 MB one that is
// never given back), freed memory is returned after the panel has closed, and "Low memory mode" draws with Qt's software renderer
// (no GPU driver, no shader compiler: about 50 MB less, at the cost of some CPU). That mode is read before Qt starts, so a change
// takes effect after a restart.
class MemoryTuner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)                     // Linux only
    Q_PROPERTY(bool lowMemory READ lowMemory WRITE setLowMemory NOTIFY changed) // the choice (next start)
    Q_PROPERTY(bool running READ running CONSTANT)                         // what this run is doing

public:
    explicit MemoryTuner(QObject *parent = nullptr);

    // Before QGuiApplication exists: arena limit, and the renderer from the saved choice. Does nothing off Linux.
    static void applyEarly();

    bool available() const;
    bool lowMemory() const;
    void setLowMemory(bool on);
    bool running() const;

    Q_INVOKABLE void trim();    // hands freed memory back to the system
    Q_INVOKABLE void restart(); // starts a new Notch and quits this one

signals:
    void changed();
};
