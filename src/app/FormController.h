#pragma once

#include "services/Chunker.h"
#include "services/FormServer.h"
#include "services/FormStore.h"

#include <QHash>
#include <functional>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class NotchController;
class ShareController;
class FormRelay;

// Forms (the Forms tab of Documents, `forms` in QML): build a form, share it on this network with a QR code, see the answers, take them out as a
// table or as files. A form's server lives only while Notch runs and the form is open; the answers stay on this computer.
class FormController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed) // [{id, title, status, answers, running, url, people, expiresAt}], newest first

public:
    FormController(NotchController *notch, ShareController *share, QObject *parent = nullptr);
    ~FormController() override;

    QVariantList list() const;

    Q_INVOKABLE QVariantMap newForm(const QString &template_); // send | contact | rsvp | blank: a form to edit (not saved yet)
    Q_INVOKABLE QVariantMap loadForm(const QString &id) const;
    Q_INVOKABLE QString validate(const QVariantMap &form) const; // "" when it can be shared, else why not
    Q_INVOKABLE bool saveForm(const QVariantMap &form);
    Q_INVOKABLE bool removeForm(const QString &id);
    Q_INVOKABLE QVariantMap newField(const QString &type) const;

    Q_INVOKABLE QStringList lanAddresses() const;
    Q_INVOKABLE QString publish(const QString &id, const QString &address, const QString &pin); // "" or why not
    // Through one of the user's servers instead: asynchronous, `publishFinished(id, error)` says how it went ("" = the link is ready).
    Q_INVOKABLE void publishRelay(const QString &id, const QString &serverId, const QString &pin);
    Q_INVOKABLE QVariantList relayServers() const; // [{id, name, ok}] the servers that can take forms
    Q_INVOKABLE void close(const QString &id);
    Q_INVOKABLE QString linkOf(const QString &id) const;

    Q_INVOKABLE QVariantMap responses(const QString &id) const;           // {columns, rows}
    // The answers of forms are kept in the search index: `index` is given a form's passages (a passage for each answer), `forget` a form that is gone.
    using IndexFn = std::function<void(const QString &id, const QString &title, const QList<Knowledge::Chunk> &chunks, const QString &fingerprint)>;
    void setIndex(IndexFn index, std::function<void(const QString &id)> forget);
    Q_INVOKABLE void reindexAll();
    Q_INVOKABLE QVariantMap summary(const QString &id) const; // the answers added up: {total, questions: [{label, answered, bars | stats}], days: [{day, count}]}
    Q_INVOKABLE bool removeResponse(const QString &id, int n);
    Q_INVOKABLE int exportCsv(const QString &id);                          // the table onto the shelf as a .csv: 1, or 0 when it failed
    Q_INVOKABLE QString reportHtml(const QString &id) const;               // the page itself (for the export, and for looking at)
    Q_INVOKABLE int exportReport(const QString &id);                       // a page with the answers and their pictures, onto the shelf: 1, or 0 when it failed
    Q_INVOKABLE int saveFiles(const QString &id, int n);                   // the files of an answer (0 = all of them) onto the shelf: how many
    Q_INVOKABLE QString debugSample();                                     // `notch --shot-page … docs-forms`: an RSVP with three answers, for a picture

signals:
    void changed();
    void publishFinished(const QString &id, const QString &error);

private:
    QVariantMap summary(const Forms::Form &f) const;
    void setStatus(const QString &id, const QString &status);

    void watch(const QString &id, FormRelay *relay);
    void announceEdit(const QString &id, const QString &name, const QString &device);
    void indexForm(const QString &id); // this form's answers into the search index

    int shelve(const QString &id, int n, bool always); // the files of an answer to the Files shelf (category of the form, tidy names); how many went
    void answered(const QString &id, int n);
    NotchController *m_notch;
    ShareController *m_share;
    IndexFn m_index;
    std::function<void(const QString &)> m_forget;
    FormStore m_store;
    QHash<QString, FormServer *> m_servers;
    QHash<QString, FormRelay *> m_relays;
};
