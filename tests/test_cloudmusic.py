from companion.cloudmusic import parse_title, parse_footer
import asyncio
from types import SimpleNamespace


def test_title_preserves_separator_inside_song():
    assert parse_title("Song - Remix - Artist") == ("Song - Remix", "Artist")
    assert parse_title("网易云音乐") is None
    assert parse_title(" - Artist") is None


def test_footer_reads_progress_and_real_play_button_state():
    assert parse_footer(["Song", "341", "01:23", "04:50", "极高"], "pause") == (1, 83, 290)
    assert parse_footer(["01:23", "04:50"], "play") == (0, 83, 290)
    assert parse_footer(["Song", "123"], "pause") is None
    assert parse_footer(["00:30", "02:00"], "unknown") is None


def test_media_uses_cloudmusic_when_smtc_has_no_session():
    from companion.windows import Media
    media = Media()
    media.manager = SimpleNamespace(get_current_session=lambda: None, get_sessions=lambda: [])
    values = iter([({"title": "Song", "artist": "Artist"}, (1, 12, 200)),
                   ({"title": "Song", "artist": "Artist"}, (0, 13, 200)),
                   ({"title": "Next", "artist": "Artist"}, (1, 0, 210))])
    media.cloudmusic = SimpleNamespace(read=lambda: next(values))
    async def no_details(*_):
        return {}
    media.cloudlibrary = SimpleNamespace(read=no_details)
    async def check():
        first, playback = await media.read()
        assert first["title"] == "Song" and playback == (1, 12, 200)
        assert media.source == "网易云播放栏"
        unchanged, paused = await media.read()
        assert unchanged is None and paused == (0, 13, 200)
        next_track, playback = await media.read()
        assert next_track["id"] != first["id"] and next_track["title"] == "Next"
        assert playback == (1, 0, 210)
    asyncio.run(check())


def test_paused_windows_session_does_not_hide_cloudmusic():
    from companion.windows import Media
    from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status

    media = Media()
    paused = SimpleNamespace(get_playback_info=lambda: SimpleNamespace(playback_status=Status.PAUSED))
    media.manager = SimpleNamespace(get_current_session=lambda: paused, get_sessions=lambda: [paused])
    media.cloudmusic = SimpleNamespace(read=lambda: ({"title": "Song", "artist": "Artist"}, (0, 28, 194)))

    async def no_details(*_):
        return {}

    media.cloudlibrary = SimpleNamespace(read=no_details)
    track, playback = asyncio.run(media.read())
    assert media.source == "网易云播放栏" and track["title"] == "Song" and playback == (0, 28, 194)


def test_playing_windows_session_wins_over_paused_current_session():
    from datetime import datetime, timedelta, timezone

    from companion.windows import Media
    from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status

    paused = SimpleNamespace(get_playback_info=lambda: SimpleNamespace(playback_status=Status.PAUSED))
    playing = SimpleNamespace(
        source_app_user_model_id="Spotify",
        get_playback_info=lambda: SimpleNamespace(playback_status=Status.PLAYING),
        get_timeline_properties=lambda: SimpleNamespace(start_time=timedelta(0), end_time=timedelta(seconds=194),
            position=timedelta(seconds=12), last_updated_time=datetime.now(timezone.utc)),
        add_media_properties_changed=lambda _: 1, remove_media_properties_changed=lambda _: None)

    async def properties():
        return SimpleNamespace(title="Spotify Song", artist="Artist", album_title="", thumbnail=None,
                               track_number=1, album_track_count=10)

    playing.try_get_media_properties_async = properties
    media = Media()
    media.manager = SimpleNamespace(get_current_session=lambda: paused, get_sessions=lambda: [paused, playing])
    track, playback = asyncio.run(media.read())
    assert media.source == "Spotify" and track["title"] == "Spotify Song" and playback == (1, 12, 194)
    assert media.cloudmusic is None


def test_minimized_player_keeps_title_and_does_not_invent_unknown_timeline(monkeypatch):
    from companion.cloudmusic import CloudMusicReader
    reader = CloudMusicReader.__new__(CloudMusicReader)
    reader.last = None
    reader._audio_playing = lambda: 1
    now = [100.0]
    monkeypatch.setattr("companion.cloudmusic.time.monotonic", lambda: now[0])
    title = ("Song", "Artist")
    assert reader._background(title) == ({"title": "Song", "artist": "Artist"}, (1, 0, 0))
    reader._remember(title, (1, 10, 200))
    now[0] = 102.5
    assert reader._background(title)[1] == (1, 12, 200)
    now[0] = 103.2
    assert reader._background(title)[1] == (1, 13, 200)
    assert reader._background(("Next", "Artist"))[1] == (1, 0, 0)


def test_open_audio_stream_is_not_treated_as_playback_after_pause(monkeypatch):
    from companion.cloudmusic import CloudMusicReader
    from pycaw.pycaw import AudioUtilities
    peak = [0.0]
    session = SimpleNamespace(Process=SimpleNamespace(name=lambda: "cloudmusic.exe"), State=1,
        _ctl=SimpleNamespace(QueryInterface=lambda _: SimpleNamespace(GetPeakValue=lambda: peak[0])))
    monkeypatch.setattr(AudioUtilities, "GetAllSessions", lambda: [session])
    now = [100.0]
    monkeypatch.setattr("companion.cloudmusic.time.monotonic", lambda: now[0])
    reader = CloudMusicReader.__new__(CloudMusicReader)
    assert reader._audio_playing() == 0
    peak[0] = .1
    assert reader._audio_playing() == 1
    peak[0] = 0
    now[0] = 101.0
    assert reader._audio_playing() == 1
    now[0] = 103.0
    assert reader._audio_playing() == 0


def test_title_change_is_detected_before_next_timeline_poll(monkeypatch):
    from companion.cloudmusic import CloudMusicReader
    reader = CloudMusicReader.__new__(CloudMusicReader)
    reader.handle = 123
    reader.last = (("Old", "Artist"), (1, 10, 200), 100)
    reader.next_title_check = 0
    now = [100.0]
    calls = []
    monkeypatch.setattr("companion.cloudmusic.time.monotonic", lambda: now[0])
    reader._title = lambda _: calls.append(True) or ("New", "Artist")
    assert reader.title_changed()
    now[0] = 100.05
    assert not reader.title_changed() and len(calls) == 1
    reader.last = (("New", "Artist"), (1, 0, 200), 100.05)
    now[0] = 100.11
    assert not reader.title_changed()


def test_verified_playback_bridges_short_audio_gap_without_extending_silence(monkeypatch):
    from companion.cloudmusic import CloudMusicReader
    from pycaw.pycaw import AudioUtilities
    reader = CloudMusicReader.__new__(CloudMusicReader)
    now = [100.0]
    monkeypatch.setattr("companion.cloudmusic.time.monotonic", lambda: now[0])
    monkeypatch.setattr(AudioUtilities, "GetAllSessions", lambda: [])
    reader._remember(("Old", "Artist"), (1, 20, 200), verified=True)
    now[0] = 100.5
    assert reader._background(("New", "Artist"))[1][0] == 1
    now[0] = 102.1
    assert reader._background(("New", "Artist"))[1][0] == 0
