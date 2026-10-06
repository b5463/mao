# MAO build helper: activates the project's ESP-IDF v6.0.3 environment for this
# PowerShell process only, then forwards the remaining arguments to idf.py.
#
# ESP32-C3-LCDkit (target esp32c3), unchanged:
#   .\tools\idf.ps1 build                        # dev profile (default) -> build/
#   .\tools\idf.ps1 -p COM13 flash monitor
#   .\tools\idf.ps1 release build                # release profile       -> build-release/
#   .\tools\idf.ps1 release -p COM13 flash
#   .\tools\idf.ps1 factory build                # factory profile: self-test at boot -> build-factory/
#
# MAO_MAIN A1 (target esp32s3): the same profiles with an "s3-" prefix:
#   .\tools\idf.ps1 s3-dev build                 # -> build-s3-dev/
#   .\tools\idf.ps1 s3-release build             # -> build-s3-release/
#   .\tools\idf.ps1 s3-factory -p COM14 flash monitor
#
# A profile layers sdkconfig.<profile> over sdkconfig.defaults (IDF adds
# sdkconfig.defaults.<target> by itself). Each profile keeps its own build
# directory and generated sdkconfig so they never mix, and the target is
# passed on every call, so no "set-target" is needed (it would reset the
# profile's sdkconfig).
# (The profile is a leading word rather than a -Parameter because PowerShell
# would prefix-match idf.py options such as -p.)
$ErrorActionPreference = 'Stop'
$idfArgs = @($args)
$buildProfile = 'dev'
$known = @('dev', 'release', 'factory', 's3-dev', 's3-release', 's3-factory')
if ($idfArgs.Count -gt 0 -and $known -contains $idfArgs[0]) {
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

$target = 'esp32c3'
$overlay = $buildProfile
if ($buildProfile.StartsWith('s3-')) {
    $target = 'esp32s3'
    $overlay = $buildProfile.Substring(3)
}
$profileArgs = @("-DIDF_TARGET=$target", "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.$overlay")
if ($buildProfile -ne 'dev') {
    $profileArgs += @('-B', "build-$buildProfile", "-DSDKCONFIG=build-$buildProfile/sdkconfig")
}

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    idf.py @profileArgs @idfArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
