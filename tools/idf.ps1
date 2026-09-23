# MAO build helper: activates the project's ESP-IDF v6.0.3 environment for this
# PowerShell process only, then forwards the remaining arguments to idf.py.
#
#   .\tools\idf.ps1 build                        # dev profile (default) -> build/
#   .\tools\idf.ps1 -p COM13 flash monitor
#   .\tools\idf.ps1 release build                # release profile       -> build-release/
#   .\tools\idf.ps1 release -p COM13 flash
#
# A profile layers sdkconfig.<profile> over sdkconfig.defaults. Each profile
# keeps its own build directory and generated sdkconfig so they never mix.
# (The profile is a leading word rather than a -Parameter because PowerShell
# would prefix-match idf.py options such as -p.)
$ErrorActionPreference = 'Stop'
$idfArgs = @($args)
$buildProfile = 'dev'
if ($idfArgs.Count -gt 0 -and ($idfArgs[0] -eq 'dev' -or $idfArgs[0] -eq 'release')) {
    $buildProfile = $idfArgs[0]
    $idfArgs = @($idfArgs | Select-Object -Skip 1)
}

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

$profileArgs = @("-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$buildProfile")
if ($buildProfile -eq 'release') {
    $profileArgs += @('-B', 'build-release', '-DSDKCONFIG=build-release/sdkconfig')
}

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    idf.py @profileArgs @idfArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
