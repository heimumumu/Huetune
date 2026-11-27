$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force build | Out-Null
& .venv/Scripts/python.exe -m ziglang c++ -std=c++17 -Wno-nullability-completeness -O1 -UNDEBUG tests/firmware_core.cpp -o build/firmware-core-tests.exe
if ($LASTEXITCODE) { throw '核心测试编译失败' }
& ./build/firmware-core-tests.exe
if ($LASTEXITCODE) { throw '核心测试失败' }
