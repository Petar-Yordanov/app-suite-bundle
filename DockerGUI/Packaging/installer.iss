[Setup]
AppId=org.appsuite.dockergui
AppName=Docker GUI
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\DockerGUI
DefaultGroupName=Docker GUI
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\dockergui.exe
OutputDir=..\artifacts
OutputBaseFilename=DockerGUI-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\dist\DockerGUI\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Docker GUI"; Filename: "{app}\dockergui.exe"

[Run]
Filename: "{app}\dockergui.exe"; Description: "Launch Docker GUI"; Flags: nowait postinstall skipifsilent
