#pragma once

#include "HardwarePlan.h"

#include <QMutex>
#include <QSet>

// What this computer has and what the models were given, in one place that the speech and the language model both ask: the cards found
// (once, then kept until "Detect again"), the person's setting (auto | cpu | gpu), a measurement of the speech model on the card of an
// integrated processor, and what failed before (so that a card that does not work is not tried at every start). Thread-safe.
namespace Audio {

class Hardware
{
public:
    static Hardware &instance();

    Gpu::Info info();                  // detects on the first call (a moment: not on the interface thread)
    bool detected() const;             // info() has been made
    Gpu::Info known() const;           // what was found, without looking (empty until detected)
    void refresh();                    // look again, and forget what failed
    QString setting() const;           // auto | cpu | gpu
    void setSetting(const QString &s);
    // `llmVulkanInstalled`: the Vulkan build of the language model program is downloaded
    HwPlan plan(bool llmVulkanInstalled, qint64 llmNeedBytes = 0); // llmNeedBytes: a larger language model than the usual one (0: the usual)
    QString notesModel() const;        // standard | large | xl: which language model writes notes and answers (the larger ones only when they can run on a card)
    void setNotesModel(const QString &m);
    HwBench bench();
    void setBench(double speedup);     // the speed test's result for the best card
    void markFailed(const QString &engine, const QString &why); // engine: whisper | llm
    QString lastFailure() const;
    // What actually runs where, for the person to read ("Speech: GPU: AMD Radeon 780M", "Notes: CPU")
    void report(const QString &engine, const QString &where);
    QString reported(const QString &engine) const;

private:
    Hardware() = default;
    QString deviceKey(); // the best card, as a key for what is stored about it
    mutable QMutex m_lock;
    bool m_detected = false;
    Gpu::Info m_info;
    QString m_failure;
    QHash<QString, QString> m_report;
};

} // namespace Audio
