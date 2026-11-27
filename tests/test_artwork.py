import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter
from companion.artwork import compose, palette, min_alpha, region, contrast, lum, rgb565, TEXT
from companion.text import render, wrap, font

ROOT = Path(__file__).resolve().parents[1]


def test_palette_reference_covers():
    expected = json.loads((ROOT/"ui-preview/palette.js").read_text(encoding="utf-8").split("=",1)[1].strip().rstrip(";"))
    for path in (ROOT/"ui-preview/covers").glob("*.jpg"):
        image = Image.open(path).convert("RGB")
        p = palette(image)
        ref = expected[path.name]
        assert p["dark"] == ref["dark"]
        assert p["a"] == ref["a"]
        assert p["as"] == ref["as"]
        assert p["tint"] == list(map(int, ref["tint"].split(",")))


def test_twenty_generated_palettes_meet_contrast():
    rng = np.random.default_rng(10)
    for i in range(20):
        array = rng.integers(0, 256, (20, 20, 3), dtype=np.uint8)
        image = Image.fromarray(array).resize((600,600)) if i>2 else Image.new("RGB",(600,600),["black","white","#6f737b"][i])
        p = palette(image)
        blur = ImageEnhance.Color(image.filter(ImageFilter.GaussianBlur(23))).enhance(1.2)
        for bounds, alpha in [((.49,.87,.25,.75),p["a"]),((.15,.87,.25,.75),p["as"])]:
            pixels=region(blur,*bounds)
            mixed=pixels*(1-alpha)+np.array(p["tint"])*alpha
            ratio=contrast(lum(mixed),lum(TEXT["dark" if p["dark"] else "light"]))
            assert np.percentile(ratio,10)>=4.48


def test_image_sizes_byte_order_and_dither():
    p, blobs=compose(None)
    assert [len(b.data) for b in blobs]==[307200,307200,93312]
    assert rgb565(Image.new("RGB",(1,1),"red"))==b"\x00\xf8"
    pixels=np.frombuffer(rgb565(Image.new("RGB",(16,16),(100,100,100))),dtype="<u2")
    assert len(set(pixels))>1


def test_text_layout_and_nibble_alignment():
    a=render(11,"短歌名",190,title=True)
    b=render(11,"これはとても長い曲のタイトルです"*10,190,title=True)
    assert (a.height,a.flags)==(36,0)
    assert (b.height,b.flags)==(36,1)
    assert b.width>190
    for blob in [a,b,render(20,"很长的专辑名称"*8,170,bold=True)]:
        assert len(blob.data)==((blob.width+1)//2)*blob.height
        assert any(blob.data)
    assert wrap("hello world",font(30,True),100)==["hello","world"]
