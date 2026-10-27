#include "WinVolume.h"

#include <QtGlobal>

#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>

namespace {

// The endpoint of the default speakers; the caller releases it. The device may change between calls, so nothing is kept.
IAudioEndpointVolume *defaultSpeakers()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // already done on Qt's GUI thread: then a no-op
    IMMDeviceEnumerator *enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&enumerator))))
        return nullptr;
    IMMDevice *device = nullptr;
    IAudioEndpointVolume *volume = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&volume));
        device->Release();
    }
    enumerator->Release();
    return volume;
}

}

namespace WinVolume {

int speakers()
{
    IAudioEndpointVolume *v = defaultSpeakers();
    if (!v)
        return -1;
    float level = 0;
    const bool ok = SUCCEEDED(v->GetMasterVolumeLevelScalar(&level));
    v->Release();
    return ok ? qBound(0, qRound(level * 100), 100) : -1;
}

bool muted()
{
    IAudioEndpointVolume *v = defaultSpeakers();
    if (!v)
        return false;
    BOOL mute = FALSE;
    v->GetMute(&mute);
    v->Release();
    return mute;
}

bool setSpeakers(int percent)
{
    IAudioEndpointVolume *v = defaultSpeakers();
    if (!v)
        return false;
    const bool ok = SUCCEEDED(v->SetMasterVolumeLevelScalar(qBound(0, percent, 100) / 100.0f, nullptr));
    if (ok && percent > 0)
        v->SetMute(FALSE, nullptr); // raising the volume of a muted output is meant to be heard
    v->Release();
    return ok;
}

void setMuted(bool mute)
{
    if (IAudioEndpointVolume *v = defaultSpeakers()) {
        v->SetMute(mute, nullptr);
        v->Release();
    }
}

}
