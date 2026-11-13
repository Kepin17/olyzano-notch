#pragma once

#include "audio/AudioController.h"
#include "audio/ChatTools.h"
#include "platform/AssistantModel.h"

#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <memory>

class Assistant;
class KnowledgeController;
class NotchController;
class Tasks;

// The assistant you talk to (`assistantChat` in QML): a conversation with the local language model. What is said goes to ChatTools: a clear command is
// done as the voice assistant would (and one that changes something waits for Add / Cancel), a question about your files goes to Ask your files, one
// about your tasks or recordings is answered from their data by a step that has no tools, and the rest is talk. Nothing leaves this computer.
class AssistantChat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed) // [{role, text, error, sources:[{mark, name, kind, ref, page, startSec, where}], action:{kind, text, state, result}}]
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString stage READ stage NOTIFY changed)            // what is happening, or ""
    Q_PROPERTY(bool ready READ ready NOTIFY changed)               // the language model is installed
    Q_PROPERTY(int serial READ serial NOTIFY changed)              // grows with every change to the list: a view that scrolls goes to the end

public:
    AssistantChat(Assistant *assistant, Tasks *tasks, NotchController *notch, AudioController *audio, KnowledgeController *knowledge, QObject *parent = nullptr);
    ~AssistantChat() override;

    QVariantList messages() const { return m_messages; }
    bool busy() const { return m_busy; }
    QString stage() const { return m_stage; }
    bool ready() const;
    int serial() const { return m_serial; }

    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void approve(int index); // run the action on that card
    Q_INVOKABLE void decline(int index);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    // `notch --shot-page file assistant-demo`: a talk to look at
    void debugFill();

signals:
    void changed();

private:
    void add(const QVariantMap &message);
    void setStage(const QString &stage, bool busy);
    void reply(const QString &text, const QVariantList &sources = {}, bool error = false);
    void card(const QString &kind, const QString &text, const AssistantModel::Intent &intent);
    void route(const QString &text);                 // the worker: choose a tool and carry it out
    void answerFromData(const QString &question, const QString &data, const QList<ChatTools::RecordingRef> &used);
    void finishFiles();
    QList<ChatTools::Turn> history() const;
    void load();
    void save() const;
    static QString path();

    Assistant *m_assistant;
    Tasks *m_tasks;
    NotchController *m_notch;
    AudioController *m_audio;
    KnowledgeController *m_knowledge;
    QVariantList m_messages;
    QThreadPool m_pool;
    QTimer m_idle;
    std::atomic_bool m_cancel{false};
    std::unique_ptr<Audio::LlamaSession> m_session; // only touched by jobs of m_pool
    bool m_busy = false;
    bool m_waitingFiles = false;
    int m_filesSerial = 0, m_serial = 0;
    QString m_stage;
    QHash<int, AssistantModel::Intent> m_actions;
};
