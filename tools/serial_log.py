#!/usr/bin/env python3
"""Records the device's serial output to a file (and the screen): a scriptable replacement for
`pio device monitor`, which needs an interactive terminal.

    python3 tools/serial_log.py                 (the first /dev/cu.usbmodem*, log in serial.log)
    python3 tools/serial_log.py PORT OUT.log    (Ctrl+C stops)

Needs pyserial: python3 -m pip install --user pyserial
"""

import glob
import sys
import time

import serial


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), "")
    out = sys.argv[2] if len(sys.argv) > 2 else "serial.log"
    if not port:
        print("No /dev/cu.usbmodem* port: connect the device or pass the port", file=sys.stderr)
        return 1
    print(f"Logging {port} -> {out} (Ctrl+C stops)")
    with open(out, "ab") as log:
        while True:
            try:
                with serial.Serial(port, 115200, timeout=0.5) as dev:
                    while True:
                        data = dev.readline()
                        if data:
                            log.write(data)
                            log.flush()
                            sys.stdout.write(data.decode("utf-8", "replace"))
                            sys.stdout.flush()
            except serial.SerialException:
                time.sleep(0.5)  # the port goes away while the device restarts: wait for it
            except KeyboardInterrupt:
                return 0


if __name__ == "__main__":
    sys.exit(main())
