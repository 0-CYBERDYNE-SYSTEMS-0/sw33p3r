import serial, time, subprocess, sys

PORT = "/dev/cu.usbmodemflip_Rug1k01"
APP_DIR = "/Users/scrimwiggins/flipper-room-sweep"
UFBT = "/private/tmp/flipper-dev/bin/ufbt"


def safe_cmd(ser, cmd, timeout_s=3.0):
    ser.reset_input_buffer()
    ser.write(cmd.encode() + b"\r\n")
    out = b""
    start = time.time()
    quiet = 0
    while time.time() - start < timeout_s:
        chunk = ser.read(ser.in_waiting or 0)
        if chunk:
            out += chunk
            quiet = 0
        else:
            quiet += 1
            if quiet > 5:
                break
        time.sleep(0.15)
    return out.decode("utf-8", errors="replace")


def open_with_retry(timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = serial.Serial(PORT, 115200, timeout=2)
            time.sleep(0.4)
            s.reset_input_buffer()
            return s
        except serial.SerialException:
            time.sleep(2)
    return None


# Launch the app (ufbt install + launch)
print("=== ufbt launch room_sweep ===")
r = subprocess.run([UFBT, "launch"], cwd=APP_DIR,
                   capture_output=True, text=True, timeout=120)
print(r.stdout[-500:])
if r.returncode != 0:
    print("STDERR:", r.stderr[-400:])
    sys.exit(1)

# Let the app initialize (RF sweep thread starts, GUI mounts)
print("Waiting 6s for app to initialize...")
time.sleep(6)

# Open CLI and check the app is still alive
ser = open_with_retry(20)
if not ser:
    print("ERROR: could not open serial")
    sys.exit(2)

# loader info tells us the running app
info = safe_cmd(ser, "loader info", 2.0)
print("\n=== loader info ===")
print(info)

# If the app crashed, loader info would show the desktop/launcher instead
if "room_sweep" in info.lower() or "Room Sweep" in info:
    print("\n*** APP ALIVE — no hard-fault crash ***")
else:
    print("\n!!! APP MAY HAVE CRASHED — loader info does not show room_sweep !!!")

# Also grab device uptime / free memory for good measure
print("\n=== device info (uptime/mem) ===")
print(safe_cmd(ser, "device info", 2.0))

ser.close()
