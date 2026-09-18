"""Collect buffered harness metrics, without logging credentials or per-frame output."""
import argparse
from pathlib import Path
import serial
import time


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--port',default='COM3')
    p.add_argument('--seconds',type=float,default=30)
    p.add_argument('--command',choices=['smoke','soak','report'])
    p.add_argument('--reset',action='store_true',help='Reset the USB-JTAG board after opening the capture port')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    a.out.parent.mkdir(parents=True,exist_ok=True)
    connection=serial.Serial();connection.port=a.port;connection.baudrate=115200;connection.timeout=0.5
    connection.dtr=False;connection.rts=False;connection.open()
    try:
        if a.reset:
            connection.rts=True
            time.sleep(0.2)
            connection.rts=False
            time.sleep(0.2)
        if a.command:connection.write((a.command+'\n').encode())
        end=time.monotonic()+a.seconds
        with a.out.open('wb') as output:
            while time.monotonic()<end:
                data=connection.read(4096)
                if data:output.write(data);output.flush()
    finally:connection.close()
    print('Captured',a.out.stat().st_size,'bytes in',a.out)


if __name__=='__main__':main()
