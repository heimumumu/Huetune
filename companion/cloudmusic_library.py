"""Read matching CloudMusic metadata locally; fetch its declared album art."""
import asyncio
import hashlib
import io
import json
import logging
import os
from pathlib import Path
import sqlite3
import tempfile
import time
import unicodedata
from urllib.parse import urlsplit, urlunsplit

from PIL import Image

LOG = logging.getLogger("media-controller")
MAX_COVER = 5*1024*1024


def normalized(value):
    return unicodedata.normalize("NFKC", value).strip()


def matching_track(data, title, artist):
    names = data.get("artists", data.get("ar", []))
    return (normalized(data.get("name", "")) == normalized(title)
            and normalized("/".join(a.get("name", "") for a in names)) == normalized(artist))


def cover_url(value):
    parts = urlsplit(value or "")
    if (parts.scheme not in ("http", "https") or not parts.hostname
            or not parts.hostname.endswith(".music.126.net") or parts.username or parts.password
            or parts.port is not None):
        return None
    return urlunsplit(("https", parts.hostname, parts.path, "param=600y600", ""))


def decode_cover(raw):
    if len(raw) > MAX_COVER:
        raise ValueError("Cover exceeds size limit")
    with Image.open(io.BytesIO(raw)) as image:
        if image.width*image.height > 16_000_000:
            raise ValueError("Cover exceeds pixel limit")
        return image.convert("RGB")


def download_cover(url):
    """Use Windows HTTPS so the frozen application needs no OpenSSL files."""
    import comtypes
    import comtypes.client
    comtypes.CoInitialize()
    request = None
    try:
        request = comtypes.client.CreateObject("WinHttp.WinHttpRequest.5.1", dynamic=True)
        request.SetTimeouts(2000, 2000, 2000, 3000)
        request.Open("GET", url, False)
        request.Send()
        if request.Status != 200 or not cover_url(request.Option[1]):
            raise OSError(f"Cover HTTP status {request.Status}")
        raw = bytes(request.ResponseBody)
        if len(raw) > MAX_COVER:
            raise ValueError("Cover exceeds size limit")
        return raw
    except comtypes.COMError as error:
        raise OSError(str(error)) from error
    finally:
        request = None
        comtypes.CoUninitialize()


class CloudMusicLibrary:
    def __init__(self, database=None, cache=None, changed=None):
        local = Path(os.environ.get("LOCALAPPDATA", "."))
        self.database = Path(database) if database else local/"NetEase/CloudMusic/Library/webdb.dat"
        self.cache = Path(cache) if cache else local/"ESP32MediaController/covers"
        self.key = None
        self.value = {}
        self.task = None
        self.retry_at = 0
        self.changed = changed

    def metadata(self, title, artist):
        if not self.database.is_file():
            return None
        with sqlite3.connect(self.database.resolve().as_uri()+"?mode=ro", uri=True, timeout=.2) as db:
            # Recent playback takes priority over older copies of the same song.
            for table, column, order in (("historyTracks", "jsonStr", " ORDER BY playtime DESC"),
                                          ("dbTrack", "jsonStr", ""), ("web_track", "track", "")):
                try:
                    rows = db.execute(f"SELECT {column} FROM {table} WHERE json_valid({column}) "
                                      f"AND json_extract({column}, '$.name')=?{order} LIMIT 32", (title,))
                    for row in rows:
                        data = json.loads(row[0])
                        if matching_track(data, title, artist):
                            return data
                except sqlite3.OperationalError:
                    continue
        return None

    def artwork(self, url):
        url = cover_url(url)
        if not url:
            return None, ""
        path = self.cache/(hashlib.sha256(url.encode()).hexdigest()+".jpg")
        try:
            raw = path.read_bytes()
            return decode_cover(raw), hashlib.sha256(raw).hexdigest()
        except (OSError, ValueError):
            pass
        raw = download_cover(url)
        image = decode_cover(raw)
        # Cache failures must not discard a successfully decoded cover.
        try:
            self.cache.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=self.cache, delete=False) as temporary:
                temporary.write(raw)
                pending = Path(temporary.name)
            try:
                pending.replace(path)
            finally:
                pending.unlink(missing_ok=True)
        except OSError:
            LOG.debug("Cover cache unavailable", exc_info=True)
        return image, hashlib.sha256(raw).hexdigest()

    def _load(self, key):
        try:
            data = self.metadata(*key)
            if not data:
                return {}
            album = data.get("album", data.get("al", {}))
            value = {"album": album.get("name", ""), "dur": int(data.get("duration", data.get("dt", 0)))//1000,
                     "idx": int(data.get("position", 0) or 0), "total": int(album.get("size", album.get("trackCount", 0)) or 0),
                     "cover": None, "cover_hash": ""}
            try:
                value["cover"], value["cover_hash"] = self.artwork(album.get("picUrl") or album.get("cover"))
            except (OSError, ValueError, Image.DecompressionBombError):
                LOG.warning("CloudMusic cover unavailable; will retry", exc_info=True)
            return value
        except (sqlite3.Error, OSError, ValueError, TypeError):
            LOG.debug("CloudMusic metadata unavailable", exc_info=True)
            return {}

    async def read(self, title, artist):
        key = title, artist
        if key != self.key:
            if self.task:
                self.task.cancel()
            self.key, self.value, self.task, self.retry_at = key, {}, None, 0
        if self.task and self.task.done():
            self.value = self.task.result()
            self.task = None
            self.retry_at = float("inf") if self.value.get("cover") is not None else time.monotonic()+10
        if self.task is None and time.monotonic() >= self.retry_at:
            # SQLite and downloads cannot hold up audio controls or heartbeat.
            self.task = asyncio.create_task(asyncio.to_thread(self._load, key))
            if self.changed:
                self.task.add_done_callback(lambda _: self.changed())
        return self.value
