import serial, time, sys

PORT = "/dev/cu.usbmodemflip_XXXX01"


def cmd(ser, c, wait=0.5):
    ser.write((c + "\r").encode())
    time.sleep(wait)
    data = ser.read(ser.in_waiting or 1)
    return data.decode(errors="replace")


ser = serial.Serial(PORT, 115200, timeout=0.1)
time.sleep(0.3)
ser.read(ser.in_waiting)

print("=== device_info ===")
print(cmd(ser, "device_info", 0.8))
print("=== power_info ===")
print(cmd(ser, "power_info", 0.6))
ser.close()
