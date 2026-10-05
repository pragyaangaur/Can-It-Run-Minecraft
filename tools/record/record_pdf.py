"""Records the scripted demo inside the PDF in Chromium's own PDF viewer.

The PDF's keyboard field takes "!" to start the demo and "." to step it.
Each step redraws synchronously, and the viewer is screenshotted once it
has repainted. Usage: record_pdf.py PDF_URL OUT_DIR [frames]
"""
import os, sys, time
from playwright.sync_api import sync_playwright

url, out = sys.argv[1], sys.argv[2]
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 751
os.makedirs(out, exist_ok=True)
with sync_playwright() as p:
    b = p.chromium.launch(channel="chromium", headless=True)
    pg = b.new_page(viewport={"width": 1920, "height": 1080})
    pg.goto(url)
    time.sleep(5)
    pg.screenshot(path=os.path.join(out, "layout.png"))
    # The keyboard field, for a 1920 by 1080 viewer at #view=FitH.
    kx, ky = int(os.environ.get("KEYS_X", 1760)), int(os.environ.get("KEYS_Y", 946))
    pg.mouse.click(kx, ky)
    time.sleep(0.3)
    pg.keyboard.type("!")
    pg.keyboard.press("Backspace")
    time.sleep(6)
    for i in range(frames):
        if i:
            # Chrome keeps typed characters in the field, so each one is
            # deleted again to leave the field's own text on screen.
            pg.keyboard.type(".")
            pg.keyboard.press("Backspace")
        time.sleep(0.25)
        pg.screenshot(path=os.path.join(out, "f%04d.png" % i))
        if i % 50 == 0:
            print(i, flush=True)
    b.close()
print("done")
