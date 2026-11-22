[Setup]
AppId=org.appsuite.terminalemulator
AppName=Terminal Emulator
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\TerminalEmulator
DefaultGroupName=Terminal Emulator
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\terminalemulator.exe
OutputDir=..\artifacts
OutputBaseFilename=TerminalEmulator-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "..\dist\TerminalEmulator\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Terminal Emulator"; Filename: "{app}\terminalemulator.exe"

[Run]
Filename: "{app}\terminalemulator.exe"; Description: "Launch Terminal Emulator"; Flags: nowait postinstall skipifsilent
