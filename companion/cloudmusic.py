"""Local accessibility adapter for CloudMusic versions without an SMTC session."""
import ctypes
from ctypes import wintypes
import re
import time

import psutil


def parse_title(value):
    title, separator, artist = value.rpartition(" - ")
    if not separator or not title.strip() or not artist.strip():
        return None
    return title.strip(), artist.strip()


def parse_footer(names, button):
    times = [int(m[1])*60+int(m[2]) for name in names
             if (m := re.fullmatch(r"(\d{1,3}):([0-5]\d)", name))]
    if len(times) < 2 or button not in ("play", "pause"):
        return None
    position, duration = times[-2:]
    return int(button == "pause"), min(position, duration, 65535), min(duration, 65535)


class CloudMusicReader:
    def __init__(self):
        import comtypes.client
        # Generate once during development; these interfaces are bundled in the exe.
        comtypes.client.GetModule("UIAutomationCore.dll")
        from comtypes.gen import UIAutomationClient as uia
        self.uia = uia
        self.automation = comtypes.client.CreateObject(uia.CUIAutomation, interface=uia.IUIAutomation)
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.GetWindowTextLengthW.argtypes = [wintypes.HWND]
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.IsIconic.argtypes = [wintypes.HWND]
        self.handle = None
        self.last = None
        self.next_title_check = 0
        self.footer_handle = None
        self.play_button = None
        self.time_labels = []

    def title_changed(self):
        now = time.monotonic()
        if now < self.next_title_check or not self.handle or not self.last:
            return False
        self.next_title_check = now+.1
        title = self._title(self.handle)
        return title is not None and title != self.last[0]

    def _audio_playing(self):
        from pycaw.pycaw import AudioUtilities, IAudioMeterInformation
        now = time.monotonic()
        for session in AudioUtilities.GetAllSessions():
            try:
                if session.Process and session.Process.name().lower() == "cloudmusic.exe" and session.State == 1:
                    # CloudMusic can leave its stream active after pressing pause.
                    # Meter this process, rather than treating an open stream as music.
                    if session._ctl.QueryInterface(IAudioMeterInformation).GetPeakValue() > 0.0000001:
                        self.last_sound = now
                        return 1
            except psutil.Error:
                continue
        last_sound = getattr(self, "last_sound", None)
        return int(last_sound is not None and now-last_sound < 2)

    def _remember(self, title, playback, verified=False):
        self.last = title, playback, time.monotonic()
        if verified and playback[0]:
            self.last_sound = time.monotonic()
        if not playback[0]:
            self.last_sound = None
        return {"title": title[0], "artist": title[1]}, tuple(map(int, playback))

    def _background(self, title):
        playing = self._audio_playing()
        position = duration = 0
        if self.last and self.last[0] == title:
            previous, timestamp = self.last[1:]
            duration = previous[2]
            # With no visible timeline, only extrapolate a known duration.
            # A song first seen minimized has an unknown (zero) timeline.
            if duration:
                position = min(duration, previous[1]+max(0, time.monotonic()-timestamp) if previous[0] else previous[1])
        return self._remember(title, (playing, position, duration))

    def _title(self, handle):
        buf = ctypes.create_unicode_buffer(self.user.GetWindowTextLengthW(handle)+1)
        self.user.GetWindowTextW(handle, buf, len(buf))
        return parse_title(buf.value)

    def _windows(self):
        pids = {p.info["pid"] for p in psutil.process_iter(["pid", "name"])
                if (p.info["name"] or "").lower() == "cloudmusic.exe"}
        handles = []

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def visit(handle, _):
            pid = wintypes.DWORD()
            self.user.GetWindowThreadProcessId(handle, ctypes.byref(pid))
            if pid.value in pids and self._title(handle):
                handles.append(handle)
            return True

        self.user.EnumWindows(visit, 0)
        return handles

    def read(self):
        from comtypes import COMError
        candidates = [self.handle] if self.handle and self._title(self.handle) else self._windows()
        for handle in candidates:
            title = self._title(handle)
            if title and self.user.IsIconic(handle):
                self.handle = handle
                return self._background(title)
            # Publish a newly detected title immediately. Do not make it wait
            # behind Chromium's accessibility traversal for the timeline.
            if title and self.last and title != self.last[0]:
                self.handle = handle
                return self._background(title)
            playback = None
            for attempt in range(2):
                try:
                    if self.footer_handle != handle or self.play_button is None:
                        root = self.automation.ElementFromHandle(handle)
                        play = root.FindFirst(self.uia.TreeScope_Descendants,
                            self.automation.CreatePropertyCondition(self.uia.UIA_AutomationIdPropertyId, "btn_pc_minibar_play"))
                        if not play:
                            break
                        footer = self.automation.ControlViewWalker.GetParentElement(play)
                        texts = footer.FindAll(self.uia.TreeScope_Descendants,
                            self.automation.CreatePropertyCondition(self.uia.UIA_ControlTypePropertyId, self.uia.UIA_TextControlTypeId))
                        elements = [texts.GetElement(i) for i in range(texts.Length)]
                        self.time_labels = [e for e in elements if re.fullmatch(r"\d{1,3}:[0-5]\d", e.CurrentName)]
                        self.play_button, self.footer_handle = play, handle
                    playback = parse_footer([e.CurrentName for e in self.time_labels], self.play_button.CurrentName)
                    if playback:
                        break
                    self.play_button = None
                except COMError:
                    self.play_button = None
                    if attempt:
                        raise
            title = self._title(handle)
            if title and playback:
                self.handle = handle
                return self._remember(title, playback, verified=True)
        self.handle = None
        return None, (0, 0, 0)
