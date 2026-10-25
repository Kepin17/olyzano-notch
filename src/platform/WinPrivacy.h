#pragma once

#include <QString>
#include <QStringList>

// Windows: which apps use the microphone or a camera right now, and the default microphone's mute switch.
// The apps come from the registry list Windows keeps for its own taskbar mic icon
// (HKCU\...\CapabilityAccessManager\ConsentStore\<capability>): an app is using it while its
// LastUsedTimeStart is set and LastUsedTimeStop is still 0.
namespace WinPrivacy {

// A ConsentStore key name as an app name:
//   "C:#Program Files#Zoom#bin#Zoom.exe" (NonPackaged) -> "Zoom"
//   "Microsoft.WindowsCamera_8wekyb3d8bbwe" (packaged) -> "WindowsCamera"
inline QString appName(const QString &key)
{
    if (key.contains(u'#')) {
        QString file = key.section(u'#', -1);
        if (file.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
            file.chop(4);
        return file;
    }
    return key.section(u'_', 0, 0).section(u'.', -1);
}

#ifdef Q_OS_WIN
QStringList appsUsing(const wchar_t *capability); // L"microphone" or L"webcam"; Notch itself left out
int micMuted();                                   // 1 muted, 0 live, -1 no microphone
void toggleMicMute();
#endif

}
