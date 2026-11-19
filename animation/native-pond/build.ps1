param(
    [string]$BuildRoot = 'C:\esp\koi-pond-native',
    [string]$EspEnvironment = '',
    [switch]$StageOnly,
    [string]$Platform = '',
    [string]$Adapter = ''
)
$ErrorActionPreference = 'Stop'
if (-not $StageOnly) {
    if ($EspEnvironment) {
        if (!(Test-Path -LiteralPath $EspEnvironment -PathType Leaf)) { throw 'ESP-IDF environment script was not found.' }
        . $EspEnvironment
    }
    if (!(Get-Command idf.py -ErrorAction SilentlyContinue)) {
        throw 'Activate an ESP-IDF 6.1 environment or provide -EspEnvironment.'
    }
}
$stageArguments = @((Join-Path $PSScriptRoot 'stage.py'), $BuildRoot)
if ($Platform) { $stageArguments += @('--platform', $Platform) }
if ($Adapter) { $stageArguments += @('--adapter', $Adapter) }
$source = python @stageArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($StageOnly) { $source; exit }
$env:CMAKE_BUILD_PARALLEL_LEVEL = '8'
$env:PYTHONUTF8 = '1'
Push-Location -LiteralPath $source
try {
    idf.py --preview -B (Join-Path $BuildRoot 'build') -DIDF_TARGET=esp32s31 build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $image = Get-Item -LiteralPath (Join-Path $BuildRoot 'build/pixel_koi_pond.bin')
    if ($image.Length -gt 0x500000) { throw 'The firmware exceeds its 5 MiB OTA slot' }
    $description = Get-Content -Raw -LiteralPath (Join-Path $BuildRoot 'build/project_description.json') | ConvertFrom-Json
    $file = 'pixel-koi-pond-' + $description.project_version + '.bin'
    $dist = Join-Path $PSScriptRoot '..\dist'
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath $image.FullName -Destination (Join-Path $dist $file) -Force
    Get-FileHash -LiteralPath (Join-Path $dist $file)
} finally {
    Pop-Location
}
