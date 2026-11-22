[Setup]
AppId=org.appsuite.systemmonitor
AppName=System Monitor
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\SystemMonitor
DefaultGroupName=System Monitor
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\systemmonitor.exe
OutputDir=..\artifacts
OutputBaseFilename=SystemMonitor-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\dist\SystemMonitor\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\System Monitor"; Filename: "{app}\systemmonitor.exe"

[Run]
Filename: "{app}\systemmonitor.exe"; Description: "Launch System Monitor"; Flags: nowait postinstall skipifsilent
