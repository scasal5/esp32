"""Physical UI runaway tests: restart only between cases, never for recovery.

Each case first presents the transition scene, then injects a guest fault and
requests a console report. The firmware verifies the framebuffer CRC is frozen.
"""
import argparse
import json
from pathlib import Path
import time
import serial
from flash_ota0 import read_mac


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    p.add_argument('--expect-mac',default='44:1B:F6:84:DA:88')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if read_mac(a.port)!=a.expect_mac.upper():raise SystemExit('Board identity mismatch')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    connection=serial.Serial();connection.port=a.port;connection.baudrate=115200
    connection.timeout=0.2;connection.dtr=False;connection.rts=False;connection.open()
    with a.out.open('wb') as output:
        def collect(seconds):
            end=time.monotonic()+seconds;data=bytearray()
            while time.monotonic()<end:
                chunk=connection.read(4096);output.write(chunk);data.extend(chunk)
            output.flush();return data.decode(errors='replace')
        try:
            for kind in ('eval','frame','promise'):
                connection.rts=True;time.sleep(0.2);connection.rts=False
                collect(3)
                connection.write(b'scenario 3\n');collect(1)
                connection.write(f'fault {kind}\n'.encode());log=collect(2)
                rows=[]
                for line in log.splitlines():
                    try:rows.append(json.loads(line))
                    except ValueError:pass
                if not any(r.get('test')=='freeze_'+kind and r.get('pass') is True for r in rows):
                    raise RuntimeError('Missing frozen framebuffer/contained fault: '+kind)
                connection.write(b'report\n');alive=collect(2)
                if '"type":"scenario"' not in alive:raise RuntimeError('Console unresponsive after '+kind)
                if any(word in log+alive for word in ('Guru Meditation','Task watchdog got triggered','Rebooting...')):
                    raise RuntimeError('Board reset/watchdog during '+kind)
            output.write(b'{"type":"invariant","test":"runaway","pass":true}\n')
        finally:connection.close()
    print('All three UI faults contained; console alive, framebuffer frozen. Board remains faulted.')


if __name__=='__main__':main()
