# 计算每张封面右侧区域的配色：模糊后取样 → 选文字颜色 → 求保证对比度的最低遮罩浓度
# 给电脑端配套程序参考：真机上由电脑端算好，随封面一起发给 ESP32
import colorsys, glob, json, os
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
TARGET = 4.5  # WCAG 正文对比度要求
TEXT = {"light": np.array([255.0, 255, 255]), "dark": np.array([20.0, 20, 20])}

def lum(rgb):  # sRGB 0-255 → 相对亮度
    c = rgb / 255.0
    c = np.where(c <= 0.03928, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return c @ np.array([0.2126, 0.7152, 0.0722])

def contrast(a, b):
    return (np.maximum(a, b) + 0.05) / (np.minimum(a, b) + 0.05)

def region(img, u0, u1, v0, v1):
    w, h = img.size
    box = (int(u0 * w), int(v0 * h), int(u1 * w), int(v1 * h))
    return np.asarray(img.crop(box), dtype=float).reshape(-1, 3)

def tint_for(mean, mode):  # 取封面色相：白字配深色遮罩，深字配浅色遮罩
    h, s, _ = colorsys.rgb_to_hsv(*(mean / 255))
    s, v = (min(s * 1.1, 0.7), 0.22) if mode == "light" else (min(s, 0.3), 0.93)
    return np.array(colorsys.hsv_to_rgb(h, s, v)) * 255

def min_alpha(px, tint, mode):  # 最暗/最亮的 10% 像素也要达到对比度要求
    tl = lum(TEXT[mode][None, :])[0]
    for a in np.arange(0.25, 0.951, 0.05):
        if np.percentile(contrast(lum(px * (1 - a) + tint * a), tl), 10) >= TARGET:
            return round(float(a), 2)
    return 0.95

out = {}
for f in sorted(glob.glob(os.path.join(ROOT, "covers", "*.jpg"))):
    im = Image.open(f).convert("RGB").filter(ImageFilter.GaussianBlur(23))
    im = ImageEnhance.Color(im).enhance(1.2)          # 与页面里的 blur(22px) saturate(1.2) 对应
    right = region(im, 0.49, 0.87, 0.25, 0.75)        # 屏幕右侧文字区对应的封面范围
    full = region(im, 0.15, 0.87, 0.25, 0.75)         # 子页面：整屏都是模糊封面
    mean = right.mean(axis=0)
    mode, a, t = min(((m, min_alpha(right, tint_for(mean, m), m), tint_for(mean, m)) for m in TEXT), key=lambda x: x[1])
    name = os.path.basename(f)
    out[name] = {"dark": mode == "dark", "tint": ",".join(str(int(x)) for x in t),
                 "a": a, "as": max(min_alpha(full, t, mode), 0.55), "ac": max(a, 0.86)}
    print(name, out[name])

with open(os.path.join(ROOT, "palette.js"), "w", encoding="utf-8") as fp:
    fp.write("// 由 tools/cover_palette.py 生成，请勿手改\nwindow.COVER_PALETTE = ")
    fp.write(json.dumps(out, ensure_ascii=False, indent=1) + ";\n")
