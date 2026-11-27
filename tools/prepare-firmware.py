"""Fetch pinned hardware drivers and build the three fixed UI font subsets."""
import concurrent.futures
import json
from pathlib import Path
import re
import subprocess
import urllib.request
try:
    import truststore
    truststore.inject_into_ssl()
except ImportError:
    pass

ROOT = Path(__file__).resolve().parents[1]
REV = "d9ff7837148b47b33edadc4b87775c341cf35bbe"
API = f"https://api.github.com/repos/waveshareteam/ESP32-S3-Touch-LCD-3.5/git/trees/{REV}?recursive=1"
BASE = f"https://raw.githubusercontent.com/waveshareteam/ESP32-S3-Touch-LCD-3.5/{REV}/"


def fetch(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        with urllib.request.urlopen(source, timeout=90) as response:
            target.write_bytes(response.read())


def main():
    tree = json.load(urllib.request.urlopen(API))["tree"]
    prefixes = {
        "examples/esp-idf/03_lvgl_system/components/esp_lcd_st7796/": "esp_lcd_st7796/",
        "examples/factory/components/XPowersLib/src/": "XPowersLib/src/",
    }
    jobs = []
    for item in tree:
        for prefix, destination in prefixes.items():
            if item["type"] == "blob" and item["path"].startswith(prefix):
                jobs.append((BASE+item["path"], ROOT/"firmware/components"/destination/item["path"][len(prefix):]))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda job: fetch(*job), jobs))
    (ROOT/"firmware/components/XPowersLib/CMakeLists.txt").write_text('idf_component_register(SRCS "src/XPowersLibInterface.cpp" INCLUDE_DIRS "src" REQUIRES esp_driver_i2c)\ntarget_compile_definitions(${COMPONENT_LIB} PUBLIC CONFIG_XPOWERS_ESP_IDF_NEW_API=1)\n', encoding="utf-8")
    (ROOT/"firmware/components/XPowersLib/Kconfig").write_text('menu "XPowersLib"\n    config XPOWERS_ESP_IDF_NEW_API\n        bool "Use ESP-IDF I2C master driver"\n        default y\nendmenu\n', encoding="utf-8")
    for name in ["LICENSE"]:
        fetch(BASE+name, ROOT/"firmware/components/WAVESHARE_LICENSE")
        fetch(BASE+"examples/factory/components/XPowersLib/LICENSE", ROOT/"firmware/components/XPowersLib/LICENSE")
    font = ROOT/".vendor/SourceHanSansSC-Regular.otf"
    fetch("https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.005R/OTF/SimplifiedChinese/SourceHanSansSC-Regular.otf", font)
    fetch("https://raw.githubusercontent.com/adobe-fonts/source-han-sans/2.005R/LICENSE.txt", ROOT/"firmware/generated/FONT_LICENSE.txt")
    ui = "\n".join((ROOT/"firmware/main"/name).read_text(encoding="utf-8") for name in ("ui.cpp", "main.cpp"))
    chars = ''.join(sorted(set(re.findall(r'[\u3000-\u9fff\uff00-\uffef]', ui))))
    subprocess.run(["npm.cmd", "install", "--prefix", str(ROOT/".vendor/font-tools"), "lv_font_conv@1.5.3"],check=True)
    converter=ROOT/".vendor/font-tools/node_modules/lv_font_conv/lv_font_conv.js"
    for size in [13, 18, 30]:
        subprocess.run(["node", str(converter), "--font", str(font), "--size", str(size), "--bpp", "4", "--format", "lvgl", "--range", "0x20-0x7e", "--symbols", chars, "--no-compress", "-o", str(ROOT/f"firmware/generated/mc_font_{size}.c")], check=True)
    subprocess.run(["node", str(converter), "--font", str(font), "--size", "48", "--bpp", "4",
                    "--format", "lvgl", "--range", "0x2d,0x30-0x3a", "--no-compress",
                    "-o", str(ROOT/"firmware/generated/mc_font_clock.c")], check=True)
    print("Hardware sources and UI fonts ready.")


if __name__ == "__main__":
    main()
