#pragma once

#include "services/FormModel.h"

#include <QString>

// The forms on this computer and the answers they got, as files: <root>/<form id>/form.json, and per answer responses/<n>/answers.json with the
// files it came with in responses/<n>/files/. Written whole or not at all. Files that people send are only kept here: nothing opens them.
// Not thread safe: one owner (the controller, on the main thread).
class FormStore
{
public:
    explicit FormStore(const QString &root) : m_root(root) {}

    static bool validId(const QString &id);
    QList<Forms::Form> list() const; // newest first
    Forms::Form load(const QString &id, bool *ok = nullptr) const;
    bool save(const Forms::Form &form);
    bool remove(const QString &id); // the form and every answer

    // Keeps an answer: the uploads are moved out of where they were held. The number of the answer, or 0 when it could not be written.
    int addResponse(const QString &formId, const QJsonObject &answers, const QList<Forms::Upload> &uploads, const QString &device, const QString &editHash = {});
    // A person changes their answer: the new answers; the uploads take the place of the files of their question, the files of other questions stay.
    bool replaceResponse(const QString &formId, int n, const QJsonObject &answers, const QList<Forms::Upload> &uploads, const QString &device);
    int findByEdit(const QString &formId, const QString &hash) const; // the answer a personal link belongs to, or 0
    QJsonObject response(const QString &formId, int n) const;
    QStringList shelved(const QString &formId, int n) const;          // the files of an answer that have gone to the Files shelf already (by the name they are kept under)
    void addShelved(const QString &formId, int n, const QStringList &names);        // the kept document of one answer ({} when there is none)
    QList<Forms::Response> responses(const QString &formId) const; // oldest first
    int responseCount(const QString &formId) const;
    QStringList files(const QString &formId, int n) const; // full paths
    bool removeResponse(const QString &formId, int n);
    QString tempDir(const QString &formId) const; // where uploads wait until their answer is sent (made when asked)
    // A form shared through the user's server: where it is and the key (kept next to the form so that it goes on after a restart).
    QJsonObject relay(const QString &formId) const;
    bool saveRelay(const QString &formId, const QJsonObject &o);
    void removeRelay(const QString &formId);

private:
    QString dir(const QString &id) const { return m_root + QLatin1Char('/') + id; }
    QString m_root;
};
