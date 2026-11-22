$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
if (!$env:APP_VERSION) { $env:APP_VERSION = '1.0.0' }
$exe = Get-ChildItem build -Recurse -File -Filter webbrowser.exe | Select-Object -First 1
if (!$exe) { throw 'webbrowser.exe not found' }
$bundle = Join-Path $PWD 'dist/WebBrowser'
New-Item -ItemType Directory -Force $bundle, artifacts | Out-Null
Copy-Item $exe.FullName "$bundle/webbrowser.exe"
New-Item -ItemType Directory -Force 'dist/deploy-qml' | Out-Null
Copy-Item UI/Main.qml, UI/DesktopPage.qml 'dist/deploy-qml'
& "$env:QT_ROOT_DIR/bin/windeployqt.exe" --release --no-compiler-runtime --qmldir "$PWD/dist/deploy-qml" "$bundle/webbrowser.exe"
if (!(Get-ChildItem $bundle -Recurse -File -Filter QtWebEngineProcess.exe)) { throw 'QtWebEngineProcess.exe not deployed' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT') -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC runtime not found' }
Copy-Item "$($crt.FullName)\*.dll" $bundle
Copy-Item README.md, THIRD_PARTY.md, COMPATIBILITY.md, REQUIREMENTS.md $bundle
$p = Start-Process -FilePath "$bundle/webbrowser.exe" -ArgumentList '--smoke-test' -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Deployed smoke test exit $($p.ExitCode)" }
$compatArgs = @('--compatibility-smoke')
if ($env:WEBBROWSER_REQUIRE_CODECS -eq '1') { $compatArgs += '--require-codecs' }
$p = Start-Process -FilePath "$bundle/webbrowser.exe" -ArgumentList $compatArgs -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Deployed compatibility smoke test exit $($p.ExitCode)" }
if ($env:PORTABLE -eq '1') {
    Compress-Archive -Path "$bundle/*" -DestinationPath 'artifacts/WebBrowser-windows-x64-portable.zip' -Force
}
$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
if ($iscc) { $compiler = $iscc.Source } else { $compiler = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" }
if (!(Test-Path $compiler)) { throw 'Inno Setup compiler not found' }
& $compiler "/DAppVersion=$env:APP_VERSION" Packaging/installer.iss
if (!(Test-Path 'artifacts/WebBrowser-windows-x64-setup.exe')) { throw 'Installer missing' }
