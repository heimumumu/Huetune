$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
& .venv/Scripts/python.exe -c "import comtypes.client; comtypes.client.GetModule('UIAutomationCore.dll')"
if ($LASTEXITCODE) { throw '准备系统媒体接口失败' }
& .venv/Scripts/python.exe -m PyInstaller --noconfirm ESP32MediaController.spec
if ($LASTEXITCODE) { throw '打包失败' }
