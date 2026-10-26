#include "platform/WinPrivacy.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <iterator>

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

namespace WinPrivacy {

namespace {

// Each subkey of `parent` whose LastUsedTimeStop is 0 while LastUsedTimeStart is not.
void collectInUse(HKEY parent, QStringList &out, const QString &self)
{
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD len = DWORD(std::size(name));
        if (RegEnumKeyExW(parent, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        const QString key = QString::fromWCharArray(name, int(len));
        if (key == QLatin1String("NonPackaged"))
            continue; // walked by the caller
        quint64 start = 0, stop = 0;
        DWORD size = sizeof(quint64);
        if (RegGetValueW(parent, name, L"LastUsedTimeStart", RRF_RT_REG_QWORD, nullptr, &start, &size) != ERROR_SUCCESS)
            continue;
        size = sizeof(quint64);
        if (RegGetValueW(parent, name, L"LastUsedTimeStop", RRF_RT_REG_QWORD, nullptr, &stop, &size) != ERROR_SUCCESS)
            continue;
        if (start == 0 || stop != 0)
            continue;
        const QString app = appName(key);
        if (!app.isEmpty() && app.compare(self, Qt::CaseInsensitive) != 0 && !out.contains(app))
            out << app;
    }
}

IAudioEndpointVolume *defaultMicVolume()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // already done on Qt's GUI thread: then a no-op
    IMMDeviceEnumerator *enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(&enumerator))))
        return nullptr;
    IMMDevice *device = nullptr;
    IAudioEndpointVolume *volume = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))) {
        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&volume));
        device->Release();
    }
    enumerator->Release();
    return volume;
}

}

QStringList appsUsing(const wchar_t *capability)
{
    const QString self = QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName();
    const std::wstring path = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\")
                              + capability;
    QStringList apps;
    HKEY store = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &store) != ERROR_SUCCESS)
        return apps;
    collectInUse(store, apps, self);
    HKEY nonPackaged = nullptr;
    if (RegOpenKeyExW(store, L"NonPackaged", 0, KEY_READ, &nonPackaged) == ERROR_SUCCESS) {
        collectInUse(nonPackaged, apps, self);
        RegCloseKey(nonPackaged);
    }
    RegCloseKey(store);
    return apps;
}

int micMuted()
{
    IAudioEndpointVolume *volume = defaultMicVolume();
    if (!volume)
        return -1;
    BOOL muted = FALSE;
    const bool ok = SUCCEEDED(volume->GetMute(&muted));
    volume->Release();
    return ok ? (muted ? 1 : 0) : -1;
}

void toggleMicMute()
{
    IAudioEndpointVolume *volume = defaultMicVolume();
    if (!volume)
        return;
    BOOL muted = FALSE;
    if (SUCCEEDED(volume->GetMute(&muted)))
        volume->SetMute(!muted, nullptr);
    volume->Release();
}


bool rtssHooked()
{
    return GetModuleHandleW(L"RTSSHooks64.dll") != nullptr || GetModuleHandleW(L"RTSSHooks.dll") != nullptr;
}

}
