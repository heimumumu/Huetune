"""Protocol v1: bounded, resynchronizing CDC framing and blob encoding."""
import binascii
import json
import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum

MAX_PAYLOAD = 16384
MAGIC = b"\xa5\x5a"


class Type(IntEnum):
    HELLO_ACK = 0x01
    PING = 0x02
    GET_STATUS = 0x03
    ENTER_BOOTLOADER = 0x04
    TRACK = 0x10
    PLAYBACK = 0x11
    VOLUME = 0x12
    PALETTE = 0x13
    SYSINFO = 0x14
    TIME = 0x15
    BLOB_BEGIN = 0x20
    BLOB_CHUNK = 0x21
    BLOB_END = 0x22
    HELLO = 0x81
    DEVICE_PING = 0x82
    DEVICE_STATUS = 0x83
    SET_VOLUME = 0x90
    SET_MUTE = 0x91
    REQ_COVER = 0x92
    SETTINGS = 0x93


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def tag(track_id):
    return zlib.crc32(track_id.encode("utf-8"))


def encode(kind, seq, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 16 KiB")
    body = struct.pack("<BBI", kind, seq & 255, len(payload)) + payload
    return MAGIC + body + struct.pack("<H", binascii.crc_hqx(body, 0xffff))


class Parser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1:] == MAGIC[:1] else b""
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 8:
                break
            kind, seq, length = struct.unpack_from("<BBI", self.buffer, 2)
            if length > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            end = 8 + length
            if len(self.buffer) < end + 2:
                break
            if binascii.crc_hqx(self.buffer[2:end], 0xffff) != struct.unpack_from("<H", self.buffer, end)[0]:
                del self.buffer[0]
                continue
            frames.append((kind, seq, bytes(self.buffer[8:end])))
            del self.buffer[:end + 2]
        return frames


@dataclass(frozen=True)
class Blob:
    id: int
    width: int
    height: int
    format: int
    flags: int
    data: bytes

    def messages(self, track_tag):
        yield Type.BLOB_BEGIN, struct.pack("<BIHHBBI", self.id, track_tag, self.width, self.height,
                                          self.format, self.flags, len(self.data))
        for offset in range(0, len(self.data), MAX_PAYLOAD - 5):
            yield Type.BLOB_CHUNK, struct.pack("<BI", self.id, offset) + self.data[offset:offset + MAX_PAYLOAD - 5]
        yield Type.BLOB_END, struct.pack("<BI", self.id, zlib.crc32(self.data))
