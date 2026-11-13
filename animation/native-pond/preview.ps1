param(
    [string]$WasiSdk = 'C:\esp\mini-app-sdk\wasi-sdk-34.0-x86_64-windows'
)
$ErrorActionPreference = 'Stop'
$folder = Join-Path ([System.IO.Path]::GetTempPath()) 'esp-mosaico-pond-preview'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
'typedef int esp_err_t;' | Set-Content -LiteralPath (Join-Path $folder 'esp_err.h') -Encoding ascii
$wasm = Join-Path $folder 'pond-preview.wasm'
$raw = Join-Path $folder 'pond-preview.rgb'
$preview = Join-Path $PSScriptRoot '..\dist\pixel-koi-sunny-rain.gif'
New-Item -ItemType Directory -Path (Split-Path -Parent $preview) -Force | Out-Null
& (Join-Path $WasiSdk 'bin/clang.exe') --target=wasm32-unknown-unknown -mcpu=mvp -mbulk-memory `
    -std=c23 -nostdlib -O3 -Wall -Wextra -Werror '-DKOI_NATIVE_FIRMWARE=1' `
    -I $folder -I (Join-Path $PSScriptRoot 'main') -I (Join-Path $PSScriptRoot '..\pixel-fish') `
    '-Wl,--no-entry,--export-memory,--initial-memory=1572864' `
    '-Wl,--export=pond_scene_frame,--export=pond_scene_touch,--export=pond_scene_request_weather' `
    '-Wl,--export=pond_preview_pixels,--export=pond_preview_audio,--export=pond_audio_volume,--export=pond_preview_pellets,--export=pond_preview_damage_matches,--export=pond_scene_visible_fish' `
    (Join-Path $PSScriptRoot 'preview.c') -o $wasm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node (Join-Path $PSScriptRoot 'verify.mjs') $wasm $raw
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ffmpeg -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 -video_size 480x480 `
    -framerate 20 -i $raw -filter_complex_threads 1 `
    -filter_complex '[0:v]split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle' `
    -loop 0 $preview
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-Item -LiteralPath $preview | Select-Object Name,Length
