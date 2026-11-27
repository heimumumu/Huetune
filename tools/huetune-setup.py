"""Windows setup for the Waveshare ESP32-S3-Touch-LCD-3.5."""
import ctypes
import hashlib
import json
import logging
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
from serial.tools import list_ports
from tempfile import TemporaryDirectory

from companion import instance
from companion.protocol import Parser, Type, encode, json_bytes


ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1] / "build/setup-payload"))
APP_DIR = Path(os.environ["LOCALAPPDATA"]) / "ESP32MediaController"
RUNTIME, ROM = 0x4001, (0x0009, 0x1001)
FLASH_SIZE = 16 * 1024 * 1024
COMPANION = "ESP32MediaController.exe"
LOG = logging.getLogger("setup")


def ports():
    return [p for p in list_ports.comports() if p.vid == 0x303a and p.pid in (RUNTIME, *ROM)]


def identity(port):
    return re.sub(r"[^0-9a-f]", "", (port.serial_number or "").lower())


def wait_for(seconds, predicate):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if result := predicate():
            return result
        time.sleep(.2)


def same_board(target, pids):
    matches = [p for p in ports() if p.pid in pids and identity(p) == identity(target)]
    return matches[0] if len(matches) == 1 else None


def serial_version(device, install_version):
    with serial.Serial(device, 115200, timeout=.15, write_timeout=1) as link:
        link.dtr = True
        parser = Parser()
        deadline = time.monotonic() + 4
        next_probe = 0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_probe:
                link.write(encode(Type.HELLO_ACK, 0, json_bytes({"proto": 1})))
                link.write(encode(Type.GET_STATUS, 1))
                next_probe = now + .4
            for kind, _, data in parser.feed(link.read(link.in_waiting or 1)):
                if kind != Type.DEVICE_STATUS:
                    continue
                status = json.loads(data)
                version = status.get("fw", "")
                if status.get("linked") and re.fullmatch(r"\d+\.\d+\.\d+", version):
                    if install_version is not None:
                        if version_tuple(version) > version_tuple(install_version):
                            raise RuntimeError(f"板上已安装较新固件 {version}。请使用同版或更新的安装程序。")
                        if version_tuple(version) < (0, 2, 3):
                            raise RuntimeError("这版固件需要手动进入下载模式。按住 BOOT、按一下 RESET、松开 BOOT，再重试。")
                        LOG.info("Requesting download mode from %s on %s", version, device)
                        link.write(encode(Type.ENTER_BOOTLOADER, 2, b"BOOT"))
                        time.sleep(.25)
                    return version
    raise RuntimeError("无法确认板上运行的是 Huetune。请按住 BOOT、按一下 RESET、松开 BOOT 后重试。")


def runtime_version(port, install_version=None):
    # A stalled Windows USB driver can block serial open/close outside pyserial's timeouts.
    args = [sys.executable]
    if not getattr(sys, "frozen", False):
        args.append(__file__)
    with TemporaryDirectory(prefix="huetune-probe-") as folder:
        result = Path(folder) / "result.json"
        try:
            subprocess.run([*args, "--probe", port.device, install_version or "-", str(result)],
                           timeout=12, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError("Windows 串口未响应。请拔下 USB，等 3 秒再插回后重试。") from error
        payload = json.loads(result.read_text(encoding="utf-8"))
        if "error" in payload:
            raise RuntimeError(payload["error"])
        return payload["version"]


def version_tuple(version):
    return tuple(map(int, version.split(".")))


def esptool(port, *command, before="no-reset", after="no-reset", baud=460800, timeout=180):
    args = [str(ROOT / "esptool.exe"), "--chip", "esp32s3", "-p", port.device,
            "--baud", str(baud), "--before", before, "--after", after, *map(str, command)]
    result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
    output = result.stdout + result.stderr
    LOG.info("%s\n%s", command[0], output.strip())
    if result.returncode:
        raise RuntimeError(f"烧录工具执行 {command[0]} 失败。请查看安装日志。")
    return output


def verify_payload():
    manifest = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
    for name, expected in manifest["sha256"].items():
        with (ROOT / name).open("rb") as source:
            if hashlib.file_digest(source, "sha256").hexdigest() != expected:
                raise RuntimeError(f"安装包校验失败：{name}。请重新下载安装程序。")
    return manifest


def stop_companion():
    if not instance.running():
        return
    if not instance.request_exit() or not wait_for(20, lambda: not instance.running()):
        raise RuntimeError("请从右下角托盘退出正在运行的 Huetune，然后重试。")


def backup_flash(port):
    folder = APP_DIR / "backups"
    folder.mkdir(parents=True, exist_ok=True)
    name = f"flash-{identity(port)}-{time.strftime('%Y%m%d-%H%M%S')}-{time.time_ns() % 1000000:06d}"
    partial = folder / (name + ".part")
    image = folder / (name + ".bin")
    try:
        esptool(port, "read-flash", "0x0", hex(FLASH_SIZE), partial, timeout=240)
        if partial.stat().st_size != FLASH_SIZE:
            raise RuntimeError("原厂固件备份不完整，未开始烧录。")
        with partial.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        (folder / (name + ".sha256.txt")).write_text(f"{digest}  {image.name}\n", encoding="ascii")
        partial.rename(image)
    finally:
        partial.unlink(missing_ok=True)
    LOG.info("Original image saved to %s", image)
    return image


def status_report(previous, fw, app):
    diagnostic = APP_DIR / "status.json"
    try:
        if diagnostic.stat().st_mtime_ns <= previous:
            return False
        payload = json.loads(diagnostic.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    device = payload.get("device", {})
    return (payload.get("app_version") == app and device.get("linked") is True and device.get("fw") == fw)


def start_menu_shortcut(target):
    powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    script = ("$ErrorActionPreference='Stop'; "
              "$shell=New-Object -ComObject WScript.Shell; "
              "$menu=[Environment]::GetFolderPath('Programs'); "
              "$link=$shell.CreateShortcut((Join-Path $menu 'Huetune.lnk')); "
              "$link.TargetPath=$env:HUETUNE_SETUP_TARGET; "
              "$link.WorkingDirectory=Split-Path -LiteralPath $env:HUETUNE_SETUP_TARGET; $link.Save()")
    env = dict(os.environ, HUETUNE_SETUP_TARGET=str(target))
    subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-Command", script],
                   env=env, check=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)


def install_companion(manifest):
    target = APP_DIR / COMPANION
    previous = (APP_DIR / "status.json")
    last_status = previous.stat().st_mtime_ns if previous.exists() else 0
    temporary = target.with_suffix(".new")
    try:
        shutil.copyfile(ROOT / COMPANION, temporary)
        deadline = time.monotonic() + 8
        while True:
            try:
                os.replace(temporary, target)
                break
            except PermissionError:
                # The old one-file launcher can still hold the exe after its mutex closes.
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.2)
    finally:
        temporary.unlink(missing_ok=True)
    start_menu_shortcut(target)
    env = dict(os.environ, PYINSTALLER_RESET_ENVIRONMENT="1")
    subprocess.Popen([str(target)], cwd=APP_DIR, env=env)
    if not wait_for(30, lambda: status_report(last_status, manifest["firmware"], manifest["companion"])):
        raise RuntimeError("新版固件已启动，配套程序已安装，但尚未确认连接。请打开配套程序查看状态。")


def install(target, report):
    report("正在校验安装包…")
    manifest = verify_payload()
    fw = manifest["firmware"]
    if not re.fullmatch(r"[0-9a-f]{12}", identity(target)):
        raise RuntimeError("没有读到开发板的 USB 序列号。请重新连接开发板后重试。")
    stop_companion()
    current = ports()
    if len(current) != 1 or identity(current[0]) != identity(target) or current[0].pid != target.pid:
        raise RuntimeError("设备连接已变化。请只连接一块目标开发板后重试。")
    if target.pid == RUNTIME:
        report("正在连接 Huetune 固件…")
        runtime_version(target, install_version=fw)
        port = wait_for(12, lambda: same_board(target, ROM))
        if not port:
            raise RuntimeError("开发板未进入下载模式。请按住 BOOT、按一下 RESET、松开 BOOT，再重试。")
        files = ("0x10000", ROOT / "media_controller.bin")
    else:
        report("正在识别开发板…")
        try:
            reset = "no-reset" if target.pid == 0x0009 else "default-reset"
            info = esptool(target, "flash-id", before=reset, baud=115200, timeout=25)
        except RuntimeError as error:
            raise RuntimeError("无法进入下载模式。请按住 BOOT、按一下 RESET、松开 BOOT，再重试。") from error
        if not re.search(r"Detected flash size:\s*16\s*MB\b", info, re.IGNORECASE):
            raise RuntimeError("检测到的闪存容量不是 16 MB，未开始烧录。")
        port = wait_for(8, lambda: same_board(target, ROM))
        if not port:
            raise RuntimeError("开发板的下载接口已断开，未开始烧录。")
        report("正在备份板上的 16 MB 固件，请保持 USB 连接…")
        backup_flash(port)
        files = ("0x0", ROOT / "bootloader.bin", "0x8000", ROOT / "partition-table.bin",
                 "0x10000", ROOT / "media_controller.bin")

    report(f"正在烧录固件 {fw}，请保持 USB 连接…")
    esptool(port, "write-flash", "--flash-mode", "dio", "--flash-size", "16MB", "--flash-freq", "80m",
            *files)
    report("写入已校验，正在等待开发板重新启动…")
    try:
        esptool(port, "read-mac", after="watchdog-reset", baud=115200, timeout=20)
    except (RuntimeError, subprocess.TimeoutExpired):
        LOG.warning("Reset command lost the download port; checking the running firmware", exc_info=True)
    runtime = wait_for(15, lambda: same_board(target, (RUNTIME,)))
    if not runtime:
        raise RuntimeError("固件已写入并校验。请按一下 RESET，再运行安装器完成连接检查。")
    if runtime_version(runtime) != fw:
        raise RuntimeError("启动后回报的固件版本与安装包不符。请查看安装日志。")
    report(f"固件 {fw} 已启动，正在安装并连接电脑配套程序…")
    install_companion(manifest)
    return f"安装完成。固件 {fw} 和电脑配套程序 {manifest['companion']} 已连接。"


class SetupWindow:
    def __init__(self, root):
        self.root = root
        self.busy = False
        self.done = False
        self.events = queue.Queue()
        root.title("Huetune · 安装与烧录")
        root.minsize(560, 350)
        root.iconbitmap(str(ROOT / "controller.ico"))
        root.protocol("WM_DELETE_WINDOW", self.close)
        panel = ttk.Frame(root, padding=26)
        panel.pack(fill="both", expand=True)
        ttk.Label(panel, text="Huetune 安装与烧录", font=("Microsoft YaHei UI", 19, "bold")).pack(anchor="w")
        ttk.Label(panel, text="仅适用于微雪 ESP32-S3-Touch-LCD-3.5（16 MB）").pack(anchor="w", pady=(6, 15))
        self.device = ttk.Label(panel, wraplength=500, justify="left")
        self.device.pack(anchor="w")
        self.confirm = tk.BooleanVar()
        ttk.Checkbutton(panel, variable=self.confirm, text="我确认只连接了上面型号的开发板").pack(anchor="w", pady=(8, 6))
        self.status = ttk.Label(panel, wraplength=500, justify="left")
        self.status.pack(anchor="w", pady=(6, 10))
        self.progress = ttk.Progressbar(panel, mode="indeterminate")
        self.progress.pack(fill="x")
        actions = ttk.Frame(panel)
        actions.pack(fill="x", pady=(16, 0))
        ttk.Button(actions, text="安装日志", command=lambda: os.startfile(APP_DIR / "setup.log")).pack(side="left")
        ttk.Button(actions, text="备份文件", command=self.open_backups).pack(side="left", padx=6)
        self.button = ttk.Button(actions, text="一键安装并烧录", command=self.start)
        self.button.pack(side="right")
        self.poll()

    def open_backups(self):
        folder = APP_DIR / "backups"
        folder.mkdir(parents=True, exist_ok=True)
        os.startfile(folder)

    def close(self):
        if not self.busy:
            self.root.destroy()

    def poll(self):
        while not self.events.empty():
            kind, text = self.events.get_nowait()
            self.status["text"] = text
            if kind != "progress":
                self.busy = False
                self.done = kind == "success"
                self.progress.stop()
        if not self.busy and not self.done:
            candidates = ports()
            if len(candidates) == 1:
                target = candidates[0]
                mode = "已安装 Huetune · 更新保留设置" if target.pid == RUNTIME else "首次烧录或恢复 · 写入前会备份"
                message = f"已识别 {target.device}。{mode}。"
            elif candidates:
                message = "检测到多块设备，请只保留一块目标开发板。"
            else:
                message = "请用 USB 数据线连接开发板。若仍未识别，按住 BOOT、按一下 RESET，再松开 BOOT。"
            self.device["text"] = message
            self.button["state"] = "normal" if len(candidates) == 1 and self.confirm.get() else "disabled"
        elif self.done:
            self.button.configure(text="关闭", command=self.close, state="normal")
        self.root.after(300, self.poll)

    def start(self):
        candidates = ports()
        if len(candidates) != 1 or not self.confirm.get() or self.busy:
            return
        self.busy = True
        self.button["state"] = "disabled"
        self.progress.start(12)

        def run():
            try:
                result = install(candidates[0], lambda text: self.events.put(("progress", text)))
            except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
                LOG.exception("Installation failed")
                self.events.put(("failure", f"{error}\n可查看安装日志后重试。"))
            else:
                self.events.put(("success", result))

        threading.Thread(target=run, daemon=True).start()


def main():
    APP_DIR.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(filename=APP_DIR / "setup.log", encoding="utf-8", level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    ctypes.windll.kernel32.SetDllDirectoryW(None)
    if sys.argv[1:] == ["--verify-package"]:
        LOG.info("Package verified: %s", verify_payload())
        return
    if len(sys.argv) == 5 and sys.argv[1] == "--probe":
        _, _, device, version, result = sys.argv
        try:
            payload = {"version": serial_version(device, version if version != "-" else None)}
        except (OSError, RuntimeError, ValueError) as error:
            LOG.exception("USB probe failed")
            payload = {"error": str(error)}
        Path(result).write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
        return
    root = tk.Tk()
    create_mutex = instance.api("CreateMutexW", instance.wintypes.HANDLE, ctypes.c_void_p,
                                instance.wintypes.BOOL, instance.wintypes.LPCWSTR)
    handle = create_mutex(None, False, instance.MUTEX + ".Setup")
    already_open = ctypes.get_last_error() == 183
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    if already_open:
        instance.close(handle)
        root.destroy()
        return
    try:
        SetupWindow(root)
        root.mainloop()
    finally:
        instance.close(handle)


if __name__ == "__main__":
    main()
