"""Antialiased CJK text to LVGL A4 (row aligned, high nibble first)."""
import math
import os
import re
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from .protocol import Blob


def font(size, bold=False):
    root = os.environ.get("WINDIR", "C:/Windows")
    path = os.environ.get("MC_FONT_BOLD" if bold else "MC_FONT", root + "/Fonts/" + ("msyhbd.ttc" if bold else "msyh.ttc"))
    return ImageFont.truetype(path, size)


def wrap(text, f, width):
    lines, line = [], ""
    for token in re.findall(r"[A-Za-z0-9]+(?:['’-][A-Za-z0-9]+)*|\s+|.", text):
        if line and f.getlength(line + token) > width:
            lines.append(line.rstrip())
            line = ""
        if f.getlength(token) > width:
            for char in token:
                if line and f.getlength(line+char) > width:
                    lines.append(line)
                    line = ""
                line += char
        else:
            line += token.lstrip() if not line else token
    if line or not lines:
        lines.append(line.rstrip())
    return lines


def render(blob_id, text, width, title=False, bold=False):
    f = font(30 if title else 18, title or bold)
    scroll = False
    lines = wrap(text, f, width) if title else [text]
    if title and len(lines) > 2:
        lines, scroll = [text], True
        width = min(8192, math.ceil(f.getlength(text))+2)
    elif not title:
        while text and f.getlength(text) > width:
            text = text[:-2].rstrip("…") + "…" if len(text) > 1 else ""
        lines = [text]
    height = (36*len(lines)) if title else 24
    width += width % 2
    im = Image.new("L", (width, height))
    draw = ImageDraw.Draw(im)
    for i, line in enumerate(lines):
        draw.text((0, i*36 if title else 0), line, font=f, fill=255, anchor="lt")
    px = ((np.asarray(im, dtype=np.uint16)+8)//17).astype(np.uint8)
    data = ((px[:, ::2] << 4) | px[:, 1::2]).tobytes()
    return Blob(blob_id, width, height, 2, int(scroll), data)


def track_text(track):
    out = []
    for title_id, artist_id, width in [(10, 12, 226), (11, 13, 190)]:
        out.extend([render(title_id, track["title"], width, title=True), render(artist_id, track["artist"], width)])
    duration = track.get("dur", 0)
    values = [track.get("album", ""), track.get("src", ""), f"{duration//60}:{duration%60:02}" if duration else "",
              f'{track["idx"]:02} / {track["total"]:02}' if track.get("total") and track.get("idx") else ""]
    return out + [render(20+i, value, 170, bold=True) for i, value in enumerate(values)]
