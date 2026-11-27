"""Enter download mode using firmware >=0.2.3; close the companion first."""
import argparse
import queue
import time

from serial.tools import list_ports

from companion.app import Link
from companion.protocol import Type


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--port")
    port = args.parse_args().port
    link = Link(queue.Queue(), lambda _: None, port)
    link.thread.start()
    try:
        until = time.monotonic()+5
        while time.monotonic() < until:
            if link.connected.is_set() and link.last_device_status.get("fw"):
                break
            time.sleep(.05)
        else:
            raise RuntimeError("Cannot connect; close the running companion first")
        version = tuple(int(v) for v in link.last_device_status["fw"].split("."))
        if version < (0, 2, 3):
            raise RuntimeError("This firmware still needs BOOT + RESET")
        link.send(Type.ENTER_BOOTLOADER, b"BOOT")
        until = time.monotonic()+5
        while time.monotonic() < until:
            ports = [p.device for p in list_ports.comports() if p.vid == 0x303a and p.pid in (0x1001, 0x0009)]
            if ports:
                print("Download mode:", ", ".join(ports))
                return
            time.sleep(.1)
        raise RuntimeError("Download interface did not appear")
    finally:
        link.stop.set()
        link.thread.join(2)


if __name__ == "__main__":
    main()
