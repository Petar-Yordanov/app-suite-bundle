[Setup]
AppId=org.appsuite.webbrowser
AppName=Web Browser
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\WebBrowser
DefaultGroupName=Web Browser
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\webbrowser.exe
OutputDir=..\artifacts
OutputBaseFilename=WebBrowser-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\dist\WebBrowser\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Web Browser"; Filename: "{app}\webbrowser.exe"

[Run]
Filename: "{app}\webbrowser.exe"; Description: "Launch Web Browser"; Flags: nowait postinstall skipifsilent
