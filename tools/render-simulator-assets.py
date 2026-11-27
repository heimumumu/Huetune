from pathlib import Path
import struct
from PIL import Image
from companion.artwork import compose
from companion.text import track_text
root=Path(__file__).resolve().parents[1]
(root/"build").mkdir(exist_ok=True)
track={"title":"新時代 (ウタ from ONE PIECE FILM RED)","artist":"Ado","album":"新時代","src":"海贼王 FILM RED","dur":228,"idx":4,"total":11}
palette,images=compose(Image.open(root/"ui-preview/covers/04.jpg"))
for blob in track_text(track)+images:
    (root/f"build/blob-{blob.id}.bin").write_bytes(struct.pack("<HHBB",blob.width,blob.height,blob.format,blob.flags)+blob.data)
next_track={"title":"アイドル","artist":"YOASOBI","album":"アイドル","src":"【我推的孩子】OP","dur":213,"idx":1,"total":11}
_,next_images=compose(Image.open(root/"ui-preview/covers/01.jpg"))
for blob in track_text(next_track)+next_images:
    (root/f"build/next-blob-{blob.id}.bin").write_bytes(struct.pack("<HHBB",blob.width,blob.height,blob.format,blob.flags)+blob.data)
