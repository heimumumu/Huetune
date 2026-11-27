"""Read-only smoke check; never changes volume or sends media keys."""
import asyncio
from companion.windows import Media, Volume, Telemetry


async def main():
    v=Volume(lambda:None)
    t=Telemetry()
    try:
        print("Volume:",v.read())
        print("Telemetry:",t.read())
        media=Media()
        await media.start()
        track,playback=await media.read()
        print("SMTC session:", bool(media.session), "Playback:",playback)
        print("Track present:",bool(track))
    finally:
        v.close()
        t.close()


asyncio.run(main())
