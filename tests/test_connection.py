import queue
import threading
import time
from types import SimpleNamespace
from companion.app import Link
from companion.protocol import Parser, Type, encode, json_bytes


def test_handshake_and_control_priority(monkeypatch):
    received=[]
    ready=threading.Event()

    class Port:
        def __init__(self,*args,**kwargs):
            self.pending=bytearray(encode(Type.HELLO,0,json_bytes({"proto":1,"fw":"test","w":480,"h":320})))
            self.parser=Parser()
            self.dtr=False
        def __enter__(self):return self
        def __exit__(self,*_):return False
        @property
        def in_waiting(self):return len(self.pending)
        def read(self,n):
            time.sleep(.001)
            data=bytes(self.pending[:n]);del self.pending[:n];return data
        def write(self,wire):
            for kind,seq,data in self.parser.feed(wire):
                received.append((kind,seq,data))
                if kind==Type.HELLO_ACK:
                    self.pending.extend(encode(Type.SETTINGS,1,json_bytes({"bg":"blur"})))
                    ready.set()
            return len(wire)

    monkeypatch.setattr('companion.app.serial.Serial',Port)
    monkeypatch.setattr('companion.app.list_ports.comports',lambda:[SimpleNamespace(device="COM99",vid=0x303a,pid=0x4001)])
    link=Link(queue.Queue(),lambda _:None)
    link.send(Type.VOLUME,b'\x32\0')
    link.publish(0,[(Type.BLOB_CHUNK,b'\x01'+b'\0'*4+b'x'*100)]*10)
    link.thread.start()
    try:
        assert ready.wait(2)
        deadline=time.monotonic()+1
        while not link.connected.is_set() and time.monotonic()<deadline:time.sleep(.01)
        assert link.connected.is_set()
        deadline=time.monotonic()+1
        while len(received)<6 and time.monotonic()<deadline:time.sleep(.01)
    finally:
        link.stop.set();link.thread.join(2)
    kinds=[frame[0] for frame in received]
    assert kinds.index(Type.VOLUME)<kinds.index(Type.BLOB_CHUNK)
    assert Type.PING in kinds
    assert [frame[1] for frame in received]==list(range(len(received)))


def test_outdated_render_is_never_published():
    link=Link(queue.Queue(),lambda _:None)
    link.generation=2
    link.publish(1,[(Type.TRACK,b'stale')])
    assert link.art.empty()
    link.publish(2,[(Type.TRACK,b'current')])
    generation,messages=link.art.get_nowait()
    assert generation==2 and list(messages)==[(Type.TRACK,b'current')]
