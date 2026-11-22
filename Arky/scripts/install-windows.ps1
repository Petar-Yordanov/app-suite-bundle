# Run this in an Administrator PowerShell window.
# MSVC compiler, linker, Windows SDK and supporting build utilities.
$ErrorActionPreference = 'Stop'
winget install --id Microsoft.VisualStudio.BuildTools --exact --accept-package-agreements --accept-source-agreements --override '--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --norestart'
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio Build Tools installation failed' }
winget install --id Xmake-io.Xmake --exact --accept-package-agreements --accept-source-agreements
if ($LASTEXITCODE -ne 0) { throw 'Xmake installation failed' }
winget install --id Git.Git --exact --accept-package-agreements --accept-source-agreements
if ($LASTEXITCODE -ne 0) { throw 'Git installation failed' }
Write-Host 'Installation completed. Open x64 Native Tools Command Prompt for VS, then verify cl and xmake --version.'
