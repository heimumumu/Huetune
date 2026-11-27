$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$python = (Resolve-Path '.venv/Scripts/python.exe').Path
$cache = (New-Item -ItemType Directory -Force '.vendor/esptool-5.6.0').FullName
$payload = (New-Item -ItemType Directory -Force 'build/setup-payload').FullName

function Get-VerifiedFile($Name, $Url, $Hash) {
    $file = Join-Path $cache $Name
    if (!(Test-Path -LiteralPath $file)) {
        Invoke-WebRequest $Url -OutFile "$file.part"
        Move-Item -LiteralPath "$file.part" -Destination $file
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $Hash) {
        throw "SHA256 verification failed: $file"
    }
    return $file
}

$binary = Get-VerifiedFile 'esptool-windows.zip' `
    'https://github.com/espressif/esptool/releases/download/v5.6.0/esptool-v5.6.0-windows-amd64.zip' `
    'D55A96FBEB97FD4CB05BD93F201E5C8F6518B7BFCDE2FC6966FD0190FD891629'
$source = Get-VerifiedFile 'esptool-v5.6.0-source.tar.gz' `
    'https://codeload.github.com/espressif/esptool/tar.gz/refs/tags/v5.6.0' `
    '33C1907F45513240D04057B93857B1E1B155396DC41D96CF2AE4CB7AABAA782C'
Expand-Archive -LiteralPath $binary (Join-Path $cache windows) -Force
$upstream = Join-Path $cache 'windows/esptool-windows-amd64'
$flasher = Join-Path $upstream 'esptool.exe'
$stagedFlasher = Join-Path $payload 'esptool.exe'
if (!(Test-Path -LiteralPath $stagedFlasher) -or (Get-FileHash $stagedFlasher).Hash -ne (Get-FileHash $flasher).Hash) {
    Copy-Item -LiteralPath $flasher -Destination $stagedFlasher
}
Copy-Item -LiteralPath (Join-Path $upstream 'LICENSE') -Destination (Join-Path $payload 'esptool-LICENSE.txt')
Copy-Item -LiteralPath $source -Destination $payload
Copy-Item -LiteralPath 'companion/assets/controller.ico' -Destination $payload
Copy-Item -LiteralPath 'dist/ESP32MediaController.exe' -Destination $payload
foreach ($file in @('bootloader.bin', 'partition-table.bin', 'media_controller.bin')) {
    Copy-Item -LiteralPath "dist/firmware/$file" -Destination $payload
}

@'
import hashlib
import json
from pathlib import Path
from companion import __version__

root = Path('build/setup-payload')
image = (root / 'media_controller.bin').read_bytes()
assert image[32:36] == b'\x32\x54\xcd\xab', 'Missing ESP application descriptor'
firmware = image[48:80].split(b'\0', 1)[0].decode('ascii')
hashes = {}
for file in sorted(root.iterdir()):
    if file.name != 'manifest.json':
        with file.open('rb') as stream:
            hashes[file.name] = hashlib.file_digest(stream, 'sha256').hexdigest()
(root / 'manifest.json').write_text(json.dumps(dict(firmware=firmware, companion=__version__, sha256=hashes), indent=2), encoding='utf-8')
'@ | & $python -
if ($LASTEXITCODE) { throw 'Could not verify and package the firmware version' }

$icon = (Resolve-Path 'companion/assets/controller.ico').Path
& $python -m PyInstaller --noconfirm --onefile --windowed --name HuetuneSetup --paths (Get-Location).Path `
    --icon $icon --specpath build --workpath build/setup-freeze `
    --add-data "$($payload):." tools/huetune-setup.py
if ($LASTEXITCODE) { throw 'Installer build failed' }
