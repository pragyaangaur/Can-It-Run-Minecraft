"""Records the scripted demo on the terminal build.

rd-term --demo runs in a real pseudo-terminal, 160 columns by 51 rows. Each
"." key steps the walk and the program redraws. The bytes it writes are
replayed into xterm.js in headless Chromium, which is screenshotted after
every frame. Usage: record_term.py OUT_DIR [frames]
"""
import base64, fcntl, os, pty, select, struct, sys, termios, time
from playwright.sync_api import sync_playwright

out = sys.argv[1]
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 751
os.makedirs(out, exist_ok=True)
COLS, ROWS = 160, 51

pid, fd = pty.fork()
if pid == 0:
    # Set the size before the program starts, so its first read sees it.
    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    os.environ["COLORTERM"] = "truecolor"
    os.execv("./rd-term", ["rd-term", "--demo"])


def read_frame(n):
    """Reads until the title change that marks the end of frame n."""
    data, mark = b"", b"\x1b]0;rdgeneric frame %d\x07" % n
    while mark not in data:
        r, _, _ = select.select([fd], [], [], 10)
        if not r:
            raise SystemExit("rd-term stopped answering at frame %d" % n)
        data += os.read(fd, 1 << 20)
    return data


chunks = [read_frame(0)]
for i in range(1, frames):
    os.write(fd, b".")
    chunks.append(read_frame(i))
    if i % 100 == 0:
        print("captured", i, flush=True)
os.write(fd, b"q")
# Keep reading so the program can write its exit sequence and quit.
try:
    while os.read(fd, 1 << 16):
        pass
except OSError:
    pass
os.waitpid(pid, 0)

PAGE = """<!doctype html><html><head>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/css/xterm.min.css">
<script src="https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/@xterm/addon-canvas@0.7.0/lib/addon-canvas.min.js"></script>
<style>body{margin:0;background:#1d1f21}#t{display:inline-block}</style></head>
<body><div id="t"></div><script>
const term = new Terminal({cols: %d, rows: %d, fontSize: 14, lineHeight: 1, letterSpacing: 0,
  fontFamily: 'Menlo, monospace', customGlyphs: true,
  theme: {background: '#1d1f21', foreground: '#c5c8c6'}});
term.loadAddon(new CanvasAddon.CanvasAddon());
term.open(document.getElementById('t'));
window.feed = s => new Promise(r => term.write(Uint8Array.from(atob(s), c => c.charCodeAt(0)), () => requestAnimationFrame(() => requestAnimationFrame(r))));
</script></body></html>""" % (COLS, ROWS)
open(os.path.join(out, "term.html"), "w").write(PAGE)

with sync_playwright() as p:
    b = p.chromium.launch(channel="chromium", headless=True)
    pg = b.new_page(viewport={"width": 1600, "height": 1000})
    pg.goto("file://" + os.path.abspath(os.path.join(out, "term.html")))
    pg.wait_for_function("window.feed !== undefined")
    el = pg.query_selector(".xterm-screen")
    for i, c in enumerate(chunks):
        pg.evaluate("s => window.feed(s)", base64.b64encode(c).decode())
        el.screenshot(path=os.path.join(out, "f%04d.png" % i))
        if i % 100 == 0:
            print(i, flush=True)
    b.close()
print("done")
