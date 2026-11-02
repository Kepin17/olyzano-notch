#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QQmlEngine;

// Keeping the memory down: on Linux glibc is told to use few malloc arenas (each thread otherwise gets its own 64 MB one that is
// never given back) and freed memory is returned after the panel has closed; on Windows the working set is handed back the same
// way. "Low memory mode" draws with Qt's software renderer on Linux and Windows (no GPU driver, no shader compiler: about 50 MB
// less, at the cost of some CPU). That mode is read before Qt starts, so a change takes effect after a restart.
class MemoryTuner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)                     // Linux and Windows
    Q_PROPERTY(bool lowMemory READ lowMemory WRITE setLowMemory NOTIFY changed) // the choice (next start)
    Q_PROPERTY(bool running READ running CONSTANT)                         // what this run is doing
    Q_PROPERTY(int restartOverMb READ restartOverMb WRITE setRestartOverMb NOTIFY changed) // 0 = never; else restart when idle and still above this
    Q_PROPERTY(bool freeWhenIdle READ freeWhenIdle WRITE setFreeWhenIdle NOTIFY changed) // let go of what the panel built, a while after it closed

public:
    explicit MemoryTuner(QObject *parent = nullptr);
    void setEngine(QQmlEngine *engine) { m_engine = engine; }

    // Before QGuiApplication exists: arena limit (Linux), and the renderer from the saved choice (Linux and Windows).
    static void applyEarly();

    bool available() const;
    bool lowMemory() const;
    void setLowMemory(bool on);
    bool running() const;
    int restartOverMb() const;
    void setRestartOverMb(int mb);
    bool freeWhenIdle() const;
    void setFreeWhenIdle(bool on);

    Q_INVOKABLE void trim();    // hands freed memory back to the system: the QML heap, unused components and pictures first, then malloc
    Q_INVOKABLE void restart(); // starts a new Notch and quits this one

    // What the watchdog needs to know from the window: is the panel closed, which page was last open, is anything running that must not be cut off.
    Q_INVOKABLE void setPanelIdle(bool idle);
    Q_INVOKABLE void setContext(const QString &page) { m_context = page; }
    Q_INVOKABLE void setBusy(bool busy) { m_busy = busy; }

signals:
    void changed();

private:
    void report(const char *why) const; // NOTCH_MEMLOG=1: one line of numbers, to find what holds memory
    void check();                       // every minute: an idle Notch that holds a lot gets trimmed, and what held it is written to memory.log

    QQmlEngine *m_engine = nullptr;
    QTimer m_log;
    QTimer m_watch;
    QElapsedTimer m_idleSince, m_reported;
    bool m_panelIdle = false, m_busy = false;
    int m_step = 0; // how many of the growth steps have been written down
    QString m_context;
};
