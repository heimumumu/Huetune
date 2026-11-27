import sys
from types import SimpleNamespace
import pytest

pytestmark=pytest.mark.skipif(sys.platform!='win32',reason='Windows audio COM adapter')


def test_endpoint_notification_accepts_installed_callback_signature(monkeypatch):
    from pycaw.pycaw import AudioUtilities
    from companion.windows import Volume
    class Endpoint:
        def RegisterControlChangeNotify(self,callback):self.callback=callback
        def UnregisterControlChangeNotify(self,callback):assert callback is self.callback
        def GetMasterVolumeLevelScalar(self):return .42
        def GetMute(self):return False
    endpoint=Endpoint()
    monkeypatch.setattr(AudioUtilities,'GetSpeakers',lambda:SimpleNamespace(id='headphones',FriendlyName='Headphones',EndpointVolume=endpoint))
    calls=[]
    volume=Volume(lambda:calls.append(True))
    try:
        endpoint.callback.on_notify(.42,False,None,2,[.42,.42])
        assert calls==[True]
        assert volume.read()==(42,0)
    finally:volume.close()


def test_default_endpoint_switch_rebinds_and_reads_current_device(monkeypatch):
    from pycaw.pycaw import AudioUtilities
    from companion.windows import Volume
    class Endpoint:
        def __init__(self,level,muted):self.level,self.muted=level,muted;self.registered=0;self.removed=0
        def RegisterControlChangeNotify(self,callback):self.registered+=1
        def UnregisterControlChangeNotify(self,callback):self.removed+=1
        def GetMasterVolumeLevelScalar(self):return self.level
        def GetMute(self):return self.muted
        def SetMute(self,value,context):self.muted=value
    speakers=Endpoint(0,True);headphones=Endpoint(.10,False)
    selected=[SimpleNamespace(id='speakers',FriendlyName='Speakers',EndpointVolume=speakers)]
    monkeypatch.setattr(AudioUtilities,'GetSpeakers',lambda:selected[0])
    v=Volume(lambda:None)
    try:
        assert v.read()==(0,1)
        selected[0]=SimpleNamespace(id='headphones',FriendlyName='Headphones',EndpointVolume=headphones)
        assert v.read()==(10,0)
        assert v.device_id=='headphones'
        assert speakers.removed==1 and headphones.registered==1
        v.set_mute(True)
        assert headphones.muted is True
        assert v.read()==(10,1)
    finally:v.close()
