"""Status window backed by WebView2, with a Tk fallback."""
import base64
import ctypes
from ctypes import wintypes
import io
import json
import logging
import math
import os
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import ttk

from .instance import MUTEX

TITLE = "ESP32 多媒体控制器"
ICON = Path(__file__).with_name("assets") / "controller.ico"
LOG = logging.getLogger("media-controller")
_mutex = None


def already_running():
    global _mutex
    if os.name != "nt":
        return False
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateMutexW.restype = wintypes.HANDLE
    _mutex = kernel.CreateMutexW(None, False, MUTEX)
    if not _mutex:
        raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error() != 183:
        return False
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
    user.FindWindowW.restype = wintypes.HWND
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user.SetForegroundWindow.argtypes = [wintypes.HWND]
    hwnd = user.FindWindowW(None, TITLE)
    if hwnd:
        user.ShowWindow(hwnd, 9)
        user.SetForegroundWindow(hwnd)
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle(_mutex)
    _mutex = None
    return True


class TkStatusWindow:
    def __init__(self, events, logdir, on_quit):
        self.events, self.logdir, self.on_quit = events, logdir, on_quit
        self.root = tk.Tk()
        self.root.title(TITLE)
        if os.name == "nt":
            self.root.iconbitmap(default=str(ICON))
        self.root.geometry("560x400")
        self.root.minsize(520, 380)
        self.root.configure(background="#f4f5f7")
        style = ttk.Style(self.root)
        style.theme_use("vista" if os.name == "nt" else "clam")
        style.configure("TFrame", background="#f4f5f7")
        style.configure("TLabel", background="#f4f5f7", font=("Microsoft YaHei UI", 10))
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 17, "bold"))
        style.configure("State.TLabel", font=("Microsoft YaHei UI", 12, "bold"), foreground="#175b94")
        frame = ttk.Frame(self.root, padding=24)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text=TITLE, style="Title.TLabel").pack(anchor="w")
        ttk.Label(frame, text="免安装程序 · 已启动", padding=(0, 8, 0, 16)).pack(anchor="w")
        self.state = tk.StringVar(value="正在检查设备连接…")
        ttk.Label(frame, textvariable=self.state, style="State.TLabel", wraplength=500).pack(anchor="w")
        self.detail = tk.StringVar(value="连接支持数据传输的 USB 线，程序会自动连接设备。")
        ttk.Label(frame, textvariable=self.detail, wraplength=500, padding=(0, 10, 0, 20)).pack(anchor="w")
        self.media = tk.StringVar(value="正在读取播放信息…")
        ttk.Label(frame, textvariable=self.media, wraplength=500).pack(anchor="w")
        self.audio = tk.StringVar(value="")
        ttk.Label(frame, textvariable=self.audio, wraplength=500, padding=(0, 6, 0, 10)).pack(anchor="w")
        buttons = ttk.Frame(frame)
        buttons.pack(side="bottom", fill="x")
        ttk.Button(buttons, text="后台运行", command=self.hide).pack(side="left")
        ttk.Button(buttons, text="打开日志", command=lambda: os.startfile(self.logdir)).pack(side="left", padx=10)
        ttk.Button(buttons, text="退出程序", command=self.quit).pack(side="right")
        ttk.Label(frame, text="关闭窗口后仍会在右下角托盘运行，双击程序可重新打开。",
                  wraplength=500, padding=(0, 0, 0, 14)).pack(side="bottom", anchor="w")
        self.root.protocol("WM_DELETE_WINDOW", self.hide)
        self.root.after(100, self.poll)
        self.root.update_idletasks()

    def show(self):
        self.root.deiconify()
        self.root.lift()

    def hide(self):
        self.root.withdraw()

    def quit(self):
        self.on_quit()
        self.root.destroy()

    def poll(self):
        while True:
            try:
                kind, value = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "show":
                self.show()
            elif kind == "quit":
                self.quit()
                return
            elif kind == "media":
                title = value.get("title")
                playing = value.get("playback", [0])[0]
                self.media.set(("正在播放：" if playing else "已暂停：")+title+" · "+value.get("artist", "")
                               if value.get("source") and title else "当前没有检测到播放来源")
                volume = value.get("volume")
                if volume is not None:
                    self.audio.set(f'{value["output"]} · 音量 {volume[0]}% · '+("已静音" if volume[1] else "未静音"))
            elif kind == "status":
                self.state.set(value)
                if "原厂" in value:
                    self.detail.set("电脑已经识别到开发板，但它尚未运行控制器固件。需要先烧录项目中的固件，才能显示歌曲并接受本程序的数据。")
                elif value.startswith("已连接"):
                    self.detail.set("设备已连接。播放音乐后，歌曲信息与系统数据将自动发送到触摸屏。")
                elif "异常" in value or "失败" in value or "占用" in value:
                    self.detail.set("请点击“打开日志”查看原因；串口被占用时，请关闭其他串口监视器。")
                else:
                    self.detail.set("请连接开发板，并确认已烧录控制器固件。程序会自动重试连接。")
        self.root.after(100, self.poll)

    def run(self):
        self.root.mainloop()


class StatusActions:
    def __init__(self, events, logdir):
        self._events, self._logdir = events, logdir

    def hide(self):
        self._events.put(("hide", None))

    def open_log(self):
        os.startfile(self._logdir)

    def quit(self):
        self._events.put(("quit", None))


class StatusWindow:
    def __init__(self, events, logdir, on_quit):
        self.events, self.logdir, self.on_quit = events, logdir, on_quit
        self.window = None
        self.fallback = None
        self.stopped = threading.Event()
        self.quitting = False
        self.status = "等待设备连接"
        self.media = {}
        self.art = {}
        self.art_track = None
        self.system = {}

    def show(self):
        if self.fallback:
            self.fallback.show()
        else:
            self.window.show()

    def hide(self):
        if self.fallback:
            self.fallback.hide()
        else:
            self.window.hide()

    def quit(self):
        self.quitting = True
        if self.fallback:
            self.fallback.quit()
        else:
            self.on_quit()
            self.window.destroy()

    def _closing(self):
        if not self.quitting:
            self.events.put(("hide", None))
            return False

    def run(self):
        try:
            import webview
            from webview.platforms import winforms

            if winforms.renderer != "edgechromium":
                raise RuntimeError("WebView2 is unavailable")
            html = Path(__file__).with_name("ui").joinpath("status.html").read_text(encoding="utf-8")
            self.window = webview.create_window(
                TITLE, html=html, js_api=StatusActions(self.events, self.logdir),
                width=680, height=500, min_size=(600, 460),
            )
            self.window.events.before_show += self._ensure_client_width
            self.window.events.closing += self._closing
            try:
                webview.start(self._poll, gui="edgechromium", icon=str(ICON))
            finally:
                self.stopped.set()
            if self.quitting:
                return
            raise RuntimeError("WebView2 window stopped")
        except Exception as exc:
            LOG.warning("Using tkinter status window: %s", exc)
        self.fallback = TkStatusWindow(self.events, self.logdir, self.on_quit)
        self.fallback.run()

    def _ensure_client_width(self):
        form = self.window.native
        minimum_width = math.ceil(640 * form._scale)
        if form.ClientSize.Width < minimum_width:
            form.Width += minimum_width - form.ClientSize.Width

    def _poll(self):
        if not self.window.events.shown.wait(10):
            return
        if not self.window.events.loaded.wait(10):
            self.window.events.closing -= self._closing
            self.window.destroy()
            return
        previous = None
        while not self.stopped.is_set():
            while True:
                try:
                    kind, value = self.events.get_nowait()
                except queue.Empty:
                    break
                if kind == "quit":
                    self.quit()
                    return
                if kind == "show":
                    self.show()
                elif kind == "hide":
                    self.hide()
                elif kind == "status":
                    self.status = value
                elif kind == "media":
                    self.media = value
                elif kind == "art":
                    self._set_art(*value)
                elif kind == "sys":
                    self._set_system(*value)
            state = self._state()
            if state != previous:
                self.window.evaluate_js("render(" + json.dumps(state) + ")")
                previous = state
            self.stopped.wait(.25)

    def _set_art(self, track_id, image, palette):
        art = {"tint": ",".join(map(str, palette["tint"])), "alpha": palette["as"],
               "darkText": palette["dark"]}
        if image is not None:
            if track_id == self.art_track and "cover" in self.art:
                art["cover"] = self.art["cover"]
            else:
                image = image.convert("RGB")
                image.thumbnail((480, 480))
                data = io.BytesIO()
                image.save(data, format="JPEG", quality=85)
                art["cover"] = "data:image/jpeg;base64," + base64.b64encode(data.getvalue()).decode("ascii")
        self.art_track, self.art = track_id, art

    def _set_system(self, cpu, memory, gpu, rate):
        if rate >= 1_000_000:
            speed = f"{rate / 1_000_000:.1f} MB/s"
        else:
            speed = f"{rate / 1_000:.1f} KB/s"
        self.system = {"处理器": f"{cpu}%", "内存": f"{memory}%"}
        if gpu != 255:
            self.system["显卡"] = f"{gpu}%"
        self.system["网速"] = speed

    def _state(self):
        media = self.media
        state = {}
        if media.get("volume") is not None:
            volume, muted = media["volume"]
            state.update(output=media["output"], volume=volume, muted=bool(muted))
        if self.status.startswith("已连接 "):
            port = self.status.removeprefix("已连接 ")
            firmware = media.get("device", {}).get("fw")
            state.update(connState="ok", connText="已连接设备",
                         connSub=f"{port} · 固件 {firmware}" if firmware else port, sys=self.system)
            if media.get("source") and media.get("title"):
                playing, pos, duration = media["playback"]
                state.update(title=media["title"], artist=media["artist"], playing=bool(playing),
                             pos=min(pos, duration), dur=duration)
                if self.art_track == media.get("track_id"):
                    state.update(self.art)
            else:
                state["notice"] = {"title": "没有正在播放的音乐",
                                   "text": "在任意播放器里播放音乐，歌曲信息会同时显示在这里和触摸屏上。"}
        elif "原厂" in self.status:
            ports = self.status.partition("（")[2].rstrip("）")
            state.update(connState="warn", connText="需要烧录固件", connSub=f"{ports} · 出厂程序",
                         notice={"title": "开发板还没有控制器固件",
                                 "text": "电脑已经识别到开发板，但它运行的是出厂程序，暂时收不到歌曲信息。",
                                 "steps": ["按 README 里的“固件构建”步骤烧录固件", "烧录完成后重新插拔 USB 线"]})
        elif "占用" in self.status:
            state.update(connState="err", connText="无法连接", connSub="串口被占用",
                         notice={"title": "串口被其他程序占用",
                                 "text": "串口正被另一个程序打开，本程序无法连接设备。",
                                 "steps": ["关闭串口监视器、Arduino IDE 等正在使用串口的程序",
                                           "关闭后程序会自动重新连接"]})
        elif "异常" in self.status:
            state.update(connState="err", connText="程序异常",
                         notice={"title": "程序出错了", "text": "请点击打开日志查看原因。"})
        else:
            state.update(connText="等待设备", connSub="正在扫描串口",
                         notice={"title": "正在寻找设备", "searching": True,
                                 "text": "把开发板接到电脑上，程序会自动连接，不用做别的。",
                                 "steps": ["用支持数据传输的 USB 线连接开发板", "确认开发板已经烧录控制器固件",
                                           "连上后这里会显示正在播放的歌曲"]})
        return state
