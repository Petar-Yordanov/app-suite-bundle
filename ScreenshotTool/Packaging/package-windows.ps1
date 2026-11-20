$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$exe = Get-ChildItem build -Recurse -File -Filter screenshottool.exe | Select-Object -First 1
if (!$exe) { throw 'screenshottool.exe was not found' }
$bundle = Join-Path $PWD 'dist/ScreenshotTool'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/screenshottool.exe"
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/UI" "$bundle/screenshottool.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio compiler runtime not found' }
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC x64 runtime not found' }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, THIRD_PARTY.md $bundle

if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath 'artifacts/ScreenshotTool-windows-x64-portable.zip' -Force
}

$template = @'
[Setup]
AppId=org.appsuite.screenshottool
AppName=Screenshot Tool
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\ScreenshotTool
DefaultGroupName=Screenshot Tool
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\screenshottool.exe
OutputDir=artifacts
OutputBaseFilename=ScreenshotTool-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "dist\ScreenshotTool\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
Name: "{autoprograms}\Screenshot Tool"; Filename: "{app}\screenshottool.exe"
Name: "{autodesktop}\Screenshot Tool"; Filename: "{app}\screenshottool.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "ScreenshotTool"; Flags: uninsdeletevalue
[Run]
Filename: "{app}\screenshottool.exe"; Description: "Launch Screenshot Tool"; Flags: nowait postinstall skipifsilent
'@
$template | Set-Content -Encoding UTF8 'screenshottool-installer.iss'
$compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($compiler) { $iscc = $compiler.Source } else { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!(Test-Path $iscc)) { throw 'Inno Setup not installed' }
& $iscc "/DAppVersion=$env:APP_VERSION" 'screenshottool-installer.iss'
if (!(Test-Path 'artifacts/ScreenshotTool-windows-x64-setup.exe')) { throw 'Installer missing' }
