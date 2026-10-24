; Inno Setup script. The workflow passes /DAppVersion=x.y.z and /DSourceDir=<folder made by windeployqt>.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\deploy"
#endif

[Setup]
AppId={{B7C1F5A2-7E0F-4C6C-9D6A-2E4B6E8A0A11}
AppName=Notch
AppVersion={#AppVersion}
AppPublisher=Notch
DefaultDirName={autopf}\Notch
DefaultGroupName=Notch
OutputBaseFilename=Notch-{#AppVersion}-windows-x64-setup
OutputDir=..\..\installer
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per user by default (no administrator prompt); the dialog offers "for all users".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
SetupIconFile=notch.ico
UninstallDisplayIcon={app}\notch.exe
WizardStyle=modern
CloseApplications=yes

[Tasks]
Name: "autostart"; Description: "Start Notch when I sign in to Windows"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Notch"; Filename: "{app}\notch.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Notch"; \
  ValueData: """{app}\notch.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\notch.exe"; Description: "Start Notch"; Flags: nowait postinstall skipifsilent
