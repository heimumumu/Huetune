"""Exercise real CDC heartbeat and CRC-checked image transfers; no media keys."""
import argparse
import json
from pathlib import Path
import struct
import time
from collections import deque
import serial
from companion.protocol import Blob, Parser, Type, encode, json_bytes
from companion.artwork import compose

args_parser=argparse.ArgumentParser()
args_parser.add_argument('--port',required=True)
args_parser.add_argument('--seconds',type=int,default=35)
args_parser.add_argument('--output',default='.vendor/device-check.json')
args=args_parser.parse_args()
result={'status':[], 'resync_hello':0, 'requests':0, 'bytes_sent':0}
pending=deque()
parser=Parser()
seq=0
ready=False
bulk_started=False
connected_at=None
bulk_ended=None
last_ping=0

with serial.Serial(args.port,115200,timeout=.01,write_timeout=.5) as port:
    port.dtr=True
    def send(kind,payload=b''):
        global seq
        data=encode(kind,seq,payload)
        if port.write(data)!=len(data):raise RuntimeError('short serial write')
        result['bytes_sent']+=len(data)
        seq=(seq+1)&255
    started=time.monotonic()
    while time.monotonic()-started<args.seconds:
        now=time.monotonic()
        for kind,_,data in parser.feed(port.read(max(1,min(65536,port.in_waiting)))):
            if kind==Type.HELLO:
                if ready:result['resync_hello']+=1
                send(Type.HELLO_ACK,json_bytes({'proto':1,'app':'device-check'}))
            elif kind==Type.SETTINGS:
                if not ready:
                    ready=True;connected_at=time.monotonic()
                    print('Device confirmed handshake',flush=True)
            elif kind==Type.REQ_COVER:
                result['requests']+=1
            elif kind==Type.DEVICE_STATUS and ready:
                status=json.loads(data)
                result['status'].append(status)
                if not bulk_started:
                    if status['track']:raise RuntimeError('Run this check before the media companion sends a track')
                    palette,images=compose(None)
                    pending.append((Type.PALETTE,bytes([palette['dark'],*palette['tint'],round(palette['a']*100),round(palette['as']*100),round(palette['ac']*100)])))
                    texts=[Blob(i,w,36 if i in (10,11) else 24,2,0,bytes(w*(36 if i in (10,11) else 24)//2)) for i,w in [(10,226),(11,190),(12,226),(13,190),(20,170),(21,170),(22,170),(23,170)]]
                    for blob in texts+images:pending.extend(blob.messages(0))
                    bulk_started=True
                if len(result['status'])%10==0:
                    print('Health samples:',len(result['status']),'linked:',status['linked'],'image_failures:',status['image_failures'],flush=True)
        if now-last_ping>=.8:
            send(Type.PING);send(Type.GET_STATUS);last_ping=now
        if pending:
            kind,payload=pending.popleft();send(kind,payload)
            if not pending:
                bulk_ended=time.monotonic()
                print('All image/text blobs sent',flush=True)

result['elapsed_seconds']=time.monotonic()-started
result['bulk_seconds']=bulk_ended-connected_at if bulk_ended else None
Path(args.output).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
assert ready and bulk_ended is not None,'No completed handshake/image transfer'
assert len(result['status'])>=20,'Too few health samples'
assert all(s['linked'] for s in result['status']),'Device lost companion connection'
assert result['resync_hello']==0,'Device restarted handshake after connecting'
assert result['requests']==0,'Device requested image retransmission'
assert all(s['image_failures']==0 for s in result['status']),'Image CRC/timeout failure'
assert all(a['uptime_ms']<b['uptime_ms'] for a,b in zip(result['status'],result['status'][1:])),'Device rebooted'
assert all(s['rx_age_ms']<2000 for s in result['status']),'Delayed heartbeat'
print('PASS:',len(result['status']),'continuous device-confirmed samples;',result['bytes_sent'],'bytes sent; no reconnect/image errors',flush=True)
