#include "Hardware.h"

#include "platform/NotchPaths.h"
#include "platform/WhisperEngine.h"

#include <QMutexLocker>
#include <QSettings>

namespace Audio {

namespace {
// The system files give a card's ids, not its name; the graphics library (when this build has it) does. Matched by kind (own memory or shared).
void nameFromVulkan(Gpu::Info &info)
{
    QList<Whisper::GpuDevice> named = Whisper::gpus();
    for (Gpu::Device &d : info.devices) {
        if (!d.name.contains(QLatin1String(" GPU (")))
            continue; // already has its name
        for (int i = 0; i < named.size(); ++i)
            if (named[i].integrated == d.integrated) {
                d.name = named[i].name;
                named.removeAt(i);
                break;
            }
    }
}

QSettings settings() { return QSettings(NotchPaths::conf(QStringLiteral("audio")), QSettings::IniFormat); }
}

Hardware &Hardware::instance()
{
    static Hardware h;
    return h;
}

Gpu::Info Hardware::info()
{
    {
        QMutexLocker lock(&m_lock);
        if (m_detected)
            return m_info;
    }
    Gpu::Info found = Gpu::detect(); // outside the lock: it can take a moment
    nameFromVulkan(found);
    QMutexLocker lock(&m_lock);
    if (!m_detected) {
        m_info = found;
        m_detected = true;
    }
    return m_info;
}

bool Hardware::detected() const
{
    QMutexLocker lock(&m_lock);
    return m_detected;
}

Gpu::Info Hardware::known() const
{
    QMutexLocker lock(&m_lock);
    return m_info;
}

void Hardware::refresh()
{
    Gpu::Info found = Gpu::detect();
    nameFromVulkan(found);
    QSettings s = settings();
    s.remove(QStringLiteral("gpu/failed"));
    QMutexLocker lock(&m_lock);
    m_info = found;
    m_detected = true;
    m_failure.clear();
}

QString Hardware::setting() const
{
    QSettings s = settings();
    s.sync();
    const QString v = s.value(QStringLiteral("hardware"), QStringLiteral("auto")).toString();
    return v == QLatin1String("cpu") || v == QLatin1String("gpu") ? v : QStringLiteral("auto");
}

void Hardware::setSetting(const QString &v)
{
    QSettings s = settings();
    s.setValue(QStringLiteral("hardware"), v == QLatin1String("cpu") || v == QLatin1String("gpu") ? v : QStringLiteral("auto"));
    s.sync();
}

QString Hardware::deviceKey()
{
    const Gpu::Info i = info();
    const Gpu::Device *best = nullptr;
    for (const Gpu::Device &d : i.devices)
        if (!best || (best->integrated && !d.integrated) || (best->integrated == d.integrated && d.vramTotal > best->vramTotal))
            best = &d;
    return best ? best->key() : QString();
}

HwBench Hardware::bench()
{
    const QString key = deviceKey();
    QSettings s = settings();
    s.sync();
    const QString k = QStringLiteral("gpu/bench/") + key;
    return key.isEmpty() || !s.contains(k) ? HwBench{} : HwBench{true, s.value(k).toDouble()};
}

void Hardware::setBench(double speedup)
{
    const QString key = deviceKey();
    if (key.isEmpty())
        return;
    QSettings s = settings();
    s.setValue(QStringLiteral("gpu/bench/") + key, speedup);
    s.sync();
}

HwPlan Hardware::plan(bool llmVulkanInstalled)
{
    Gpu::Info i = info();
    if (Whisper::gpusBuilt() && Whisper::gpus().isEmpty())
        i.vulkan = false; // the graphics library found no card to use (no working driver), whatever the system's files say
    const QString key = deviceKey();
    QSet<QString> failed;
    QSettings s = settings();
    s.sync();
    for (const QString &engine : {QStringLiteral("whisper"), QStringLiteral("llm")})
        if (!key.isEmpty() && s.value(QStringLiteral("gpu/failed/") + engine).toString() == key)
            failed.insert(engine);
    return planHardware(i, hwSettingOf(setting()), bench(), HwNeeds{}, failed, llmVulkanInstalled);
}

void Hardware::markFailed(const QString &engine, const QString &why)
{
    const QString key = deviceKey();
    if (!key.isEmpty()) {
        QSettings s = settings();
        s.setValue(QStringLiteral("gpu/failed/") + engine, key);
        s.sync();
    }
    QMutexLocker lock(&m_lock);
    m_failure = why;
}

QString Hardware::lastFailure() const
{
    QMutexLocker lock(&m_lock);
    return m_failure;
}

void Hardware::report(const QString &engine, const QString &where)
{
    QMutexLocker lock(&m_lock);
    m_report.insert(engine, where);
}

QString Hardware::reported(const QString &engine) const
{
    QMutexLocker lock(&m_lock);
    return m_report.value(engine);
}

} // namespace Audio
