"""Tray application. Serial I/O never shares a thread with cover rendering."""
import argparse
import asyncio
import datetime
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import queue
import struct
import sys
import threading
import time
import json
import serial
from serial.tools import list_ports
from . import __version__
from .protocol import Parser, Type, encode, json_bytes, tag
from .artwork import compose
from .text import track_text

LOG = logging.getLogger("media-controller")
VID_PID = {(0x303a, 0x4001)}


class Link:
    def __init__(self, inbox, status, port=None):
        self.inbox, self.status, self.port = inbox, status, port
        self.control = queue.Queue(maxsize=128)
        self.art = queue.Queue(maxsize=2)
        self.stop = threading.Event()
        self.connected = threading.Event()
        self.generation = 0
        self.last_device_status = {}
        self.last_blobs = {}
        self.thread = threading.Thread(target=self.run, daemon=True)

    def send(self, kind, payload=b""):
        try:
            self.control.put_nowait((kind, payload))
        except queue.Full:
            LOG.warning("control queue full")

    def publish(self, generation, messages, *, follow_up=False):
        if generation != self.generation:
            return
        if not follow_up:
            while True:
                try:
                    self.art.get_nowait()
                except queue.Empty:
                    break
        self.art.put_nowait((generation, iter(messages)))

    def run(self):
        while not self.stop.is_set():
            failed = False
            detected = list(list_ports.comports())
            ports = [self.port] if self.port else [p.device for p in detected if (p.vid, p.pid) in VID_PID]
            factory_ports = [p.device for p in detected if (p.vid, p.pid) == (0x303a, 0x1001)]
            for port in ports:
                try:
                    with serial.Serial(port, 115200, timeout=.01, write_timeout=.3) as ser:
                        self.last_blobs = {}
                        pending_blobs = {}
                        ser.dtr = True
                        parser, seq, rx, ping = Parser(), 0, time.monotonic(), 0
                        artwork = None
                        awaiting_ack = False
                        while not self.stop.is_set():
                            now = time.monotonic()
                            waiting = ser.in_waiting
                            outgoing = artwork is not None or not self.art.empty() or not self.control.empty()
                            incoming = ser.read(min(waiting, 65536)) if waiting else (b"" if outgoing else ser.read(1))
                            for kind, _, data in parser.feed(incoming):
                                received_at = time.monotonic()
                                if kind == Type.HELLO:
                                    hello = json.loads(data)
                                    if hello.get("proto") != 1 or (hello.get("w"), hello.get("h")) != (480, 320):
                                        raise ValueError("incompatible firmware")
                                    self.send(Type.HELLO_ACK, json_bytes({"proto": 1, "app": __version__}))
                                    awaiting_ack = True
                                    rx = received_at
                                    if self.connected.is_set():
                                        LOG.warning("Device restarted handshake on %s", port)
                                        self.connected.clear()
                                        self.inbox.put(("disconnected", b""))
                                    self.status("正在确认设备连接 " + port)
                                confirmed = False
                                if kind == Type.SETTINGS and awaiting_ack:
                                    confirmed = isinstance(json.loads(data), dict)
                                if kind == Type.DEVICE_STATUS:
                                    self.last_device_status = json.loads(data)
                                    confirmed = self.last_device_status.get("linked") is True
                                    if not confirmed and self.connected.is_set():
                                        LOG.warning("Device reports disconnected on %s", port)
                                        self.connected.clear()
                                        self.inbox.put(("disconnected", b""))
                                        self.status("设备连接中断，正在重连 " + port)
                                    if not self.connected.is_set() and not awaiting_ack:
                                        # The prior companion may have exited less than 3 s
                                        # ago. Re-ACK after a verified device response;
                                        # heartbeats alone would keep that stale session alive.
                                        if isinstance(self.last_device_status.get("fw"),str) and isinstance(self.last_device_status.get("linked"),bool):
                                            self.send(Type.HELLO_ACK,json_bytes({"proto":1,"app":__version__}))
                                            awaiting_ack=True
                                            confirmed=False
                                if confirmed and awaiting_ack:
                                    self.connected.set()
                                    awaiting_ack = False
                                    self.inbox.put(("connected", b""))
                                    self.status("已连接 " + port)
                                    LOG.info("Device confirmed connection on %s", port)
                                if self.connected.is_set() or awaiting_ack:
                                    rx = received_at
                                    self.inbox.put((kind, data))
                            now = time.monotonic()
                            if now-rx > 3:
                                break
                            if now-ping >= 1:
                                if self.connected.is_set() or awaiting_ack:
                                    self.send(Type.PING)
                                self.send(Type.GET_STATUS)
                                ping = now
                            if artwork is not None and generation != self.generation:
                                artwork = None
                            if artwork is None:
                                try:
                                    generation, artwork = self.art.get_nowait()
                                except queue.Empty:
                                    pass
                                if artwork is not None and generation != self.generation:
                                    artwork = None
                            # One blob chunk at a time; volume and heartbeat always go first.
                            batch = []
                            for _ in range(16):
                                try:
                                    batch.append(self.control.get_nowait())
                                except queue.Empty:
                                    break
                            if artwork is not None:
                                message = next(artwork, None)
                                if message is None:
                                    artwork = None
                                else:
                                    batch.append(message)
                            for kind, data in batch:
                                wire = encode(kind, seq, data)
                                sent = ser.write(wire)
                                if sent != len(wire):
                                    raise serial.SerialTimeoutException("short write")
                                if kind == Type.BLOB_BEGIN and len(data) == 15 and data[0] in (1, 2, 3):
                                    blob_id, track_tag, width, height, _, _, total = struct.unpack("<BIHHBBI", data)
                                    pending_blobs[blob_id] = (time.monotonic(), {"tag": track_tag, "width": width, "height": height, "bytes": total})
                                elif kind == Type.BLOB_END and len(data) == 5 and data[0] in pending_blobs:
                                    blob_id, crc = struct.unpack("<BI", data)
                                    began, info = pending_blobs.pop(blob_id)
                                    self.last_blobs = {**self.last_blobs, blob_id: {**info, "crc32": crc, "send_ms": round((time.monotonic()-began)*1000)}}
                                seq = (seq+1) & 255
                except serial.SerialException as exc:
                    failed = True
                    self.status("设备串口被占用或连接失败")
                    LOG.warning("%s: %s", port, exc)
                except Exception:
                    LOG.exception("serial session")
                finally:
                    if self.connected.is_set():
                        LOG.info("Connection closed on %s", port)
                    self.connected.clear()
                    self.generation += 1
                    self.inbox.put(("disconnected", b""))
                    while not self.control.empty():
                        self.control.get_nowait()
            if not failed:
                self.status("检测到原厂/调试固件（" + ", ".join(factory_ports) + "）" if not ports and factory_ports else "等待设备连接")
            self.stop.wait(1)


async def service(link, inbox, report=None, report_ui=None):
    import comtypes
    from .windows import Media, Volume, Telemetry
    comtypes.CoInitialize()
    volume_dirty = threading.Event()
    volume_dirty.set()
    volume = Volume(volume_dirty.set)
    media, telemetry = Media(), Telemetry()
    await media.start()
    last_track, bg_mode, next_media, next_sys, next_time = None, 0, 0, 0, 0
    last_playback = (0, 0, 0)
    next_volume = 0
    current_volume = None
    render_task = None
    immersive, page = False, 0

    async def render(track, generation, mode):
        try:
            texts = await asyncio.to_thread(track_text, track)
            track_tag = tag(track["id"])
            track_frame = (Type.TRACK, json_bytes({k: v for k, v in track.items() if k not in ("cover", "title", "artist")}))
            # Publish both heading variants before any artwork computation.
            heading = texts[0:2] + texts[2:4] if immersive and page == 0 else texts[2:4] + texts[0:2]
            link.publish(generation, [track_frame, *(message for blob in heading for message in blob.messages(track_tag))])
            if track.get("cover_pending"):
                return
            p, images = await asyncio.to_thread(compose, track["cover"], mode)
            if generation != link.generation:
                return
            palette = (Type.PALETTE, bytes([p["dark"], *p["tint"], round(p["a"]*100), round(p["as"]*100), round(p["ac"]*100)]))
            # Send the visible view first; alternate art and detail text can follow.
            primary = [images[0]] if immersive and page == 0 else [images[2], images[1]]
            secondary = [blob for blob in images if blob not in primary]
            ordered = [*primary, *secondary, *texts[4:]]
            link.publish(generation, [palette, *(message for blob in ordered for message in blob.messages(track_tag))], follow_up=True)
            if report_ui:
                report_ui("art", (track["id"], track["cover"], p))
        except Exception:
            LOG.exception("artwork processing")

    def refresh():
        nonlocal render_task
        link.generation += 1
        if render_task:
            render_task.cancel()
        if last_track:
            render_task = asyncio.create_task(render(last_track, link.generation, bg_mode))

    try:
        while not link.stop.is_set():
            while not inbox.empty():
                kind, data = inbox.get_nowait()
                if kind == "connected":
                    media.dirty = True
                    next_media = next_sys = next_time = 0
                    volume_dirty.set()
                    refresh()
                elif kind == Type.SET_VOLUME and len(data) == 1:
                    volume.set_volume(data[0])
                    volume_dirty.set()
                elif kind == Type.SET_MUTE and len(data) == 1:
                    volume.set_mute(data[0])
                    volume_dirty.set()
                elif kind == Type.REQ_COVER and len(data) == 1:
                    bg_mode = int(bool(data[0]))
                    refresh()
                elif kind == Type.SETTINGS:
                    try:
                        settings = json.loads(data)
                        immersive = settings.get("cover") == "immersive"
                        page = settings.get("page", 0)
                        mode = int(settings.get("bg", "blur") == "color")
                        if mode != bg_mode:
                            bg_mode = mode
                            refresh()
                    except (ValueError, TypeError):
                        LOG.warning("invalid device settings")
            now = time.monotonic()
            if link.connected.is_set():
                if media.source == "网易云播放栏" and media.cloudmusic.title_changed():
                    media.dirty = True
                if volume_dirty.is_set() or now >= next_volume:
                    volume_dirty.clear()
                    current_volume = volume.read()
                    link.send(Type.VOLUME, bytes(current_volume))
                    next_volume = now+1
                if now >= next_media or media.dirty:
                    try:
                        track, playback = await media.read()
                        if track:
                            LOG.info("Track from %s: %s / %s", media.source, track["title"], track["artist"])
                            last_track = track
                            refresh()
                        if media.source is None and last_track:
                            playback = (0, last_playback[1], last_playback[2])
                        last_playback = playback
                        link.send(Type.PLAYBACK, struct.pack("<BHH", *playback))
                        if report or report_ui:
                            value = {"source": media.source, "title": (last_track or {}).get("title", ""),
                                    "artist": (last_track or {}).get("artist", ""), "playback": playback,
                                    "cover": {"available": (last_track or {}).get("cover") is not None,
                                              "loading": (last_track or {}).get("cover_pending", False),
                                              "hash": (last_track or {}).get("cover_hash", ""),
                                              "sent_images": dict(link.last_blobs)},
                                    "output": volume.device_name, "volume": current_volume,
                                    "device": dict(link.last_device_status)}
                            if report_ui:
                                report_ui("media", {**value, "track_id": (last_track or {}).get("id")})
                            if report:
                                report(value)
                    except Exception:
                        LOG.exception("Media read")
                    next_media = now+1
                if now >= next_sys:
                    metrics = telemetry.read()
                    link.send(Type.SYSINFO, struct.pack("<BBBI", *metrics))
                    if report_ui:
                        report_ui("sys", metrics)
                    next_sys = now+1.5
                if now >= next_time:
                    offset = int(datetime.datetime.now().astimezone().utcoffset().total_seconds()/60)
                    link.send(Type.TIME, struct.pack("<Ih", int(time.time()), offset))
                    next_time = now+3600
            await asyncio.sleep(.02)
    finally:
        if render_task:
            render_task.cancel()
        volume.close()
        telemetry.close()
        comtypes.CoUninitialize()


def autostart(enabled):
    import winreg
    command = f'"{sys.executable}"' if getattr(sys, "frozen", False) else f'"{sys.executable}" -m companion.app'
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_SET_VALUE) as key:
        if enabled:
            winreg.SetValueEx(key, "ESP32MediaController", 0, winreg.REG_SZ, command)
        else:
            try:
                winreg.DeleteValue(key, "ESP32MediaController")
            except FileNotFoundError:
                pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="手动指定串口；默认按固件 VID/PID 自动发现")
    parser.add_argument("--console", action="store_true")
    parser.add_argument("--autostart", choices=["on", "off"])
    args = parser.parse_args()
    logdir = Path(os.environ.get("LOCALAPPDATA", "."))/"ESP32MediaController"
    logdir.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(logdir/"companion.log", maxBytes=2_000_000, backupCount=3, encoding="utf-8")
    logging.basicConfig(level=logging.INFO, handlers=[handler], format="%(asctime)s %(levelname)s %(message)s")
    if args.autostart:
        autostart(args.autostart == "on")
        return
    if not args.console:
        from .status_window import StatusWindow, already_running
        if already_running():
            return
    inbox, tray, ui_events = queue.Queue(), None, queue.Queue()

    def status(text):
        ui_events.put(("status", text))
        if tray:
            tray.title = "ESP32 多媒体控制器 · " + text

    link = Link(inbox, status, args.port)

    def quit_app():
        link.stop.set()
        if tray:
            tray.stop()

    window = StatusWindow(ui_events, logdir, quit_app) if not args.console else None
    LOG.info("Application started (%s)", __version__)
    exit_event = exit_watcher = None
    if window and os.name == "nt":
        from . import instance
        exit_event = instance.create_exit_event()

        def wait_for_installer():
            if instance.wait_for_exit(exit_event):
                ui_events.put(("quit", None))

        exit_watcher = threading.Thread(target=wait_for_installer, daemon=True)
        exit_watcher.start()

    def work():
        try:
            def report(value):
                value = dict(value, app_version=__version__)
                (logdir/"status.json").write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
            def report_ui(kind, value):
                ui_events.put((kind, value))
            asyncio.run(service(link, inbox, report, report_ui if window else None))
        except Exception:
            LOG.exception("companion stopped")
            status("程序异常，请查看日志")
            link.stop.set()

    worker = threading.Thread(target=work, daemon=True)
    link.thread.start()
    worker.start()
    try:
        if args.console:
            while worker.is_alive():
                worker.join(.5)
        else:
            import pystray
            from PIL import Image
            with Image.open(Path(__file__).with_name("assets") / "controller.png") as source:
                icon = source.resize((64, 64), Image.Resampling.LANCZOS)
            tray = pystray.Icon("ESP32MediaController", icon, "ESP32 多媒体控制器 · 等待设备连接",
                menu=pystray.Menu(pystray.MenuItem("打开状态窗口", lambda *_: ui_events.put(("show", None)), default=True),
                                  pystray.MenuItem("开机启动", lambda *_: autostart(True)),
                                  pystray.MenuItem("取消开机启动", lambda *_: autostart(False)),
                                  pystray.MenuItem("打开日志", lambda *_: os.startfile(logdir)),
                                  pystray.MenuItem("退出", lambda *_: ui_events.put(("quit", None)))))
            tray.run_detached()
            window.run()
    except KeyboardInterrupt:
        pass
    finally:
        link.stop.set()
        if exit_event:
            instance.signal(exit_event)
            exit_watcher.join()
            instance.close(exit_event)
        if tray:
            tray.stop()
        worker.join(5)
        link.thread.join(2)


if __name__ == "__main__":
    main()
