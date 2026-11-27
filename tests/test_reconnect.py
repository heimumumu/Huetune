import queue
import threading
import time
from types import SimpleNamespace
from companion.app import Link
from companion.protocol import Parser,Type,encode,json_bytes


def test_reconnect_to_device_still_linked_to_prior_process(monkeypatch):
    confirmed=threading.Event()
    received=[]
    class Port:
        def __init__(self,*args,**kwargs):self.pending=bytearray();self.parser=Parser();self.dtr=False
        def __enter__(self):return self
        def __exit__(self,*args):return False
        @property
        def in_waiting(self):return len(self.pending)
        def read(self,n):
            time.sleep(.001)
            data=bytes(self.pending[:n]);del self.pending[:n];return data
        def write(self,data):
            for kind,seq,payload in self.parser.feed(data):
                received.append(kind)
                if kind==Type.GET_STATUS:self.pending.extend(encode(Type.DEVICE_STATUS,0,json_bytes({'fw':'0.2.1','linked':True})))
                elif kind==Type.HELLO_ACK:
                    self.pending.extend(encode(Type.SETTINGS,1,json_bytes({'bg':'blur'})))
                    confirmed.set()
            return len(data)
    monkeypatch.setattr('companion.app.serial.Serial',Port)
    monkeypatch.setattr('companion.app.list_ports.comports',lambda:[SimpleNamespace(device='COM99',vid=0x303a,pid=0x4001)])
    link=Link(queue.Queue(),lambda _:None)
    link.thread.start()
    try:
        assert confirmed.wait(2),'Device never sends HELLO while its prior session is still alive'
        until=time.monotonic()+1
        while not link.connected.is_set() and time.monotonic()<until:time.sleep(.01)
        assert link.connected.is_set()
        assert received[0]==Type.GET_STATUS
        assert received.index(Type.GET_STATUS)<received.index(Type.HELLO_ACK)
    finally:link.stop.set();link.thread.join(2)
