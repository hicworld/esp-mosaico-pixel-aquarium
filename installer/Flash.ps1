[CmdletBinding()]
param(
    [string]$Port = '',
    [switch]$Check
)
$ErrorActionPreference = 'Stop'

try {
    $toolPython = Join-Path $PSScriptRoot '.venv-flash\Scripts\python.exe'
    if (!(Test-Path -LiteralPath $toolPython)) {
        $basePython = $null
        $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
        if ($launcher) {
            $basePython = & $launcher.Source -3 -c 'import sys; print(sys.executable)'
            if ($LASTEXITCODE -ne 0) { $basePython = $null }
        }
        if (!$basePython) {
            $command = Get-Command python.exe -ErrorAction SilentlyContinue
            if ($command -and $command.Source -notlike '*\WindowsApps\*') { $basePython = $command.Source }
        }
        if (!$basePython) { throw '请安装 64 位 Python 3.11，并勾选 Add Python to PATH，然后重试。' }
        & $basePython -c "import struct,sys; sys.exit(0 if sys.version_info >= (3, 10) and struct.calcsize('P') == 8 else 1)"
        if ($LASTEXITCODE -ne 0) { throw '需要 64 位 Python 3.10 或更新版本，建议使用 3.11。' }
        & $basePython -m venv (Join-Path $PSScriptRoot '.venv-flash')
        if ($LASTEXITCODE -ne 0) { throw '创建烧录工具环境失败。' }
    }
    & $toolPython -c "import importlib.metadata as m,sys; versions={d.metadata['Name'].lower():d.version for d in m.distributions()}; sys.exit(0 if versions.get('esptool')=='5.3.1' and versions.get('pyserial')=='3.5' else 1)"
    if ($LASTEXITCODE -ne 0) {
        & $toolPython -m pip install -r (Join-Path $PSScriptRoot 'requirements-flash.txt')
        if ($LASTEXITCODE -ne 0) { throw '安装烧录依赖失败，请检查网络后重试。' }
    }

    if ($Check) {
        & $toolPython (Join-Path $PSScriptRoot 'flash_firmware.py') --check
    } else {
        if (!$Port) {
            & $toolPython -m serial.tools.list_ports -v
            $Port = Read-Host '请输入设备串口，例如 COM19'
        }
        & $toolPython (Join-Path $PSScriptRoot 'flash_firmware.py') --port $Port
    }
    if ($LASTEXITCODE -ne 0) { throw '操作未完成，请查看上方输出。' }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
