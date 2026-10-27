#include "MicCapture.h"

#include <QStandardPaths>
#include <QVariant>

#ifndef Q_OS_WIN

MicCapture::MicCapture(QObject *parent) : QObject(parent)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] {
        QByteArray data = m_rest + m_proc.readAllStandardOutput();
        m_rest = data.size() % 2 ? data.right(1) : QByteArray();
        if (data.size() % 2)
            data.chop(1);
        if (!data.isEmpty()) {
            m_got = true;
            emit chunk(data);
        }
    });
    connect(&m_proc, &QProcess::finished, this, [this] {
        if (!m_got && m_proc.property("wanted").toBool())
            emit failed(tr("The microphone could not be opened."));
    });
}

MicCapture::~MicCapture() { stop(); }

bool MicCapture::start()
{
    if (running())
        return true;
    if (QStandardPaths::findExecutable(QStringLiteral("parec")).isEmpty()) {
        emit failed(tr("The microphone needs parec (package pulseaudio-utils)."));
        return false;
    }
    m_rest.clear();
    m_got = false;
    m_proc.setProperty("wanted", true);
    m_proc.start(QStringLiteral("parec"), {QStringLiteral("--device=@DEFAULT_SOURCE@"), QStringLiteral("--raw"),
                                           QStringLiteral("--format=s16le"), QStringLiteral("--rate=16000"),
                                           QStringLiteral("--channels=1"), QStringLiteral("--latency-msec=60"),
                                           QStringLiteral("--client-name=OlyzanoAssistant")});
    return true;
}

void MicCapture::stop()
{
    m_proc.setProperty("wanted", false);
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(300);
    }
}

bool MicCapture::running() const { return m_proc.state() != QProcess::NotRunning; }

#endif
