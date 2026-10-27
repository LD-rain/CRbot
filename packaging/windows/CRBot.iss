; Compile with tools/package.ps1 (Inno Setup 6.3+).
#ifndef PayloadDir
  #error PayloadDir is required
#endif

[Setup]
AppId={{D3B735E8-1F87-4C97-9C62-87439286061A}
AppName=CRBot
AppVersion={#DisplayVersion}
AppVerName=CRBot {#DisplayVersion}
VersionInfoVersion={#AppVersion}
VersionInfoProductVersion={#AppVersion}
VersionInfoProductTextVersion={#DisplayVersion}
VersionInfoTextVersion={#DisplayVersion}
AppPublisher=CRBot contributors
AppPublisherURL=https://github.com/LD-rain/CRbot
DefaultDirName={localappdata}\Programs\CRBot
DefaultGroupName=CRBot
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayIcon={app}\CRbot.exe
UninstallDisplayName=CRBot {#DisplayVersion}
LicenseFile={#PayloadDir}\LICENSE
OutputDir={#InstallerDir}
OutputBaseFilename={#InstallerName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CRBot"; Filename: "{app}\CRbot.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\CRBot"; Filename: "{app}\CRbot.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\CRbot.exe"; Description: "Launch CRBot"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent unchecked

; Keep runtime logs/screenshots on uninstall. Only installed files are removed.
