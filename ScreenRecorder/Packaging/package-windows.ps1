$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$exe = Get-ChildItem build -Recurse -File -Filter screenrecorder.exe | Select-Object -First 1
if (!$exe) { throw 'screenrecorder.exe was not found' }
$bundle = Join-Path $PWD 'dist/ScreenRecorder'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/screenrecorder.exe"
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/UI" "$bundle/screenrecorder.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio compiler runtime not found' }
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC x64 runtime not found' }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, THIRD_PARTY.md $bundle
$ffmpeg = $env:FFMPEG_BINARY
if (!$ffmpeg) {
    $candidate = Get-ChildItem "$env:ChocolateyInstall/lib/ffmpeg/tools" -Recurse -File -Filter ffmpeg.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) { $ffmpeg = $candidate.FullName }
    else { $ffmpeg = (Get-Command ffmpeg.exe -ErrorAction Stop).Source }
}
if (!(Test-Path $ffmpeg) -or (Get-Item $ffmpeg).Length -lt 1MB) { throw 'Set FFMPEG_BINARY to the real FFmpeg executable, not a shim.' }
Copy-Item $ffmpeg "$bundle/ffmpeg.exe"
Get-ChildItem (Split-Path $ffmpeg) -File -Filter '*.dll' | Copy-Item -Destination $bundle -Force
& "$bundle/ffmpeg.exe" -hide_banner -version
if (!$env:APP_VERSION) { $env:APP_VERSION = '1.0.0' }
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$check = Start-Process -FilePath "$bundle/screenrecorder.exe" -ArgumentList '--smoke-test' -Wait -PassThru
if ($check.ExitCode -ne 0) { throw 'Deployed QML smoke test failed' }

if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath 'artifacts/ScreenRecorder-windows-x64-portable.zip' -Force
}

$template = @'
[Setup]
AppId=org.appsuite.screenrecorder
AppName=Screen Recorder
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\ScreenRecorder
DefaultGroupName=Screen Recorder
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\screenrecorder.exe
OutputDir=artifacts
OutputBaseFilename=ScreenRecorder-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Files]
Source: "dist\ScreenRecorder\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
Name: "{autoprograms}\Screen Recorder"; Filename: "{app}\screenrecorder.exe"
Name: "{autodesktop}\Screen Recorder"; Filename: "{app}\screenrecorder.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "ScreenRecorder"; Flags: uninsdeletevalue
[Run]
Filename: "{app}\screenrecorder.exe"; Description: "Launch Screen Recorder"; Flags: nowait postinstall skipifsilent
'@
$template | Set-Content -Encoding UTF8 'screenrecorder-installer.iss'
$compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($compiler) { $iscc = $compiler.Source } else { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!(Test-Path $iscc)) { throw 'Inno Setup not installed' }
& $iscc "/DAppVersion=$env:APP_VERSION" 'screenrecorder-installer.iss'
if (!(Test-Path 'artifacts/ScreenRecorder-windows-x64-setup.exe')) { throw 'Installer missing' }
