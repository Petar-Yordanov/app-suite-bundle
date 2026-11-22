$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$exe = Get-ChildItem build -Recurse -File -Filter Arky.exe | Select-Object -First 1
if (!$exe) { throw 'Arky.exe was not found' }
$bundle = Join-Path $PWD 'dist/Arky'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/Arky.exe"
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/src/qml" "$bundle/Arky.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
$cli = Get-ChildItem build -Recurse -File -Filter arky-cli.exe | Select-Object -First 1
if (!$cli) { throw 'arky-cli.exe was not found' }
Copy-Item $cli.FullName "$bundle/arky-cli.exe"
Get-ChildItem $exe.DirectoryName -File -Filter *.dll | Copy-Item -Destination $bundle -Force
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio compiler runtime not found' }
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC\*\$arch\Microsoft.VC143.CRT") -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw "MSVC $arch runtime not found" }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, FINDEX_LICENSE.txt $bundle
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$check = Start-Process -FilePath "$bundle/Arky.exe" -ArgumentList '--smoke-test' -Wait -PassThru
if ($check.ExitCode -ne 0) { throw 'Deployed UI regression failed' }
& "$bundle/arky-cli.exe" --help
if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath "artifacts/Arky-windows-$arch-portable.zip" -Force
}

$template = @'
[Setup]
AppId=org.appsuite.Arky
AppName=Arky
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\Arky
DefaultGroupName=Arky
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\Arky.exe
OutputDir=artifacts
OutputBaseFilename=Arky-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Files]
Source: "dist\Arky\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
Name: "{autoprograms}\Arky"; Filename: "{app}\Arky.exe"
Name: "{autodesktop}\Arky"; Filename: "{app}\Arky.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\Arky.Archive"; ValueType: string; ValueName: ""; ValueData: "Archive file"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Arky.Archive\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Arky.exe,0"
Root: HKCU; Subkey: "Software\Classes\Arky.Archive\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Arky.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\Arky.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Arky"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\Arky.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Arky.exe"" ""%1"""
Root: HKCU; Subkey: "Software\AppSuite\Arky\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Arky"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\AppSuite\Arky\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Lightweight archive manager powered by libarchive and libzip."
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Arky"; ValueData: "Software\AppSuite\Arky\Capabilities"; Flags: uninsdeletevalue
'@
$extensions = @('zip','zipx','7z','rar','rar5','tar','gz','tgz','xz','txz','zst','bz2','tbz2','cpio','cab','iso','ar','xar','lha','lzh','warc','001')
foreach ($extension in $extensions) {
    $template += "`r`nRoot: HKCU; Subkey: `"Software\Classes\.$extension\OpenWithProgids`"; ValueType: none; ValueName: `"Arky.Archive`"; Flags: uninsdeletevalue"
    $template += "`r`nRoot: HKCU; Subkey: `"Software\Classes\Applications\Arky.exe\SupportedTypes`"; ValueType: string; ValueName: `".$extension`"; ValueData: `"`""
    $template += "`r`nRoot: HKCU; Subkey: `"Software\AppSuite\Arky\Capabilities\FileAssociations`"; ValueType: string; ValueName: `".$extension`"; ValueData: `"Arky.Archive`""
}
$template += "`r`n`r`n[Run]`r`nFilename: `"{app}\Arky.exe`"; Description: `"Launch Arky`"; Flags: nowait postinstall skipifsilent`r`n"
if ($arch -eq 'arm64') { $template = $template.Replace('x64compatible','arm64').Replace('Arky-windows-x64-setup','Arky-windows-arm64-setup') }
$template | Set-Content -Encoding UTF8 'Arky-installer.iss'
$compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($compiler) { $iscc = $compiler.Source } else { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!$env:ARKY_VERSION) { $env:ARKY_VERSION = '0.9.0' }
if (!(Test-Path $iscc)) { throw 'Inno Setup not installed' }
& $iscc "/DAppVersion=$env:ARKY_VERSION" 'Arky-installer.iss'
if (!(Test-Path "artifacts/Arky-windows-$arch-setup.exe")) { throw 'Installer missing' }
