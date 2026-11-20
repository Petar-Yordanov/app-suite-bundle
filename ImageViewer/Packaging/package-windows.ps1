$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$exe = Get-ChildItem build -Recurse -File -Filter imageviewer.exe | Select-Object -First 1
if (!$exe) { throw 'imageviewer.exe was not found' }
$bundle = Join-Path $PWD 'dist/ImageViewer'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/imageviewer.exe"
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/UI" "$bundle/imageviewer.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# Ensure all Qt image plugins are bundled, not just the ones seen by QML scanning.
New-Item -ItemType Directory -Force "$bundle/imageformats" | Out-Null
Copy-Item "$env:QT_ROOT_DIR/plugins/imageformats/*.dll" "$bundle/imageformats/" -Force
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio compiler runtime not found' }
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC x64 runtime not found' }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, THIRD_PARTY.md $bundle

if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath 'artifacts/ImageViewer-windows-x64-portable.zip' -Force
}

$template = @'
[Setup]
AppId=org.appsuite.imageviewer
AppName=Image Viewer
AppVersion={#AppVersion}
AppPublisher=AppSuite
DefaultDirName={localappdata}\Programs\ImageViewer
DefaultGroupName=Image Viewer
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\imageviewer.exe
OutputDir=artifacts
OutputBaseFilename=ImageViewer-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Files]
Source: "dist\ImageViewer\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Icons]
Name: "{autoprograms}\Image Viewer"; Filename: "{app}\imageviewer.exe"
Name: "{autodesktop}\Image Viewer"; Filename: "{app}\imageviewer.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\ImageViewer.Image"; ValueType: string; ValueName: ""; ValueData: "Image Viewer document"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\ImageViewer.Image\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\imageviewer.exe,0"
Root: HKCU; Subkey: "Software\Classes\ImageViewer.Image\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\imageviewer.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\imageviewer.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Image Viewer"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\imageviewer.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\imageviewer.exe"" ""%1"""
Root: HKCU; Subkey: "Software\AppSuite\ImageViewer\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Image Viewer"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\AppSuite\ImageViewer\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Lightweight image viewer for common, camera RAW and advanced image formats."
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Image Viewer"; ValueData: "Software\AppSuite\ImageViewer\Capabilities"; Flags: uninsdeletevalue
'@
$extensions = @('jpg','jpeg','jpe','png','bmp','dib','tif','tiff','gif','webp','avif','heic','heif','hif','svg','svgz','psd','ico','icns','tga','wbmp','pbm','pgm','ppm','3fr','arw','cr2','cr3','crw','dcr','dng','erf','fff','iiq','kdc','mef','mos','mrw','nef','nrw','orf','pef','raf','raw','rw2','rwl','sr2','srf','srw','x3f')
foreach ($extension in $extensions) {
    $template += "`r`nRoot: HKCU; Subkey: `"Software\Classes\.$extension\OpenWithProgids`"; ValueType: none; ValueName: `"ImageViewer.Image`"; Flags: uninsdeletevalue"
    $template += "`r`nRoot: HKCU; Subkey: `"Software\Classes\Applications\imageviewer.exe\SupportedTypes`"; ValueType: string; ValueName: `".$extension`"; ValueData: `"`""
    $template += "`r`nRoot: HKCU; Subkey: `"Software\AppSuite\ImageViewer\Capabilities\FileAssociations`"; ValueType: string; ValueName: `".$extension`"; ValueData: `"ImageViewer.Image`""
}
$template += "`r`n`r`n[Run]`r`nFilename: `"{app}\imageviewer.exe`"; Description: `"Launch Image Viewer`"; Flags: nowait postinstall skipifsilent`r`n"
$template | Set-Content -Encoding UTF8 'imageviewer-installer.iss'
$compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($compiler) { $iscc = $compiler.Source } else { $iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!(Test-Path $iscc)) { throw 'Inno Setup not installed' }
& $iscc "/DAppVersion=$env:APP_VERSION" 'imageviewer-installer.iss'
if (!(Test-Path 'artifacts/ImageViewer-windows-x64-setup.exe')) { throw 'Installer missing' }
