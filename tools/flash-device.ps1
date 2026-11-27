param([Parameter(Mandatory=$true)][string]$Port, [string]$Python='')
$ErrorActionPreference='Stop'
if (!$Python) {
    $candidates=@((Join-Path $PSScriptRoot '../../.venv/Scripts/python.exe'), (Join-Path $PSScriptRoot '../.venv/Scripts/python.exe'))
    $Python=$candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (!$Python) { $Python='python' }
}
Push-Location $PSScriptRoot
try {
    # Keep NVS intact by writing only the three firmware regions.
    & $Python -m esptool --chip esp32s3 -p $Port --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 media_controller.bin
    if ($LASTEXITCODE) { throw '烧录失败' }
} finally { Pop-Location }
