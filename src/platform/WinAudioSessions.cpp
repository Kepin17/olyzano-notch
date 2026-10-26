#include "platform/WinAudioSessions.h"

#include <QString>

#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

namespace {
template<class T>
void releaseCom(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Discord.exe, DiscordPTB.exe, DiscordCanary.exe
bool discordExe(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != 0;
    CloseHandle(process);
    if (!ok)
        return false;
    const QString exe = QString::fromWCharArray(path, int(size)).section(u'\\', -1);
    return exe.startsWith(QLatin1String("discord"), Qt::CaseInsensitive) && exe.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
}
}

WinAudioSessions::~WinAudioSessions()
{
    close(m_capture);
    close(m_render);
}

bool WinAudioSessions::open(Flow &flow, bool capture)
{
    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *device = nullptr;
    const bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                               reinterpret_cast<void **>(&enumerator)))
                    && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eConsole, &device))
                    && SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                                  reinterpret_cast<void **>(&flow.manager)));
    releaseCom(device);
    releaseCom(enumerator);
    return ok;
}

void WinAudioSessions::close(Flow &flow)
{
    releaseCom(flow.manager);
}

bool WinAudioSessions::isDiscord(quint32 pid)
{
    auto it = m_isDiscord.find(pid);
    if (it == m_isDiscord.end()) {
        if (m_isDiscord.size() > 256) // process ids come and go: forget the old ones
            m_isDiscord.clear();
        it = m_isDiscord.insert(pid, discordExe(pid));
    }
    return it.value();
}

template<class F>
bool WinAudioSessions::eachDiscord(Flow &flow, bool capture, F each)
{
    // The default device can change; look it up again now and then.
    if (flow.manager && ++flow.idle >= 60) {
        flow.idle = 0;
        close(flow);
    }
    if (!flow.manager && !open(flow, capture))
        return false;

    IAudioSessionEnumerator *list = nullptr;
    int count = 0;
    if (FAILED(flow.manager->GetSessionEnumerator(&list)) || FAILED(list->GetCount(&count))) {
        releaseCom(list);
        close(flow);
        return false;
    }
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl *control = nullptr;
        if (FAILED(list->GetSession(i, &control)))
            continue;
        IAudioSessionControl2 *control2 = nullptr;
        DWORD pid = 0;
        if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void **>(&control2)))
            && SUCCEEDED(control2->GetProcessId(&pid)) && pid != 0 && isDiscord(pid)) {
            AudioSessionState state = AudioSessionStateInactive;
            control->GetState(&state);
            IAudioMeterInformation *meter = nullptr;
            float peak = 0;
            if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioMeterInformation), reinterpret_cast<void **>(&meter))))
                meter->GetPeakValue(&peak);
            releaseCom(meter);
            each(int(state), double(peak));
        }
        releaseCom(control2);
        releaseCom(control);
    }
    releaseCom(list);
    return true;
}

DiscordAudio WinAudioSessions::read()
{
    DiscordAudio audio;
    eachDiscord(m_capture, true, [&](int state, double peak) {
        if (state == AudioSessionStateActive) {
            audio.captureActive = true;
            audio.capturePeak = qMax(audio.capturePeak, peak);
        }
    });
    eachDiscord(m_render, false, [&](int state, double peak) {
        if (state == AudioSessionStateActive) {
            audio.renderActive = true;
            audio.renderPeak = qMax(audio.renderPeak, peak);
        }
    });
    return audio;
}

QByteArray WinAudioSessions::dump()
{
    QByteArray out;
    const char *names[] = {"inactive", "ACTIVE", "expired"};
    auto line = [&](const char *flow, int state, double peak) {
        out += QByteArray(flow) + ' ' + names[qBound(0, state, 2)] + " peak=" + QByteArray::number(peak, 'f', 3) + ' ';
    };
    int captures = 0, renders = 0;
    eachDiscord(m_capture, true, [&](int s, double p) { ++captures; line("capture", s, p); });
    eachDiscord(m_render, false, [&](int s, double p) { ++renders; line("render", s, p); });
    if (captures + renders == 0)
        out += "(no discord sessions)";
    return out;
}
