"""Records the scripted demo on the bare-metal build in QEMU.

Each "." key press steps the kernel's demo by two ticks. The kernel answers
on the serial port once the frame is on screen, then the monitor takes a
screendump. Usage: record_x86.py OUT_DIR [frames] [--no-dumps]
With --no-dumps only the serial trace is kept, which the tests use.
"""
import os, socket, subprocess, sys, time

out = sys.argv[1]
args = [a for a in sys.argv[2:] if a != "--no-dumps"]
dumps = "--no-dumps" not in sys.argv
frames = int(args[0]) if args else 751
os.makedirs(out, exist_ok=True)
qemu = subprocess.Popen([
    "qemu-system-i386", "-cpu", "486", "-m", "64", "-kernel", "build/rdgeneric-x86.elf",
    "-display", "none", "-monitor", "tcp:127.0.0.1:45550,server,nowait",
    "-serial", "tcp:127.0.0.1:45551,server,nowait"])
time.sleep(1)
mon = socket.create_connection(("127.0.0.1", 45550))
ser = socket.create_connection(("127.0.0.1", 45551))
ser.settimeout(120)
buf = b""


def serial_line():
    global buf
    while b"\n" not in buf:
        buf += ser.recv(4096)
    line, buf = buf.split(b"\n", 1)
    return line.decode().strip()


def monitor(cmd):
    mon.sendall((cmd + "\n").encode())
    data = b""
    mon.settimeout(30)
    while not data.rstrip().endswith(b"(qemu)"):
        data += mon.recv(4096)


monitor("info status")
while "fps" not in serial_line():  # booted and running once fps is reported
    pass
trace = open(os.path.join(out, "trace.txt"), "w")
for i in range(frames):
    monitor("sendkey dot")
    while True:
        line = serial_line()
        if line.startswith("F "):
            break
    trace.write(line + "\n")
    if dumps:
        monitor(f"screendump {os.path.join(out, 'f%04d.ppm' % i)}")
    if i % 50 == 0:
        print(i, line, flush=True)
qemu.kill()
print("done")
