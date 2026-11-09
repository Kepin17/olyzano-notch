#pragma once

#include "audio/AudioController.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

// Settings → Languages: which languages are in use (they show in the language pickers of Audio and Documents, and "automatic" chooses among
// them) and, for each, the OCR language pack it reads pictures with: from the system, downloaded here, or missing (Download). The downloads
// are the ones of ModelStore, so progress, speed, stop and remove are the same as for the other models.
class LanguageController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList all READ all NOTIFY changed)          // every language: [{code, english, native, enabled, ocr: system|ready|downloading|partial|missing, ocrId, size, progress, ...}]
    Q_PROPERTY(QVariantList picker READ picker NOTIFY changed)    // the ones in use, for a picker: [{code, label}]
    Q_PROPERTY(QStringList enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)

public:
    explicit LanguageController(AudioController *audio, QObject *parent = nullptr);

    QVariantList all() const;
    QVariantList picker() const;
    QStringList enabled() const { return m_enabled; }
    QString search() const { return m_search; }
    void setSearch(const QString &s);

    Q_INVOKABLE void setEnabled(const QString &code, bool on);
    Q_INVOKABLE void downloadPack(const QString &code);
    Q_INVOKABLE void cancelPack(const QString &code);
    Q_INVOKABLE void removePack(const QString &code);
    Q_INVOKABLE QString label(const QString &code) const; // "Indonesian", or "Automatic" for "auto"
    Q_INVOKABLE bool rtl(const QString &code) const;

signals:
    void changed();

private:
    void apply();
    AudioController *m_audio;
    QStringList m_enabled, m_system;
    QString m_search;
};
