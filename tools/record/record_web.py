"""Records the scripted demo on the web build in headless Chromium.

The page, opened with #demo, exposes rdDemo(ticks), which steps the walk,
redraws the canvas and returns the favicon view. Usage:
record_web.py PAGE_URL OUT_DIR [frames]
"""
import base64, os, sys
from playwright.sync_api import sync_playwright

url, out = sys.argv[1], sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 751
os.makedirs(out, exist_ok=True)
with sync_playwright() as p:
    b = p.chromium.launch(channel="chromium", headless=True)
    pg = b.new_page(viewport={"width": 1152, "height": 720})
    pg.goto(url)
    pg.wait_for_function("window.rdDemo !== undefined")
    for i in range(frames):
        r = pg.evaluate("t => window.rdDemo(t)", 0 if i == 0 else 2)
        pg.screenshot(path=os.path.join(out, "f%04d.png" % i))
        icon = base64.b64decode(r["favicon"].split(",", 1)[1])
        open(os.path.join(out, "icon%04d.png" % i), "wb").write(icon)
        if i % 100 == 0:
            print(i, r["tick"], flush=True)
    b.close()
print("done")
