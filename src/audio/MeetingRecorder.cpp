#include "MeetingRecorder.h"

#include "RecorderMath.h"
#include "WavFile.h"

#include <QDir>
#include <QStorageInfo>

MeetingRecorder::MeetingRecorder(QObject *parent) : QObject(parent), m_mic(this, MicCapture::Microphone), m_sys(this, MicCapture::SystemSound)
{
    connect(&m_mic, &MicCapture::chunk, this, [this](const QByteArray &c) {
        if (!m_recording)
            return;
        write(m_micFile, m_micSamples, c);
        m_level = Audio::peakOf(c.constData(), int(c.size()));
        emit levelChanged();
        emit micAudio(c, m_clock.elapsed());
    });
    connect(&m_sys, &MicCapture::chunk, this, [this](const QByteArray &c) {
        if (m_recording)
        {
            write(m_sysFile, m_sysSamples, c);
            emit sysAudio(c, m_clock.elapsed());
        }
    });
    connect(&m_mic, &MicCapture::failed, this, [this](const QString &why) { emit failed(why); });
    // The computer's own sound may be unavailable (no monitor source): the recording goes on with the microphone alone.
    m_limit.setSingleShot(true);
    connect(&m_limit, &QTimer::timeout, this, [this] { emit limitReached(); });
}

MeetingRecorder::~MeetingRecorder()
{
    if (m_recording)
        discard();
}

void MeetingRecorder::write(QFile &file, qint64 &samples, const QByteArray &chunk)
{
    const qint64 n = chunk.size() / 2;
    if (const qint64 gap = Audio::silenceBefore(samples, n, m_clock.elapsed(), Rate); gap > 0) {
        file.write(QByteArray(int(gap * 2), '\0'));
        samples += gap;
    }
    file.write(chunk.constData(), n * 2);
    samples += n;
}

QString MeetingRecorder::start(const QString &dir)
{
    if (m_recording)
        return {};
    if (!QDir().mkpath(dir))
        return tr("Cannot create the folder of the recording");
    const QStorageInfo disk(dir);
    const qint64 need = Q_INT64_C(600) << 20; // an hour of two channels, twice (raw, then the file)
    if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < need)
        return tr("Not enough disk space to record: about %1 are needed").arg(QLocale().formattedDataSize(need));
    m_dir = dir;
    m_micFile.setFileName(dir + QStringLiteral("/rec-mic.raw"));
    m_sysFile.setFileName(dir + QStringLiteral("/rec-sys.raw"));
    if (!m_micFile.open(QIODevice::WriteOnly | QIODevice::Truncate) || !m_sysFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return tr("Cannot write the recording");
    m_micSamples = m_sysSamples = 0;
    m_level = 0;
    m_clock.start();
    m_recording = true;
    if (!m_mic.start()) {
        m_recording = false;
        m_micFile.close();
        m_sysFile.close();
        return tr("The microphone could not be opened");
    }
    m_sys.start(); // may fail quietly: see above
    m_limit.start(MaxSeconds * 1000);
    return {};
}

QString MeetingRecorder::stop(QString *wavPath)
{
    if (!m_recording)
        return tr("Nothing is being recorded");
    const qint64 total = m_clock.elapsed() * Rate / 1000;
    m_mic.stop();
    m_sys.stop();
    m_limit.stop();
    m_recording = false;
    // both files are brought to the same length: silence at the end of the quieter one
    const auto pad = [total](QFile &f, qint64 &n) {
        if (n < total) {
            f.write(QByteArray(int((total - n) * 2), '\0'));
            n = total;
        }
    };
    pad(m_micFile, m_micSamples);
    pad(m_sysFile, m_sysSamples);
    m_micFile.close();
    m_sysFile.close();
    const QString out = m_dir + QStringLiteral("/source.wav");
    const QString err = Wav::interleaveRaw(m_micFile.fileName(), m_sysFile.fileName(), out, Rate);
    QFile::remove(m_micFile.fileName());
    QFile::remove(m_sysFile.fileName());
    if (!err.isEmpty())
        return err;
    if (wavPath)
        *wavPath = out;
    return {};
}

void MeetingRecorder::discard()
{
    m_mic.stop();
    m_sys.stop();
    m_limit.stop();
    m_recording = false;
    m_micFile.close();
    m_sysFile.close();
    QFile::remove(m_micFile.fileName());
    QFile::remove(m_sysFile.fileName());
}
