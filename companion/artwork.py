"""Reference palette algorithm, desktop compositing and dithered RGB565."""
import colorsys
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter, ImageOps
from .protocol import Blob

TEXT = {"light": np.array([255., 255, 255]), "dark": np.array([20., 20, 20])}


def lum(rgb):
    c = rgb / 255.
    c = np.where(c <= .03928, c / 12.92, ((c + .055) / 1.055) ** 2.4)
    return c @ np.array([.2126, .7152, .0722])


def contrast(a, b):
    return (np.maximum(a, b) + .05) / (np.minimum(a, b) + .05)


def region(im, u0, u1, v0, v1):
    w, h = im.size
    return np.asarray(im.crop((int(u0*w), int(v0*h), int(u1*w), int(v1*h))), dtype=float).reshape(-1, 3)


def tint_for(mean, mode):
    h, s, _ = colorsys.rgb_to_hsv(*(mean / 255))
    s, v = (min(s * 1.1, .7), .22) if mode == "light" else (min(s, .3), .93)
    return np.array(colorsys.hsv_to_rgb(h, s, v)) * 255


def min_alpha(px, tint, mode):
    tl = lum(TEXT[mode][None, :])[0]
    for a in np.arange(.25, .951, .05):
        if np.percentile(contrast(lum(px * (1-a) + tint*a), tl), 10) >= 4.5:
            return round(float(a), 2)
    return .95


def square(im):
    return ImageOps.fit(im.convert("RGB"), (600, 600), method=Image.Resampling.LANCZOS)


def palette(im):
    blurred = ImageEnhance.Color(im.filter(ImageFilter.GaussianBlur(23))).enhance(1.2)
    right = region(blurred, .49, .87, .25, .75)
    full = region(blurred, .15, .87, .25, .75)
    mean = right.mean(axis=0)
    mode, a, t = min(((m, min_alpha(right, tint_for(mean, m), m), tint_for(mean, m)) for m in TEXT), key=lambda x: x[1])
    return {"dark": mode == "dark", "tint": [int(x) for x in t], "a": a,
            "as": max(min_alpha(full, t, mode), .55), "ac": max(a, .86)}


def rgb565(im):
    # 8x8 ordered Bayer dithering; vectorized so cover work cannot starve heartbeat.
    b = np.array([[0, 2], [3, 1]], dtype=float)
    for _ in range(2):
        b = np.block([[4*b, 4*b+2], [4*b+3, 4*b+1]])
    h, w = im.height, im.width
    d = np.tile((b + .5)/64 - .5, ((h+7)//8, (w+7)//8))[:h, :w, None]
    levels = np.array([31, 63, 31])
    q = np.clip(np.floor(np.asarray(im, dtype=float)/255*levels + .5 + d), 0, levels).astype(np.uint16)
    return ((q[:, :, 0] << 11) | (q[:, :, 1] << 5) | q[:, :, 2]).astype("<u2").tobytes()


def compose(image, bg_mode=0):
    im = square(image if image is not None else Image.new("RGB", (600, 600), "#6F737B"))
    p = palette(im)
    # CSS inset -40px then scale(1.15), centered; blur before center crop.
    large = ImageOps.fit(im, (644, 460), method=Image.Resampling.LANCZOS)
    large = ImageEnhance.Color(large.filter(ImageFilter.GaussianBlur(22))).enhance(1.2)
    blurred = np.asarray(large.crop((82, 70, 562, 390)), dtype=float)
    sharp = np.asarray(im.resize((320, 320), Image.Resampling.LANCZOS), dtype=float)
    immersive = blurred.copy()
    mask = np.clip((230.4-np.arange(320))/96, 0, 1)[None, :, None]
    immersive[:, :320] = sharp*mask + immersive[:, :320]*(1-mask)
    tint = np.array(p["tint"])
    a = p["ac"] if bg_mode else p["a"]
    alpha = np.interp(np.arange(480)/480, [0, .26, .36, .46, .52, 1], [0, 0, .4*a, .85*a, a, a])[None, :, None]
    immersive = immersive*(1-alpha) + tint*alpha
    ua = max(p["as"], p["ac"]) if bg_mode else p["as"]
    uniform = blurred*(1-ua) + tint*ua
    images = [Image.fromarray(np.uint8(np.clip(immersive, 0, 255))), Image.fromarray(np.uint8(uniform)), im.resize((216, 216), Image.Resampling.LANCZOS)]
    return p, [Blob(i+1, img.width, img.height, 1, 0, rgb565(img)) for i, img in enumerate(images)]
