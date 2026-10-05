"""Builds the demo video from the four recordings.

Every recording shows the same scripted walk, frame for frame. The game
picture stays in one place on a 1920 by 1080 stage while the thing it is
running in changes around it: a browser, a terminal, Chrome's PDF viewer
and a CRT for the bare-metal build. It ends by pulling back to all four
playing in step.

Usage: composite.py REC_DIR OUT.mp4
REC_DIR holds web/, term/, pdf/ and x86/ from the record_*.py scripts.
"""
import subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

rec, out = sys.argv[1], sys.argv[2]
W, H = 1920, 1080
GX, GY, GW, GH = 384, 196, 1152, 720  # where the game picture sits
FRAMES = 751
SS = 2  # supersampling for the drawn chrome


def backdrop():
    # Flat, so the four scaled-down stages in the closing grid sit on one
    # continuous background with no visible panel edges.
    return Image.new("RGB", (W, H), (22, 25, 30))


BACK = backdrop()


def layer():
    return Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))


def s(*v):
    return [int(a * SS) for a in v]


def shadow_for(box, radius, strength=150, blur=40):
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(sh)
    x0, y0, x1, y1 = box
    d.rounded_rectangle((x0, y0 + 18, x1, y1 + 18), radius, fill=(0, 0, 0, strength))
    return sh.filter(ImageFilter.GaussianBlur(blur))


def finish(lay):
    return lay.resize((W, H), Image.LANCZOS)


def lights(d, x, y):
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        cx = x + i * 20
        d.ellipse(s(cx - 6, y - 6, cx + 6, y + 6), fill=c)


# ---- browser ----
BW0, BW1, BTOP = GX, GX + GW, GY - 92


def browser_chrome():
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rounded_rectangle(s(BW0, BTOP, BW1, GY + GH), 12 * SS, fill=(32, 33, 36))
    lights(d, BW0 + 22, BTOP + 22)
    d.rounded_rectangle(s(BW0 + 90, BTOP + 8, BW0 + 330, BTOP + 50), 10 * SS, fill=(53, 54, 58))
    d.rectangle(s(BW0, BTOP + 42, BW1, GY), fill=(53, 54, 58))
    d.rounded_rectangle(s(BW0 + 136, BTOP + 18, BW0 + 290, BTOP + 30), 6 * SS, fill=(90, 92, 98))
    d.rounded_rectangle(s(BW0 + 150, BTOP + 52, BW1 - 30, BTOP + 84), 16 * SS, fill=(32, 33, 36))
    for i in range(3):  # back, forward, reload
        cx = BW0 + 30 + i * 36
        d.ellipse(s(cx - 9, BTOP + 59, cx + 9, BTOP + 77), outline=(140, 142, 148), width=2 * SS)
    d.rounded_rectangle(s(BW0 + 168, BTOP + 63, BW0 + 180, BTOP + 74), 2 * SS, fill=(140, 142, 148))
    d.arc(s(BW0 + 169, BTOP + 56, BW0 + 179, BTOP + 68), 180, 360, fill=(140, 142, 148), width=2 * SS)
    d.rounded_rectangle(s(BW0 + 194, BTOP + 63, BW0 + 420, BTOP + 73), 5 * SS, fill=(70, 72, 78))
    return finish(lay)


BROWSER = browser_chrome()
BROWSER_SHADOW = shadow_for((BW0, BTOP, BW1, GY + GH), 12)


def env_browser(i):
    img = Image.alpha_composite(BACK.convert("RGBA"), BROWSER_SHADOW)
    img = Image.alpha_composite(img, BROWSER)
    game = Image.open(f"{rec}/web/f{i:04d}.png").convert("RGB").resize((GW, GH), Image.LANCZOS)
    # The page is the game canvas, edge to edge, with rounded lower corners.
    mask = Image.new("L", (GW, GH), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, -20, GW - 1, GH - 1), 12, fill=255)
    img.paste(game, (GX, GY), mask)
    icon = Image.open(f"{rec}/web/icon{i:04d}.png").convert("RGB").resize((20, 20), Image.NEAREST)
    img.paste(icon, (BW0 + 106, BTOP + 18))
    return img.convert("RGB")


# ---- terminal ----
TX0, TX1, TTOP = GX - 14, GX + GW + 14, GY - 40
TBOT = GY + int(816 * GW / 1280) + 12


def term_chrome():
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rounded_rectangle(s(TX0, TTOP, TX1, TBOT), 12 * SS, fill=(29, 31, 33))
    d.rounded_rectangle(s(TX0, TTOP, TX1, GY - 6), 12 * SS, fill=(46, 48, 52))
    d.rectangle(s(TX0, GY - 18, TX1, GY - 6), fill=(46, 48, 52))
    lights(d, TX0 + 22, TTOP + 17)
    return finish(lay)


TERM = term_chrome()
TERM_SHADOW = shadow_for((TX0, TTOP, TX1, TBOT), 12)


def env_term(i):
    img = Image.alpha_composite(BACK.convert("RGBA"), TERM_SHADOW)
    img = Image.alpha_composite(img, TERM)
    shot = Image.open(f"{rec}/term/f{i:04d}.png").convert("RGB")
    shot = shot.resize((GW, int(shot.size[1] * GW / shot.size[0])), Image.LANCZOS)
    img.paste(shot, (GX, GY))
    return img.convert("RGB")


# ---- PDF in Chrome's viewer ----
# The game area in the viewer screenshot, measured from the recording.
PX0, PY0, PX1, PY1 = 608, 115, 1613, 755
PS = GW / (PX1 - PX0)
POX = GX - PX0 * PS
POY = GY + GH / 2 - (PY0 + PY1) / 2 * PS
CROP = (470, 0, 1750, 1080)  # part of the viewer kept, in screenshot pixels
WX0, WY0 = int(CROP[0] * PS + POX), int(CROP[1] * PS + POY)
WX1, WY1 = int(CROP[2] * PS + POX), min(H + 40, int(CROP[3] * PS + POY))
PDF_SHADOW = shadow_for((WX0, WY0, WX1, WY1), 12)


def env_pdf(i):
    img = Image.alpha_composite(BACK.convert("RGBA"), PDF_SHADOW).convert("RGB")
    shot = Image.open(f"{rec}/pdf/f{i:04d}.png").convert("RGB").crop(CROP)
    shot = shot.resize((WX1 - WX0, int(shot.size[1] * PS)), Image.LANCZOS)
    mask = Image.new("L", shot.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, shot.size[0] - 1, shot.size[1] + 40), 12, fill=255)
    img.paste(shot, (WX0, WY0), mask)
    return img


# ---- bare metal on a CRT ----
CX0, CX1, CY0, CY1 = GX - 92, GX + GW + 92, GY - 80, GY + GH + 110


def crt_chrome():
    lay = layer()
    d = ImageDraw.Draw(lay)
    # stand, then the case and the recessed screen
    d.polygon(s(W / 2 - 170, CY1 - 10, W / 2 + 170, CY1 - 10, W / 2 + 230, H + 10, W / 2 - 230, H + 10),
              fill=(178, 170, 150))
    d.rounded_rectangle(s(CX0, CY0, CX1, CY1), 34 * SS, fill=(214, 206, 186))
    d.rounded_rectangle(s(CX0 + 10, CY0 + 10, CX1 - 10, CY1 - 10), 28 * SS, outline=(196, 188, 168), width=3 * SS)
    d.rounded_rectangle(s(GX - 26, GY - 26, GX + GW + 26, GY + GH + 26), 30 * SS, fill=(150, 144, 128))
    d.rounded_rectangle(s(GX - 16, GY - 16, GX + GW + 16, GY + GH + 16), 26 * SS, fill=(18, 18, 18))
    # power button and its green light
    d.rounded_rectangle(s(CX1 - 150, CY1 - 58, CX1 - 96, CY1 - 38), 6 * SS, fill=(190, 182, 162))
    d.ellipse(s(CX1 - 70, CY1 - 54, CX1 - 58, CY1 - 42), fill=(70, 220, 90))
    for k in range(6):  # vents
        d.rounded_rectangle(s(CX0 + 60 + k * 26, CY1 - 56, CX0 + 74 + k * 26, CY1 - 40), 4 * SS, fill=(190, 182, 162))
    return finish(lay)


CRT = crt_chrome()
CRT_SHADOW = shadow_for((CX0, CY0, CX1, CY1), 34, 170)
_scan = np.ones((GH, 1, 1), np.float32)
_scan[1::3] = 0.82
_yy, _xx = np.mgrid[0:GH, 0:GW]
_vig = 1 - 0.22 * (((_xx - GW / 2) / (GW / 2)) ** 2 + ((_yy - GH / 2) / (GH / 2)) ** 2)
CRT_SHADE = (_scan * _vig[..., None]).astype(np.float32)
SCREEN_MASK = Image.new("L", (GW, GH), 0)
ImageDraw.Draw(SCREEN_MASK).rounded_rectangle((0, 0, GW - 1, GH - 1), 22, fill=255)


def env_x86(i):
    img = Image.alpha_composite(BACK.convert("RGBA"), CRT_SHADOW)
    img = Image.alpha_composite(img, CRT).convert("RGB")
    vga = Image.open(f"{rec}/x86/f{i:04d}.ppm").convert("RGB").resize((GW, GH), Image.NEAREST)
    a = np.asarray(vga).astype(np.float32) * CRT_SHADE
    glow = np.asarray(vga.filter(ImageFilter.GaussianBlur(3))).astype(np.float32)
    a = np.clip(a * 0.9 + glow * 0.22, 0, 255).astype(np.uint8)
    img.paste(Image.fromarray(a), (GX, GY), SCREEN_MASK)
    return img


ENVS = [env_browser, env_term, env_pdf, env_x86]

# ---- timeline ----
CUTS = [170, 360, 545]  # frame where each wipe starts
WIPE = 22
GRID_START, GRID_LEN = 640, 34

_xs = np.arange(W)[None, :]
_ys = np.arange(H)[:, None]


def ease(t):
    t = min(1, max(0, t))
    return t * t * (3 - 2 * t)


def wipe(a, b, t):
    """Soft diagonal wipe from a to b, t from 0 to 1."""
    edge = -500 + t * (W + 1100)
    alpha = np.clip((edge - (_xs + 0.35 * _ys)) / 160, 0, 1)[..., None]
    return Image.fromarray((np.asarray(a) * (1 - alpha) + np.asarray(b) * alpha).astype(np.uint8))


QW, QH = 912, 513
QPOS = [(40, 22), (W - QW - 40, 22), (40, H - QH - 22), (W - QW - 40, H - QH - 22)]


def grid(i, t):
    """Pulls back from the CRT to all four platforms in a two by two grid."""
    canvas = BACK.copy()
    e = ease(t)
    for k, env in enumerate(ENVS):
        if k == 3:
            continue
        if e <= 0:
            break
        q = env(i).resize((QW, QH), Image.LANCZOS)
        m = Image.new("L", (QW, QH), int(255 * e))
        canvas.paste(q, QPOS[k], m)
    full = env_x86(i)
    sw, sh = int(W + (QW - W) * e), int(H + (QH - H) * e)
    x, y = int(QPOS[3][0] * e), int(QPOS[3][1] * e)
    canvas.paste(full.resize((sw, sh), Image.LANCZOS), (x, y))
    return canvas


def frame(i):
    if i >= GRID_START:
        return grid(i, (i - GRID_START) / GRID_LEN)
    seg = sum(1 for c in CUTS if i >= c)
    for k, c in enumerate(CUTS):
        if c <= i < c + WIPE:
            return wipe(ENVS[k](i), ENVS[k + 1](i), ease((i - c) / WIPE))
    return ENVS[seg](i)


ff = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                       "-s", f"{W}x{H}", "-r", "30", "-i", "-", "-c:v", "libx264", "-preset", "slow",
                       "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out],
                      stdin=subprocess.PIPE)
only = [int(a) for a in sys.argv[3:]]
for i in (only or range(FRAMES)):
    img = frame(i)
    if only:
        img.save(out.replace(".mp4", f"-{i}.png"))
        continue
    ff.stdin.write(img.tobytes())
    if i % 50 == 0:
        print(i, flush=True)
ff.stdin.close()
ff.wait()
print("done")
