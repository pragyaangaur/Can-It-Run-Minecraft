"""Connects to rd-term --serve, sends input and rebuilds the half-block
screen it receives into a PPM, so the server can be checked headless.
Usage: telnet_probe.py PORT OUT.ppm [keys]"""
import re, socket, sys, time

port, out = int(sys.argv[1]), sys.argv[2]
keys = sys.argv[3].encode() if len(sys.argv) > 3 else b""
W, H = 100, 40
s = socket.create_connection(("127.0.0.1", port))
s.sendall(bytes([255, 250, 31, 0, W, 0, H, 255, 240]))
data = b""
end = time.time() + 2.5
sent = False
s.settimeout(0.05)
while time.time() < end:
    if not sent and time.time() > end - 1.5:
        s.sendall(keys)
        sent = True
    try:
        chunk = s.recv(1 << 20)
        if not chunk:
            break
        data += chunk
    except socket.timeout:
        pass
s.close()
text = data.decode("utf-8", "replace")
grid = [[((0, 0, 0), (0, 0, 0)) for _ in range(W)] for _ in range(H)]
x = y = 0
fg = bg = (0, 0, 0)
for m in re.finditer(r"\x1b\[([0-9;?<>]*)([A-Za-z])|(▀)|([^\x1b])", text):
    if m.group(3):
        if 0 <= y < H and 0 <= x < W:
            grid[y][x] = (fg, bg)
        x += 1
    elif m.group(2) == "H":
        p = m.group(1).split(";")
        y, x = int(p[0]) - 1, int(p[1]) - 1
    elif m.group(2) == "m":
        p = m.group(1).split(";")
        if p[:2] == ["38", "2"]:
            fg = tuple(map(int, p[2:5]))
        elif p[:2] == ["48", "2"]:
            bg = tuple(map(int, p[2:5]))
    elif m.group(4):
        x += 1
with open(out, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (W, (H - 1) * 2))
    for row in grid[: H - 1]:
        for half in (0, 1):
            f.write(bytes(c for cell in row for c in cell[half]))
status = re.findall(r"rdgeneric[^\x1b]*", text)
print("bytes received:", len(data), "| last status:", status[-1].strip() if status else None)
