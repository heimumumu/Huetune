import binascii
import random
import struct
import zlib
import pytest
from companion.protocol import Blob, Parser, Type, encode, tag


def test_crc_reference_and_fragmentation():
    assert binascii.crc_hqx(b"123456789", 0xffff) == 0x29b1
    data = bytes(range(256))*64
    wire = encode(Type.BLOB_CHUNK, 255, data)
    parser = Parser()
    result = []
    for byte in wire:
        result.extend(parser.feed(bytes([byte])))
    assert result == [(Type.BLOB_CHUNK, 255, data)]


def test_resync_noise_bad_crc_and_oversized_frame():
    wire = bytearray(encode(Type.TRACK, 1, b'{"id":"old"}'))
    wire[-1] ^= 1
    noisy = b"noise\xa5" + bytes(wire) + b"\xa5\x5a\x01\x02\xff\xff\xff\xff" + encode(Type.PING, 2)
    p = Parser()
    assert p.feed(noisy) == [(Type.PING, 2, b"")]
    with pytest.raises(ValueError):
        encode(1, 0, b"x"*16385)


def test_blob_roundtrip_and_track_tag():
    data = random.Random(123).randbytes(480*320*2)
    blob = Blob(1, 480, 320, 1, 0, data)
    parts = list(blob.messages(tag("曲目一")))
    assert struct.unpack("<BIHHBBI", parts[0][1]) == (1, zlib.crc32("曲目一".encode()), 480, 320, 1, 0, len(data))
    out = bytearray()
    for kind, payload in parts[1:-1]:
        assert kind == Type.BLOB_CHUNK and len(payload) <= 16384
        assert struct.unpack_from("<I", payload, 1)[0] == len(out)
        out.extend(payload[5:])
    assert out == data
    assert struct.unpack("<BI", parts[-1][1]) == (1, zlib.crc32(out))
