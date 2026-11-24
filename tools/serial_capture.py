#!/usr/bin/env python3
"""Capture UART0 logs, optionally resetting the development board via RTS."""
import argparse
from pathlib import Path
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--duration", type=float, default=35)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    log = bytearray()
    try:
        port.open()
        if args.reset:
            port.rts = True
            time.sleep(0.1)
            port.rts = False
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                log.extend(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    finally:
        port.close()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(log)


if __name__ == "__main__":
    main()
