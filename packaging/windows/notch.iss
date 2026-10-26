; Inno Setup script. The workflow passes /DAppVersion=x.y.z and /DSourceDir=<folder made by windeployqt>.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\deploy"
#endif

[Setup]
AppId={{B7C1F5A2-7E0F-4C6C-9D6A-2E4B6E8A0A11}
AppName=Olyzano Notch
AppVersion={#AppVersion}
AppPublisher=Olyzano
DefaultDirName={autopf}\Notch
DefaultGroupName=Olyzano Notch
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
Name: "autostart"; Description: "Start Olyzano Notch when I sign in to Windows"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Olyzano Notch"; Filename: "{app}\notch.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Notch"; \
  ValueData: """{app}\notch.exe"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\notch.exe"; Description: "Start Olyzano Notch"; Flags: nowait postinstall skipifsilent

[Code]
// RivaTuner (RTSS / MSI Afterburner) treats any GPU-drawing app as a game; say once how to tell it to ignore Notch.
function RtssInstalled: Boolean;
begin
  Result := RegKeyExists(HKLM32, 'SOFTWARE\Unwinder\RTSS') or RegKeyExists(HKLM, 'SOFTWARE\Unwinder\RTSS')
         or DirExists(ExpandConstant('{commonpf32}\RivaTuner Statistics Server'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (not WizardSilent) and RtssInstalled then
    MsgBox('RivaTuner Statistics Server is installed. If its overlay shows on Olyzano Notch:' + #13#10#13#10 +
           '1. Open RivaTuner and click the + button.' + #13#10 +
           '2. Pick ' + ExpandConstant('{app}\notch.exe') + #13#10 +
           '3. Set "Application detection level" to None for it.' + #13#10#13#10 +
           'Settings > About in Olyzano Notch has this too.', mbInformation, MB_OK);
end;
