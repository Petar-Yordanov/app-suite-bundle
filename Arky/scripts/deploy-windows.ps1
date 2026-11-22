param(
    [Parameter(Mandatory=$false)][string]$QtBin = "",
    [Parameter(Mandatory=$false)][string]$Configuration = "release"
)

$ErrorActionPreference = "Stop"
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Exe = Get-ChildItem (Join-Path $Project "build") -Recurse -Filter "Arky.exe" -ErrorAction Stop |
    Where-Object { $_.FullName -match [regex]::Escape($Configuration) } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Exe) { throw "Build Arky first: xmake f -m $Configuration; xmake" }

$Tool = $null
if ($QtBin) {
    $Tool = Join-Path $QtBin "windeployqt.exe"
} else {
    $Command = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($Command) { $Tool = $Command.Source }
}
if (-not $Tool -or -not (Test-Path $Tool)) {
    throw "windeployqt.exe not found. Pass -QtBin 'C:\path\to\Qt\6.x\msvc2022_64\bin'. Qt installed by Xmake may be under ~/.xmake/packages."
}

$Dest = Join-Path $Project "dist\Arky"
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item $Exe.FullName (Join-Path $Dest "Arky.exe") -Force
$Args = @('--qmldir', (Join-Path $Project 'src\qml'), '--dir', $Dest)
if ($Configuration -eq 'release') { $Args += '--release' }
else { $Args += '--debug' }
$Args += (Join-Path $Dest "Arky.exe")
& $Tool @Args
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
Write-Host "Deployed Qt runtime and QML plugins to: $Dest"
Write-Host "NOTE: libarchive and any non-Qt dynamic dependencies must also be present."
Write-Host "Test the application from this folder on a clean machine before distribution."
