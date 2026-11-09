#pragma once

#include "../services/GpuInfo.h"

#include <QObject>
#include <QSet>
#include <QString>

// Who runs what: the speech model and the language model on a graphics processor, or on the processor. Pure, so that the choices can
// be tested without a computer that has the card. The point of "Auto" is that it does not guess: a card of its own with room goes, an
// integrated one (it shares the system's memory, so it may be no quicker at all) only when a measurement said it is quicker.
namespace Audio {

enum class HwSetting { Auto, Cpu, Gpu };

inline HwSetting hwSettingOf(const QString &s) { return s == QLatin1String("cpu") ? HwSetting::Cpu : s == QLatin1String("gpu") ? HwSetting::Gpu : HwSetting::Auto; }

struct HwNeeds {
    qint64 whisper = Q_INT64_C(600) << 20; // memory the speech model wants on the card
    qint64 llm = Q_INT64_C(4) << 30;       // the language model with its context
    int llmLayers = 36;                    // layers of the language model that can move to the card
};

struct HwBench {
    bool known = false;
    double speedup = 0; // how many times quicker the speech model was on the card than on the processor
};

struct HwPlan {
    bool whisperGpu = false;
    int llmLayers = 0;      // layers sent to the card (0: all on the processor)
    bool llmVulkan = false; // the Vulkan build of the language model program (else the plain one)
    QString device;         // the card used ("" for none)
    QString reason;         // why, in words
};

inline HwPlan planHardware(const Gpu::Info &info, HwSetting setting, const HwBench &bench, const HwNeeds &needs, const QSet<QString> &failed, bool llmVulkanInstalled)
{
    HwPlan p;
    if (setting == HwSetting::Cpu) {
        p.reason = QObject::tr("Set to use the processor only");
        return p;
    }
    if (!info.vulkan) {
        p.reason = QObject::tr("No Vulkan driver was found");
        return p;
    }
    const Gpu::Device *best = nullptr; // a card of its own first, then the one with the most room
    for (const Gpu::Device &d : info.devices)
        if (!best || (best->integrated && !d.integrated) || (best->integrated == d.integrated && d.vramFree > best->vramFree))
            best = &d;
    if (!best) {
        p.reason = QObject::tr("No graphics processor was found");
        return p;
    }
    if (setting == HwSetting::Auto && best->integrated && !(bench.known && bench.speedup >= 1.3)) {
        p.reason = bench.known ? QObject::tr("%1 shares the computer's memory and was not quicker in the speed test").arg(best->name)
                               : QObject::tr("%1 shares the computer's memory: run the speed test to see if it helps").arg(best->name);
        return p;
    }
    const qint64 margin = Q_INT64_C(400) << 20;
    p.device = best->name;
    p.whisperGpu = !failed.contains(QStringLiteral("whisper")) && best->vramFree >= needs.whisper + margin;
    if (llmVulkanInstalled && !failed.contains(QStringLiteral("llm"))) {
        const qint64 room = best->vramFree - (p.whisperGpu ? needs.whisper : 0) - margin;
        if (room >= needs.llm) {
            p.llmLayers = 99;
        } else if (room * 5 >= needs.llm * 2) { // part of the layers only when that is a good part (at least two fifths): a few layers are not worth the trip
            p.llmLayers = int(double(needs.llmLayers) * double(room) / double(needs.llm));
        }
        p.llmVulkan = p.llmLayers > 0;
    }
    if (!p.whisperGpu && !p.llmVulkan) {
        p.device.clear();
        p.reason = failed.contains(QStringLiteral("whisper")) || failed.contains(QStringLiteral("llm")) ? QObject::tr("The graphics processor did not work earlier")
                                                                                                          : QObject::tr("%1 has too little free memory").arg(best->name);
        return p;
    }
    p.reason = QObject::tr("%1: speech %2, notes %3").arg(best->name, p.whisperGpu ? QObject::tr("on it") : QObject::tr("on the processor"),
                                                          p.llmLayers >= needs.llmLayers ? QObject::tr("on it") : p.llmLayers > 0 ? QObject::tr("partly on it") : QObject::tr("on the processor"));
    return p;
}

} // namespace Audio
