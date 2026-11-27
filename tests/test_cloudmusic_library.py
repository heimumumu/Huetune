import asyncio
import hashlib
import io
import json
import sqlite3
import time
from types import SimpleNamespace

from PIL import Image
import pytest

from companion.cloudmusic_library import CloudMusicLibrary, cover_url, decode_cover


def test_matches_artist_and_prefers_most_recent_matching_record(tmp_path):
    path = tmp_path/"歌曲.dat"
    with sqlite3.connect(path) as db:
        db.execute("CREATE TABLE historyTracks(playtime INTEGER,jsonStr TEXT)")
        for stamp, artist, album in [(1, "Artist", "Old"), (2, "Artist", "New"), (3, "Other", "Wrong")]:
            db.execute("INSERT INTO historyTracks VALUES(?,?)", (stamp, json.dumps(
                {"name": "Song", "artists": [{"name": artist}], "album": {"name": album}})))
    before = hashlib.sha256(path.read_bytes()).digest()
    library = CloudMusicLibrary(path, tmp_path/"covers")
    assert library.metadata("Song", "Artist")["album"]["name"] == "New"
    assert library.metadata("Song", "Missing") is None
    assert hashlib.sha256(path.read_bytes()).digest() == before


def test_declared_cover_url_uses_official_https_cdn_only():
    assert cover_url("http://p3.music.126.net/a.jpg?param=50y50") == "https://p3.music.126.net/a.jpg?param=600y600"
    for value in ("file:///local.jpg", "https://example.com/a.jpg", "https://p3.music.126.net.evil/a.jpg", ""):
        assert cover_url(value) is None
    with pytest.raises((OSError, ValueError)):
        decode_cover(b"not an image")


def test_cover_cache_works_without_network_after_first_download(tmp_path, monkeypatch):
    raw = io.BytesIO()
    Image.new("RGB", (32, 32), (50, 100, 200)).save(raw, format="JPEG")
    url = "https://p3.music.126.net/a.jpg?param=600y600"
    calls = []
    def download(*args, **kwargs):
        calls.append(True)
        return raw.getvalue()
    monkeypatch.setattr("companion.cloudmusic_library.download_cover", download)
    library = CloudMusicLibrary(tmp_path/"missing.dat", tmp_path/"covers")
    first, digest = library.artwork(url)
    assert first.size == (32, 32) and digest == hashlib.sha256(raw.getvalue()).hexdigest()
    def offline(*args, **kwargs): raise OSError("offline")
    monkeypatch.setattr("companion.cloudmusic_library.download_cover", offline)
    second, second_digest = library.artwork(url)
    assert second.tobytes() == first.tobytes() and second_digest == digest and len(calls) == 1


def test_late_previous_song_download_cannot_replace_current_cover(tmp_path, monkeypatch):
    library = CloudMusicLibrary(tmp_path/"missing.dat", tmp_path/"covers")
    def load(key):
        time.sleep(.05 if key[0] == "First" else .005)
        return {"album": key[0], "cover": object()}
    monkeypatch.setattr(library, "_load", load)
    async def check():
        assert await library.read("First", "Artist") == {}
        await asyncio.sleep(.01)
        assert await library.read("Second", "Artist") == {}
        await asyncio.sleep(.09)
        result = await library.read("Second", "Artist")
        assert result["album"] == "Second"
    asyncio.run(check())


def test_late_cover_refreshes_same_song_without_changing_track_identity():
    from companion.windows import Media
    media = Media()
    media.manager = SimpleNamespace(get_current_session=lambda: None, get_sessions=lambda: [])
    media.cloudmusic = SimpleNamespace(read=lambda: ({"title": "Song", "artist": "Artist"}, (1, 12, 200)))
    details = {}
    async def read(*_): return details
    media.cloudlibrary = SimpleNamespace(read=read)
    async def check():
        first, _ = await media.read()
        assert first["cover"] is None
        image = Image.new("RGB", (32, 32), "blue")
        details.update(cover=image, cover_hash="cover-revision", album="Album", dur=200)
        enriched, _ = await media.read()
        assert enriched["id"] == first["id"] and enriched["cover"] is image and enriched["album"] == "Album"
        unchanged, _ = await media.read()
        assert unchanged is None
    asyncio.run(check())
