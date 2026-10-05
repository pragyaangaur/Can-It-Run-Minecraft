import sys, time
from playwright.sync_api import sync_playwright
url, out = sys.argv[1], sys.argv[2]
keys = sys.argv[3] if len(sys.argv) > 3 else ""
with sync_playwright() as p:
    b = p.chromium.launch(channel="chromium", headless=True)
    pg = b.new_page(viewport={"width": 1100, "height": 900})
    pg.goto(url)
    time.sleep(4)
    pg.screenshot(path=out + "-0.png")
    if keys:
        # Click the keyboard field, then type.
        pg.mouse.click(int(sys.argv[4]), int(sys.argv[5]))
        time.sleep(0.5)
        for ch in keys:
            pg.keyboard.press(ch if ch != " " else "Space")
            time.sleep(0.05)
        time.sleep(1.5)
        pg.screenshot(path=out + "-1.png")
    b.close()
