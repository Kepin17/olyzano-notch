#pragma once

#include "platform/AssistantModel.h"
#include "platform/MicCapture.h"
#include "platform/UtteranceModel.h"
#include "platform/Whisper.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVector>
#include <memory>
#include <mutex>

class Activities;
class Launcher;
class MediaPlayers;
class NotchController;
class QFile;
class QNetworkReply;
class QuickSettings;
class Tasks;

// The offline voice assistant, as `assistant` in QML: press the mic, say a command, and it is done. The microphone is
// open only while listening; whisper.cpp turns the words into text on this computer (the model is a file downloaded once
// from Settings); AssistantModel turns the text into one of a few safe actions. Audio is never saved or sent anywhere.
class Assistant : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)                      // this build has whisper.cpp
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY settingsChanged)         // "base" | "tiny"
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged) // "auto" | "id" | "en"
    Q_PROPERTY(QString searchEngine READ searchEngine WRITE setSearchEngine NOTIFY settingsChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)                // idle | listening | thinking | done | error
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)                  // 0..1: how loud the voice is, for the wave
    Q_PROPERTY(QString heard READ heard NOTIFY resultChanged)
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString modelState READ modelState NOTIFY modelChanged)      // missing | downloading | ready
    Q_PROPERTY(qreal modelProgress READ modelProgress NOTIFY modelChanged)  // 0..1 while downloading
    Q_PROPERTY(QString modelSizeText READ modelSizeText NOTIFY settingsChanged)
    Q_PROPERTY(QString modelError READ modelError NOTIFY modelChanged)

public:
    Assistant(Launcher *launcher, Tasks *tasks, QuickSettings *quick, MediaPlayers *media, Activities *activities,
              NotchController *notch, QObject *parent = nullptr);
    ~Assistant() override;

    bool available() const { return Whisper::compiled(); }
    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), false).toBool(); }
    void setEnabled(bool on) { store(QStringLiteral("enabled"), on); }
    QString model() const { return m_settings.value(QStringLiteral("model")).toString() == QLatin1String("tiny") ? QStringLiteral("tiny") : QStringLiteral("base"); }
    void setModel(const QString &m);
    QString language() const;
    void setLanguage(const QString &l) { store(QStringLiteral("language"), l == QLatin1String("id") || l == QLatin1String("en") ? l : QStringLiteral("auto")); }
    QString searchEngine() const;
    void setSearchEngine(const QString &e) { store(QStringLiteral("searchEngine"), e); }
    QString state() const { return m_state; }
    qreal level() const { return m_level; }
    QString heard() const { return m_heard; }
    QString result() const { return m_result; }
    QString modelState() const;
    qreal modelProgress() const { return m_progress; }
    QString modelSizeText() const;
    QString modelError() const { return m_modelError; }

    Q_INVOKABLE void start();            // listen for one command
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void say(const QString &text); // the same without the microphone (the words typed or given on the command line)
    Q_INVOKABLE void downloadModel();    // the one thing that uses the network: asked for, from Settings
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void removeModel();
    // For `notch --assistant-wav` and tests: speech in a 16 kHz mono 16-bit WAV file, to text.
    QString transcribeFile(const QString &wavPath);
    QString describe(const AssistantModel::Intent &in) const; // what the command would do, in words (no action)
    static QString modelPath(const QString &model);

signals:
    void settingsChanged();
    void stateChanged();
    void levelChanged();
    void resultChanged();
    void modelChanged();

private:
    void store(const QString &key, const QVariant &v);
    void setState(const QString &s);
    void finish(const QString &state, const QString &heard, const QString &result);
    void onChunk(const QByteArray &s16le);
    void listenEnded(bool speech);
    void act(const QString &text);
    QString run(const AssistantModel::Intent &in);
    void checkModel();
    void downloadChunk();
    void downloadDone();

    Launcher *m_launcher;
    Tasks *m_tasks;
    QuickSettings *m_quick;
    MediaPlayers *m_media;
    Activities *m_activities;
    NotchController *m_notch;
    QSettings m_settings;

    MicCapture m_mic;
    UtteranceModel::Tracker m_tracker;
    QElapsedTimer m_clock;
    QVector<float> m_pcm;
    QString m_state = QStringLiteral("idle");
    qreal m_level = 0;
    QString m_heard, m_result;
    QTimer m_idleTimer;

    std::mutex m_whisperMutex; // one recognition at a time; the model stays loaded between commands
    Whisper m_whisper;
    QString m_loadedModel;
    QFutureWatcher<QString> m_watcher;

    QNetworkAccessManager m_net;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_part;
    std::unique_ptr<QCryptographicHash> m_hash;
    qreal m_progress = 0;
    QString m_modelError;
    bool m_modelReady = false;
};
