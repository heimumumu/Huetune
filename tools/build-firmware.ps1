param([string]$Port='', [switch]$Flash, [string]$BuildDir=(Join-Path $env:LOCALAPPDATA 'ESP32MediaController/firmware-build'))
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
if (!$env:IDF_PATH) { throw '请先打开 ESP-IDF 5.5 的 PowerShell 环境' }
& .venv/Scripts/python.exe tools/prepare-firmware.py
if ($LASTEXITCODE) { throw '驱动或字体准备失败' }
idf.py -C firmware -B $BuildDir build
if ($LASTEXITCODE) { throw '固件构建失败' }
if ($Flash) {
    if (!$Port) { throw '烧录时请指定 -Port COM端口号' }
    idf.py -C firmware -B $BuildDir -p $Port flash
    if ($LASTEXITCODE) { throw '烧录失败' }
}
