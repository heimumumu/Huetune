param([Parameter(Mandatory=$true)][string]$BuildDir)
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$target=Join-Path (Get-Location) 'dist/firmware'
New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir 'bootloader/bootloader.bin') -Destination $target
Copy-Item -LiteralPath (Join-Path $BuildDir 'partition_table/partition-table.bin') -Destination $target
Copy-Item -LiteralPath (Join-Path $BuildDir 'media_controller.bin') -Destination $target
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'flash-device.ps1') -Destination (Join-Path $target 'flash.ps1')
Get-ChildItem -LiteralPath $target -Filter '*.bin' | Get-FileHash -Algorithm SHA256 | ForEach-Object { $_.Hash+'  '+[IO.Path]::GetFileName($_.Path) } | Set-Content (Join-Path $target 'SHA256.txt')
