# Run from a VS x64 Native Tools shell using:
# powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1
# This is an executable verification harness, not a claim of a successful Windows build.
param(
    [ValidateSet('debug','release')][string]$Configuration = 'release',
    [string]$QtSdk = ''
)
$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Invoke-Checked([string]$Label, [string]$Program, [string[]]$Arguments) {
    Write-Host "`n=== $Label ===" -ForegroundColor Cyan
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label failed (exit $LASTEXITCODE)" }
}

# Use the CI-provided Qt SDK automatically, if present.
if (-not $QtSdk -and $env:QT_ROOT_DIR) { $QtSdk = $env:QT_ROOT_DIR }
if ($QtSdk) {
    $QtSdk = (Resolve-Path $QtSdk).Path
    if (-not (Test-Path (Join-Path $QtSdk 'bin\qmlimportscanner.exe'))) {
        throw "Qt SDK is missing qmlimportscanner.exe: $QtSdk"
    }
}
Invoke-Checked 'Xmake version' 'xmake' @('--version')
$configArgs = @('f','-c','-p','windows','-a','x64','--vs=2022','-m',$Configuration,'-y')
if ($QtSdk) {
    if (-not (Test-Path $QtSdk)) { throw "Qt SDK not found: $QtSdk" }
    $configArgs += "--qt=$QtSdk"
}
Invoke-Checked 'Configure Windows x64' 'xmake' $configArgs
Invoke-Checked 'Build all targets' 'xmake' @('-y')
Invoke-Checked 'Portable archive engine tests' 'xmake' @('run','archive_tests',(Join-Path (Get-Location) 'tests/fixtures/password.zip'))
Invoke-Checked 'Native transfer planning tests' 'xmake' @('run','transfer_plan_tests')
Invoke-Checked 'Qt MIME/model tests' 'xmake' @('run','mime_tests')

# Windows Qt can render into an offscreen platform plugin without a desktop session.
$oldPlatform = $env:QT_QPA_PLATFORM
$oldBackend = $env:QT_QUICK_BACKEND
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_BACKEND = 'software'
    Invoke-Checked 'QML/window/backend startup smoke test' 'xmake' @('run','Arky','--smoke-test')
} finally {
    $env:QT_QPA_PLATFORM = $oldPlatform
    $env:QT_QUICK_BACKEND = $oldBackend
}
Write-Host "`nAll checks passed for this Windows machine." -ForegroundColor Green
