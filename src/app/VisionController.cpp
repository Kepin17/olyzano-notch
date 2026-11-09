#include "VisionController.h"

#include "NotchController.h"
#include "audio/AudioEngines.h"
#include "audio/VisionPrompt.h"
#include "services/Languages.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QtConcurrent>

VisionController::VisionController(NotchController *notch, AudioController *audio, QObject *parent) : QObject(parent), m_notch(notch), m_audio(audio)
{
    m_pool.setMaxThreadCount(1);
    connect(audio, &AudioController::modelsChanged, this, &VisionController::changed);
}

VisionController::~VisionController()
{
    m_cancel = true;
    m_pool.waitForDone(5000);
}

bool VisionController::ready() const { return Audio::LlamaSession::visionAvailable(m_audio->modelStore()); }

void VisionController::cancel() { m_cancel = true; }

void VisionController::clearError()
{
    m_error.clear();
    emit changed();
}

void VisionController::run(const QString &kind, const QString &itemId)
{
    if (m_busy)
        return;
    const QVariantMap info = m_notch->documentInfo(itemId);
    if (!info.value(QStringLiteral("ok")).toBool())
        return;
    if (!ready()) {
        m_error = tr("Download the picture model first: Settings → AI → Reading images.");
        emit changed();
        return;
    }
    const bool read = kind.startsWith(QLatin1String("read")), toNote = kind.endsWith(QLatin1String("note"));
    const QString path = info.value(QStringLiteral("path")).toString(), base = info.value(QStringLiteral("base")).toString(), name = info.value(QStringLiteral("name")).toString();
    const QString code = m_audio->language();
    const QString language = Lang::known(code) ? Lang::englishName(code) : QStringLiteral("the language of the writing in the picture (English when there is none)");
    ModelStore *store = m_audio->modelStore();
    const int threads = m_audio->threads();
    m_busy = true;
    m_cancel = false;
    m_error.clear();
    m_status = tr("Starting the picture model…");
    emit changed();
    (void)QtConcurrent::run(&m_pool, [this, store, threads, path, base, name, read, toNote, language] {
        QString error, text;
        const auto status = [this](const QString &s) { QMetaObject::invokeMethod(this, [this, s] { m_status = s; emit changed(); }, Qt::QueuedConnection); };
        const QByteArray png = Audio::Vision::scaledPng(path, 1568, &error);
        if (error.isEmpty()) {
            Audio::LlamaSession session(store, [threads] { return threads; }, true);
            error = session.start(&m_cancel);
            if (error.isEmpty()) {
                status(tr("Reading the picture…"));
                const Audio::Vision::Mode mode = read ? Audio::Vision::Mode::ReadText : Audio::Vision::Mode::Describe;
                const Audio::Vision::ImageChatFn chat = [&](const QString &s, const QString &u, const QByteArray &img, const QString &mime, const QJsonObject &schema) {
                    return session.chatImage(s, u, img, mime, schema, &m_cancel);
                };
                const Audio::Vision::Result r = Audio::Vision::look(chat, png, QStringLiteral("image/png"), language, mode, &error);
                if (error.isEmpty())
                    text = Audio::Vision::toText(r, mode);
            }
            session.stop();
        }
        if (m_cancel)
            error = QStringLiteral("cancelled");
        QMetaObject::invokeMethod(this, [this, error, text, base, name, read, toNote] {
            m_busy = false;
            m_status.clear();
            if (error == QLatin1String("cancelled")) {
                // stopped by the person: nothing to say
            } else if (!error.isEmpty()) {
                m_error = error;
            } else if (toNote) {
                const QString file = base + (read ? tr(" (text from picture).txt") : tr(" (description).txt"));
                if (m_notch->addDocument(file, QStringLiteral("text/plain"), text.toUtf8(), tr("Read from %1 by the picture model").arg(name)))
                    m_notch->announce(read ? tr("Text saved as a note") : tr("Description saved as a note"), file, QStringLiteral("text-plain"));
            } else {
                m_notch->copyText(text);
                m_notch->announce(read ? tr("Text copied") : tr("Description copied"), tr("%n characters", nullptr, int(text.size())), QStringLiteral("edit-copy"));
            }
            emit changed();
        }, Qt::QueuedConnection);
    });
}
