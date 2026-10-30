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
// RivaTuner (RTSS / MSI Afterburner) treats any GPU-drawing app as a game; give it a notch.exe profile with
// detection off (what its Shift + Add does). Profiles sits in Program Files, so a per-user install falls back to the guide.
function RtssDir: String;
begin
  if not (RegQueryStringValue(HKLM32, 'SOFTWARE\Unwinder\RTSS', 'InstallDir', Result)
          or RegQueryStringValue(HKLM, 'SOFTWARE\Unwinder\RTSS', 'InstallDir', Result)) then
    Result := ExpandConstant('{commonpf32}\RivaTuner Statistics Server');
  if not DirExists(Result + '\Profiles') then
    Result := '';
end;

function RtssIgnoresNotch(Dir: String): Boolean;
var
  Cfg: String;
begin
  Cfg := Dir + '\Profiles\notch.exe.cfg';
  // Keep a profile the user already made.
  Result := FileExists(Cfg) or SaveStringToFile(Cfg, '[Hooking]' + #13#10 + 'EnableHooking=0' + #13#10, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir: String;
begin
  if CurStep <> ssPostInstall then
    exit;
  Dir := RtssDir;
  if (Dir <> '') and not RtssIgnoresNotch(Dir) and not WizardSilent then
    MsgBox('RivaTuner Statistics Server is installed. If its overlay shows on Olyzano Notch:' + #13#10#13#10 +
           '1. Open RivaTuner and click the + button.' + #13#10 +
           '2. Pick ' + ExpandConstant('{app}\notch.exe') + #13#10 +
           '3. Set "Application detection level" to None for it.' + #13#10#13#10 +
           'Settings > About in Olyzano Notch has this too.', mbInformation, MB_OK);
end;
