#!/usr/bin/env python3
"""Phase 0 probe battery: interrogate the BFFB ESP32 Marauder over the
Flipper GPIO app's USB-UART bridge (host-side, receive-only).

Run AFTER the bridge is started on the Flipper (GPIO app -> USB-UART Bridge;
it must be started on-device with no host session connected, then the USB
re-enumerates and a second CDC interface appears).

Every step is bounded, RX-only, and followed by `stopscan`. Attack commands
present in the firmware (evilportal, blespam, btwardrive, sniffskim) are never
sent. Transcript goes to .omo/evidence/marauder-probe-<ts>.txt.
"""
import argparse
import glob
import time
from pathlib import Path

import serial

OUT = Path(".omo/evidence")
# (label, command, seconds) — sniff durations are deliberately short.
BATTERY = [
    ("settings", "settings", 2.0),
    ("sniffbeacon", "sniffbeacon", 3.0),
    ("sniffprobe", "sniffprobe", 6.0),
    ("scansta", "scansta", 5.0),
    ("list -s", "list -s", 1.5),
    ("scanap", "scanap", 4.0),
    ("list -a", "list -a", 1.5),
    ("sniffesp", "sniffesp", 4.0),
    ("sniffraw", "sniffraw", 2.0),
    ("sniffbt", "sniffbt", 4.0),
    ("sniffbt -t airtag", "sniffbt -t airtag", 4.0),
    ("sniffbt -t flipper", "sniffbt -t flipper", 3.0),
    ("sniffbt -t flock", "sniffbt -t flock", 3.0),
    ("sniffbt -t meta", "sniffbt -t meta", 3.0),
    ("sigmon", "sigmon", 3.0),
    ("sniffpwn", "sniffpwn", 2.0),
]


def list_marauder_ports():
    return sorted(glob.glob("/dev/cu.usbmodem*"))


def looks_like_marauder(port):
    try:
        s = serial.Serial(port, 115200, timeout=1)
    except Exception:
        return False
    try:
        s.reset_input_buffer()
        s.write(b"\r\n")
        time.sleep(0.6)
        data = s.read(1024).decode("utf-8", "replace")
        return data.strip().endswith(">") and "Flipper" not in data
    finally:
        s.close()


def run(port, transcript):
    s = serial.Serial(port, 115200, timeout=1)
    try:
        s.write(b"stopscan\r\n")
        time.sleep(1.0)
        for label, cmd, seconds in BATTERY:
            transcript.write(f"\n########## [{label}] $ {cmd} ({seconds}s) ##########\n")
            s.reset_input_buffer()
            s.write(cmd.encode() + b"\r\n")
            deadline = time.time() + seconds
            while time.time() < deadline:
                chunk = s.read(4096)
                if chunk:
                    text = chunk.decode("utf-8", "replace")
                    transcript.write(text)
                    transcript.flush()
            s.write(b"stopscan\r\n")
            time.sleep(0.8)
            tail = s.read(4096).decode("utf-8", "replace")
            transcript.write(tail)
            transcript.flush()
        transcript.write("\n########## cleanup ##########\n")
        s.write(b"clearlist -a\r\n")
        time.sleep(0.5)
        s.write(b"clearlist -s\r\n")
        time.sleep(0.5)
        s.write(b"stopscan\r\n")
        time.sleep(0.5)
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="bridge CDC port (auto-detects if omitted)")
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"marauder-probe-{time.strftime('%Y-%m-%d-%H%M%S')}.txt"

    port = args.port
    if not port:
        candidates = list_marauder_ports()
        print("usbmodem ports:", candidates)
        for c in candidates:
            if looks_like_marauder(c):
                port = c
                print("Marauder prompt found on:", c)
                break
    if not port:
        raise SystemExit(
            "No Marauder prompt found. Start the USB-UART Bridge on the "
            "Flipper (GPIO app), wait for USB to re-enumerate, retry."
        )

    with open(path, "w") as t:
        t.write(f"# marauder probe battery {time.strftime('%Y-%m-%d %H:%M:%S')} on {port}\n")
        run(port, t)
    print("transcript:", path)


if __name__ == "__main__":
    main()
