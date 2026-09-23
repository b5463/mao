# MAO build helper: activates the project's ESP-IDF v6.0.3 environment for this
# PowerShell process only, then forwards all arguments to idf.py.
#   .\tools\idf.ps1 build
#   .\tools\idf.ps1 -p COM13 flash monitor
$ErrorActionPreference = 'Stop'
$idfPath = if ($env:MAO_IDF_PATH) { $env:MAO_IDF_PATH } else { Join-Path $env:USERPROFILE 'esp\v6.0.3\esp-idf' }
if (-not (Test-Path (Join-Path $idfPath 'export.ps1'))) {
    throw "ESP-IDF not found at $idfPath (set MAO_IDF_PATH)"
}
$env:IDF_TOOLS_PATH = Join-Path $env:USERPROFILE '.espressif'
# export.ps1 writes progress to stderr; Windows PowerShell 5.1 would treat that
# as a terminating error under 'Stop'.
$ErrorActionPreference = 'Continue'
. (Join-Path $idfPath 'export.ps1') 2>&1 | Out-Null
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "ESP-IDF environment activation failed"
}
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    idf.py @args
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
