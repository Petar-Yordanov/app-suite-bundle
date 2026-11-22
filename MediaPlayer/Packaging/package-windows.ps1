$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$arch = if ($env:VLC_ARCH -eq 'arm64') { 'arm64' } else { 'x64' }
$exe = Get-ChildItem build -Recurse -File -Filter mediaplayer.exe | Select-Object -First 1
if (!$exe) { throw 'mediaplayer.exe was not found' }
$bundle = Join-Path $PWD 'dist/MediaPlayer'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/mediaplayer.exe"
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/UI" "$bundle/mediaplayer.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# Bundle the official architecture-matched libVLC DLLs and all its plugins.
if (!$env:VLC_SDK_ROOT -or !(Test-Path "$env:VLC_SDK_ROOT/libvlc.dll")) { throw 'Set VLC_SDK_ROOT to the official VLC runtime directory' }
Copy-Item "$env:VLC_SDK_ROOT/*.dll" $bundle -Force
New-Item -ItemType Directory -Force "$bundle/plugins" | Out-Null
Copy-Item "$env:VLC_SDK_ROOT/plugins/*" "$bundle/plugins" -Recurse -Force
New-Item -ItemType Directory -Force "$bundle/VLC-licenses" | Out-Null
if ($env:VLC_INCLUDE_DIR) {
    Get-ChildItem (Split-Path $env:VLC_INCLUDE_DIR) -File | Where-Object { $_.Name -match '^(COPYING|AUTHORS|THANKS)' } | Copy-Item -Destination "$bundle/VLC-licenses"
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio compiler runtime not found' }
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC\*\$arch\Microsoft.VC143.CRT") -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw "MSVC $arch runtime not found" }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, THIRD_PARTY.md $bundle
$oldRoot = $env:VLC_SDK_ROOT
$env:VLC_SDK_ROOT = ''
$env:MEDIAPLAYER_BUNDLED_ONLY = '1'
$check = Start-Process -FilePath "$bundle/mediaplayer.exe" -ArgumentList '--check-engine' -Wait -PassThru
$env:VLC_SDK_ROOT = $oldRoot
Remove-Item Env:MEDIAPLAYER_BUNDLED_ONLY
if ($check.ExitCode -ne 0) { throw 'Deployed libVLC initialization failed' }

if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath "artifacts/MediaPlayer-windows-$arch-portable.zip" -Force
}

$template = @'
[Setup]
AppId=org.appsuite.mediaplayer
AppName=Media Player
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\MediaPlayer
DefaultGroupName=Media Player
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\mediaplayer.exe
OutputDir=artifacts
OutputBaseFilename=MediaPlayer-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Files]
Source: "dist\MediaPlayer\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
Name: "{autoprograms}\Media Player"; Filename: "{app}\mediaplayer.exe"
Name: "{autodesktop}\Media Player"; Filename: "{app}\mediaplayer.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\MediaPlayer.Video"; ValueType: string; ValueName: ""; ValueData: "Video file"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\MediaPlayer.Video\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\mediaplayer.exe,0"
Root: HKCU; Subkey: "Software\Classes\MediaPlayer.Video\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\mediaplayer.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\mediaplayer.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Media Player"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\mediaplayer.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\mediaplayer.exe"" ""%1"""
Root: HKCU; Subkey: "Software\AppSuite\MediaPlayer\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Media Player"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\AppSuite\MediaPlayer\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Lightweight local video player powered by libVLC."
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Media Player"; ValueData: "Software\AppSuite\MediaPlayer\Capabilities"; Flags: uninsdeletevalue
'@
$extensions = @('mp4','m4v','mkv','webm','avi','mov','qt','wmv','asf','flv','f4v','mpeg','mpg','mpe','m1v','m2v','mpv','ts','mts','m2ts','m2t','vob','ogv','ogm','3gp','3g2','divx','rm','rmvb','mxf','dv','mjpg','mjpeg','nsv','nut','y4m','h264','h265','hevc','av1','ivf','vc1','bik','smk','roq','wtv','dvr-ms','amv','mod','tod','gxf','m4s','mp2v','trp','fli','flc')
foreach ($extension in $extensions) {
    $template += "`r`nRoot: HKCU; Subkey: `"Software\Classes\.$extension\OpenWithProgids`"; ValueType: none; ValueName: `"MediaPlayer.Video`"; Flags: uninsdeletevalue"
    $template += "`r`nRoot: HKCU; Subkey: `"Software\Classes\Applications\mediaplayer.exe\SupportedTypes`"; ValueType: string; ValueName: `".$extension`"; ValueData: `"`""
    $template += "`r`nRoot: HKCU; Subkey: `"Software\AppSuite\MediaPlayer\Capabilities\FileAssociations`"; ValueType: string; ValueName: `".$extension`"; ValueData: `"MediaPlayer.Video`""
}
$template += "`r`n`r`n[Run]`r`nFilename: `"{app}\mediaplayer.exe`"; Description: `"Launch Media Player`"; Flags: nowait postinstall skipifsilent`r`n"
if ($arch -eq 'arm64') { $template = $template.Replace('x64compatible','arm64').Replace('MediaPlayer-windows-x64-setup','MediaPlayer-windows-arm64-setup') }
$template | Set-Content -Encoding UTF8 'mediaplayer-installer.iss'
$compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($compiler) { $iscc = $compiler.Source } else { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!(Test-Path $iscc)) { throw 'Inno Setup not installed' }
& $iscc "/DAppVersion=$env:APP_VERSION" 'mediaplayer-installer.iss'
if (!(Test-Path "artifacts/MediaPlayer-windows-$arch-setup.exe")) { throw 'Installer missing' }
