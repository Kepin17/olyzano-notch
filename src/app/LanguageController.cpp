#include "LanguageController.h"

#include "services/LanguageSettings.h"
#include "services/OcrPacks.h"

#include <QDir>
#include <QLocale>

LanguageController::LanguageController(AudioController *audio, QObject *parent) : QObject(parent), m_audio(audio)
{
    m_enabled = LanguageSettings::enabled();
    m_system = OcrPacks::systemLanguages();
    OcrPacks::configure(m_audio->modelStore()->root() + QStringLiteral("/models"), NotchPaths::dataDir(QStringLiteral("ocr")));
    apply();
    connect(m_audio, &AudioController::modelsChanged, this, &LanguageController::changed);
}

// what OCR reads with, in order: the languages in use that are written in something other than... all of them, the first ones first
void LanguageController::apply()
{
    QStringList tess;
    for (const QString &c : std::as_const(m_enabled))
        if (const QString t = Lang::tessCode(c); !t.isEmpty() && !tess.contains(t))
            tess << t;
    OcrPacks::setWanted(tess);
}

void LanguageController::setSearch(const QString &s)
{
    if (m_search == s)
        return;
    m_search = s;
    emit changed();
}

void LanguageController::setEnabled(const QString &code, bool on)
{
    if (!Lang::known(code))
        return;
    if (on && !m_enabled.contains(code))
        m_enabled << code;
    else if (!on && m_enabled.contains(code) && m_enabled.size() > 1) // one stays: there is always a language
        m_enabled.removeAll(code);
    else
        return;
    LanguageSettings::setEnabled(m_enabled);
    apply();
    emit changed();
}

QString LanguageController::label(const QString &code) const
{
    if (code == QLatin1String("auto"))
        return tr("Automatic");
    const Lang::Language *l = Lang::find(code);
    return l ? l->english : code;
}

bool LanguageController::rtl(const QString &code) const { return Lang::isRtl(code); }

QVariantList LanguageController::picker() const
{
    QVariantList out;
    for (const QString &c : m_enabled)
        if (const Lang::Language *l = Lang::find(c))
            out.append(QVariantMap{{QStringLiteral("code"), c}, {QStringLiteral("label"), l->english}, {QStringLiteral("native"), l->native}});
    return out;
}

QVariantList LanguageController::all() const
{
    QVariantList out;
    const QString needle = m_search.trimmed().toLower();
    const ModelStore *store = m_audio->modelStore();
    for (const Lang::Language &l : Lang::all()) {
        if (!needle.isEmpty() && !l.english.toLower().contains(needle) && !l.native.toLower().contains(needle) && !l.code.contains(needle))
            continue;
        const QString id = QStringLiteral("ocr-") + l.tess;
        const QString st = store->state(id);
        const bool system = m_system.contains(l.tess);
        const Audio::CatalogItem item = store->item(id);
        out.append(QVariantMap{{QStringLiteral("code"), l.code}, {QStringLiteral("english"), l.english}, {QStringLiteral("native"), l.native}, {QStringLiteral("enabled"), m_enabled.contains(l.code)},
                               {QStringLiteral("rtl"), l.rtl}, {QStringLiteral("hasWords"), Lang::hasWords(l.code)}, {QStringLiteral("ocrId"), id},
                               {QStringLiteral("ocr"), st == QLatin1String("ready") ? QStringLiteral("ready") : st == QLatin1String("downloading") ? QStringLiteral("downloading") : system ? QStringLiteral("system")
                                                       : st == QLatin1String("partial") ? QStringLiteral("partial") : QStringLiteral("missing")},
                               {QStringLiteral("sizeText"), QLocale().formattedDataSize(item.size)}, {QStringLiteral("progress"), store->progress(id)},
                               {QStringLiteral("speedText"), st == QLatin1String("downloading") && store->speed(id) > 0 ? QLocale().formattedDataSize(qint64(store->speed(id))) + QStringLiteral("/s") : QString()},
                               {QStringLiteral("error"), store->error(id)}});
    }
    return out;
}

void LanguageController::downloadPack(const QString &code) { m_audio->downloadModel(QStringLiteral("ocr-") + Lang::tessCode(code)); }
void LanguageController::cancelPack(const QString &code) { m_audio->cancelDownload(QStringLiteral("ocr-") + Lang::tessCode(code)); }
void LanguageController::removePack(const QString &code) { m_audio->removeModel(QStringLiteral("ocr-") + Lang::tessCode(code)); }
