"""Windows SMTC, endpoint volume and system telemetry adapters."""
import asyncio
import hashlib
import io
import re
import time
import ctypes
from ctypes import wintypes
import psutil
from PIL import Image


class Media:
    def __init__(self):
        self.manager = self.session = None
        self.dirty = True
        self.tokens = []
        self.track = None
        self.session_dirty = True
        self.source = None
        self.cloudmusic = None
        self.cloudlibrary = None
        self.cloud_revision = None

    async def start(self):
        from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionManager
        self.manager = await GlobalSystemMediaTransportControlsSessionManager.request_async()
        self.manager_token = self.manager.add_current_session_changed(self.session_changed)

    def session_changed(self, *_):
        self.session_dirty = True
        self.dirty = True

    def changed(self, *_):
        self.dirty = True

    async def read(self):
        from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionPlaybackStatus
        from winsdk.windows.storage.streams import DataReader
        states = GlobalSystemMediaTransportControlsSessionPlaybackStatus
        current = self.manager.get_current_session()
        status = current.get_playback_info().playback_status if current else None
        cloud_info, cloud_playback = None, (0, 0, 0)
        if status != states.PLAYING:
            # Windows can retain a paused session after its player closes.
            # Look for actual playback before relying on the current session.
            for session in self.manager.get_sessions():
                if session.get_playback_info().playback_status == states.PLAYING:
                    current, status = session, states.PLAYING
                    break
            else:
                if self.cloudmusic is None:
                    from .cloudmusic import CloudMusicReader
                    self.cloudmusic = CloudMusicReader()
                cloud_info, cloud_playback = self.cloudmusic.read()
                if cloud_info or status == states.CLOSED:
                    current = None
        if self.session_dirty or bool(current) != bool(self.session) or (current and self.session and current.source_app_user_model_id != self.session.source_app_user_model_id):
            self.session_dirty = False
            if self.session:
                for remove, token in self.tokens:
                    remove(token)
            self.tokens = []
            self.session = current
            self.dirty = True
            if current:
                self.tokens = [(current.remove_media_properties_changed, current.add_media_properties_changed(self.changed))]
        current = self.session
        if not current:
            info, playback = cloud_info, cloud_playback
            self.source = "网易云播放栏" if info else None
            if not info:
                return None, playback
            if self.cloudlibrary is None:
                from .cloudmusic_library import CloudMusicLibrary
                self.cloudlibrary = CloudMusicLibrary(changed=self.changed)
            details = await self.cloudlibrary.read(info["title"], info["artist"])
            identity = hashlib.sha256(("cloudmusic\0"+info["title"]+"\0"+info["artist"]).encode()).hexdigest()[:32]
            pending = getattr(self.cloudlibrary, "task", None) is not None
            revision = (identity, details.get("cover_hash", ""), details.get("album", ""), details.get("dur", 0), pending)
            track = None
            if self.dirty or not self.track or self.track["id"] != identity or self.cloud_revision != revision:
                album = details.get("album", "")
                source = re.search(r"[《〈](.*?)[》〉]", album)
                track = dict(info, id=identity, album=album, src=source.group(1) if source else "",
                             dur=details.get("dur") or playback[2], idx=details.get("idx", 0), total=details.get("total", 0),
                             cover=details.get("cover"), cover_hash=details.get("cover_hash", ""), cover_pending=pending)
                self.track = track
                self.cloud_revision = revision
            self.dirty = False
            return track, playback
        self.source = current.source_app_user_model_id
        timeline = current.get_timeline_properties()
        playing = status == states.PLAYING
        duration = max(0, int((timeline.end_time-timeline.start_time).total_seconds()))
        pos = (timeline.position-timeline.start_time).total_seconds()
        if playing:
            import datetime
            pos += max(0, (datetime.datetime.now(datetime.timezone.utc)-timeline.last_updated_time).total_seconds())
        playback = (int(playing), min(65535, max(0, int(pos))), min(65535, duration))
        track = None
        if self.dirty:
            prop = await current.try_get_media_properties_async()
            cover = None
            raw = b""
            if prop.thumbnail:
                stream = await prop.thumbnail.open_read_async()
                try:
                    if stream.size <= 20*1024*1024:
                        reader = DataReader(stream)
                        await reader.load_async(stream.size)
                        raw_buffer = bytearray(stream.size)
                        reader.read_bytes(raw_buffer)
                        raw = bytes(raw_buffer)
                        try:
                            cover = Image.open(io.BytesIO(raw)).convert("RGB")
                        except (OSError, ValueError):
                            cover = None
                        reader.detach_stream()
                        reader.close()
                finally:
                    stream.close()
            album = prop.album_title or ""
            source = re.search(r"[《〈](.*?)[》〉]", album)
            identity = "\0".join([current.source_app_user_model_id, prop.title, prop.artist, album])
            track = {"id": hashlib.sha256(identity.encode()+raw).hexdigest()[:32], "title": prop.title or "没有正在播放的音乐",
                     "artist": prop.artist or "", "album": album, "src": source.group(1) if source else "",
                     "dur": duration, "idx": prop.track_number, "total": prop.album_track_count, "cover": cover}
            self.track = track
            self.dirty = False
        return track, playback


class Volume:
    def __init__(self, changed):
        from pycaw.callbacks import AudioEndpointVolumeCallback
        self.endpoint = None
        self.device_id = None
        self.device_name = ""

        class Callback(AudioEndpointVolumeCallback):
            def on_notify(self, *args):
                changed()

        self.callback = Callback()
        self._refresh_endpoint()

    def _refresh_endpoint(self):
        from comtypes import COMError
        from pycaw.pycaw import AudioUtilities
        device = AudioUtilities.GetSpeakers()
        if device.id == self.device_id:
            return
        endpoint = device.EndpointVolume
        endpoint.RegisterControlChangeNotify(self.callback)
        old = self.endpoint
        self.endpoint = endpoint
        self.device_id = device.id
        self.device_name = device.FriendlyName
        if old is not None:
            try:
                old.UnregisterControlChangeNotify(self.callback)
            except (OSError, ValueError, COMError):
                pass

    def read(self):
        self._refresh_endpoint()
        return round(self.endpoint.GetMasterVolumeLevelScalar()*100), int(self.endpoint.GetMute())

    def set_volume(self, value):
        self._refresh_endpoint()
        self.endpoint.SetMasterVolumeLevelScalar(min(100, max(0, value))/100, None)
        self.endpoint.SetMute(False, None)

    def set_mute(self, muted):
        self._refresh_endpoint()
        self.endpoint.SetMute(bool(muted), None)

    def close(self):
        if self.endpoint is not None:
            self.endpoint.UnregisterControlChangeNotify(self.callback)


class GPUCounter:
    """PDH wildcard expansion handles localized Windows counter names."""
    def __init__(self):
        self.query = wintypes.HANDLE()
        self.counter = wintypes.HANDLE()
        self.pdh = ctypes.WinDLL("pdh")
        self.valid = self.pdh.PdhOpenQueryW(None, 0, ctypes.byref(self.query)) == 0
        if self.valid:
            self.valid = self.pdh.PdhAddEnglishCounterW(self.query, r"\GPU Engine(*)\Utilization Percentage", 0, ctypes.byref(self.counter)) == 0
            self.pdh.PdhCollectQueryData(self.query)

    def read(self):
        if not self.valid or self.pdh.PdhCollectQueryData(self.query):
            return 255
        class Value(ctypes.Structure):
            _fields_ = [("status", wintypes.DWORD), ("value", ctypes.c_double)]
        class Item(ctypes.Structure):
            _fields_ = [("name", wintypes.LPWSTR), ("value", Value)]
        size, count = wintypes.DWORD(), wintypes.DWORD()
        self.pdh.PdhGetFormattedCounterArrayW(self.counter, 0x200, ctypes.byref(size), ctypes.byref(count), None)
        if not size.value:
            return 255
        buf = ctypes.create_string_buffer(size.value)
        if self.pdh.PdhGetFormattedCounterArrayW(self.counter, 0x200, ctypes.byref(size), ctypes.byref(count), buf):
            return 255
        items = ctypes.cast(buf, ctypes.POINTER(Item))
        engines = {}
        for i in range(count.value):
            item = items[i]
            if item.value.status in (0, 1) and "engtype_3D" in item.name:
                key = item.name.split("_luid_")[-1]
                engines[key] = engines.get(key, 0) + item.value.value
        return min(100, round(max(engines.values()))) if engines else 255

    def close(self):
        if self.query:
            self.pdh.PdhCloseQuery(self.query)


class Telemetry:
    def __init__(self):
        self.net = psutil.net_io_counters()
        self.t = time.monotonic()
        self.nvml = None
        self.pdh = GPUCounter()
        try:
            import pynvml
            pynvml.nvmlInit()
            self.nvml = pynvml
            self.gpus = [pynvml.nvmlDeviceGetHandleByIndex(i) for i in range(pynvml.nvmlDeviceGetCount())]
        except Exception:
            pass
        psutil.cpu_percent()

    def read(self):
        now, net = time.monotonic(), psutil.net_io_counters()
        rate = int(max(0, net.bytes_sent-self.net.bytes_sent, net.bytes_recv-self.net.bytes_recv)/max(.01, now-self.t))
        self.t, self.net = now, net
        gpu = 255
        try:
            gpu = max(self.nvml.nvmlDeviceGetUtilizationRates(g).gpu for g in self.gpus) if self.nvml and self.gpus else self.pdh.read()
        except Exception:
            gpu = self.pdh.read()
        return round(psutil.cpu_percent()), round(psutil.virtual_memory().percent), gpu, min(0xffffffff, rate)

    def close(self):
        self.pdh.close()
        if self.nvml:
            self.nvml.nvmlShutdown()
