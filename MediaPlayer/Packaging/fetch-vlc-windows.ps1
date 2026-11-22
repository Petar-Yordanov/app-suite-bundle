$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$version = '3.0.24'
$architecture = if ($env:VLC_ARCH -eq 'arm64') { 'winarm64' } else { 'win64' }
$base = "https://download.videolan.org/pub/videolan/vlc/$version"
$root = Join-Path $PWD '.deps/vlc'
New-Item -ItemType Directory -Force $root | Out-Null
function Get-Verified([string]$url,[string]$destination) {
    Invoke-WebRequest -Uri "$url.sha256" -OutFile "$destination.sha256" -UseBasicParsing
    $expected = ((Get-Content "$destination.sha256" -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
    if ($expected -notmatch '^[a-f0-9]{64}$') { throw 'Invalid VideoLAN SHA256 manifest' }
    Invoke-WebRequest -Uri $url -OutFile $destination -UseBasicParsing
    if ((Get-FileHash $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw "SHA256 verification failed: $url" }
}
$archive = Join-Path $root "vlc-$version-$architecture.zip"
$source = Join-Path $root "vlc-$version.tar.xz"
Get-Verified "$base/$architecture/vlc-$version-$architecture.zip" $archive
Get-Verified "$base/vlc-$version.tar.xz" $source
$runtime = Join-Path $root 'runtime'
New-Item -ItemType Directory -Force $runtime | Out-Null
Expand-Archive -Path $archive -DestinationPath $runtime -Force
& tar.exe -xf $source -C $root
$dll = Get-ChildItem $runtime -Recurse -File -Filter libvlc.dll | Select-Object -First 1
if (!$dll -or !(Test-Path (Join-Path $dll.DirectoryName 'plugins'))) { throw 'Official VLC runtime was not found' }
$include = Join-Path $root "vlc-$version/include"
if (!(Test-Path "$include/vlc/vlc.h")) { throw 'Official libVLC headers were not found' }
$env:VLC_SDK_ROOT = $dll.DirectoryName
$env:VLC_INCLUDE_DIR = $include
$env:PATH = "$env:VLC_SDK_ROOT;$env:PATH"
if ($env:GITHUB_ENV) {
    "VLC_SDK_ROOT=$env:VLC_SDK_ROOT" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    "VLC_INCLUDE_DIR=$env:VLC_INCLUDE_DIR" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    $env:VLC_SDK_ROOT | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
}
Write-Host "VLC_SDK_ROOT=$env:VLC_SDK_ROOT"
Write-Host "VLC_INCLUDE_DIR=$env:VLC_INCLUDE_DIR"
