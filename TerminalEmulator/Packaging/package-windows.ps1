$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
if (!$env:APP_VERSION) { $env:APP_VERSION = '1.0.0' }
$exe = Get-ChildItem build -Recurse -File -Filter terminalemulator.exe | Select-Object -First 1
if (!$exe) { throw 'terminalemulator.exe not found' }
$bundle = Join-Path $PWD 'dist/TerminalEmulator'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/terminalemulator.exe"
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/UI" "$bundle/terminalemulator.exe"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC runtime not found' }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, THIRD_PARTY.md $bundle
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$p = Start-Process -FilePath "$bundle/terminalemulator.exe" -ArgumentList '--smoke-test' -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Deployed smoke test exit $($p.ExitCode)" }
if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath 'artifacts/TerminalEmulator-windows-x64-portable.zip' -Force
}
$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($iscc) { $compiler = $iscc.Source } else { $compiler = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!(Test-Path $compiler)) { throw 'Inno Setup compiler not found' }
& $compiler "/DAppVersion=$env:APP_VERSION" Packaging/installer.iss
if (!(Test-Path 'artifacts/TerminalEmulator-windows-x64-setup.exe')) { throw 'Installer missing' }
