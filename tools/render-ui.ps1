$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$zig=(Resolve-Path .venv/Lib/site-packages/ziglang/zig.exe).Path
& .venv/Scripts/python.exe tools/render-simulator-assets.py
cmake -S simulator -B build/ui-sim -G Ninja "-DCMAKE_C_COMPILER=$zig" '-DCMAKE_C_COMPILER_ARG1=cc' "-DCMAKE_CXX_COMPILER=$zig" '-DCMAKE_CXX_COMPILER_ARG1=c++' "-DCMAKE_AR=$zig" '-DCMAKE_C_ARCHIVE_CREATE=<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>' '-DCMAKE_C_ARCHIVE_FINISH=' '-DCMAKE_BUILD_TYPE=Release'
if ($LASTEXITCODE) { throw '模拟器配置失败' }
cmake --build build/ui-sim -j 8
if ($LASTEXITCODE) { throw '模拟器编译失败' }
& ./build/ui-sim/controller_ui_sim.exe
if ($LASTEXITCODE) { throw '界面渲染或重绘检查失败' }
& .venv/Scripts/python.exe -c "from pathlib import Path; from PIL import Image; [Image.open(p).save(p.with_suffix('.png')) for p in Path('build').glob('*.ppm')]"
