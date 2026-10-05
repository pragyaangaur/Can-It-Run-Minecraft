"""Writes the rdgeneric PDF by hand, with no PDF library.

Usage: make_pdf.py engine.js runtime.js out.pdf WIDTH HEIGHT

The page carries the game as its open action. Every screen row is a text
field two units tall, the same geometry DoomPDF uses, and keyboard input
arrives through a text field's keystroke action.
"""
import sys

engine_js, runtime_js, out, W, H = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])

engine = open(engine_js).read()
# wasm2js writes an ES module. PDF scripts are plain scripts, so the export
# lines go and the exports object is kept as `rd`.
engine = "\n".join(l for l in engine.splitlines() if not l.startswith("export "))
engine += "\nvar rd = retasmFunc;\n"
runtime = open(runtime_js).read().replace("__W__", str(W)).replace("__H__", str(H))
script = "try {\n" + engine + runtime + "\n} catch (e) { app.alert('rdgeneric: ' + (e.stack || e)); }\n"

objects = []  # each entry is the bytes between "n 0 obj" and "endobj"


def add(body):
    objects.append(body if isinstance(body, bytes) else body.encode("latin-1"))
    return len(objects)


def pdf_str(s):
    return "(" + s.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")"


def stream(data, extra=""):
    if isinstance(data, str):
        data = data.encode("latin-1")
    return b"<< /Length %d %s>>\nstream\n" % (len(data), extra.encode()) + data + b"\nendstream"


def js_action(code):
    return add("<< /S /JavaScript /JS %d 0 R >>" % add(stream(code)))


def text_field(name, x, y, w, h, value="", extra=""):
    return add(
        "<< /Type /Annot /Subtype /Widget /FT /Tx /Ff 2 /T %s /V %s /Rect [%g %g %g %g] /BS << /W 0 >> %s>>"
        % (pdf_str(name), pdf_str(value), x, y, x + w, y + h, extra)
    )


def button(name, label, x, y, w, h, key):
    down = js_action("btn_down('%s')" % key)
    up = js_action("btn_up('%s')" % key)
    return add(
        "<< /Type /Annot /Subtype /Widget /FT /Btn /Ff 65536 /T %s /Rect [%g %g %g %g] "
        "/MK << /BG [0.88] /CA %s >> /AA << /D %d 0 R /U %d 0 R >> >>"
        % (pdf_str(name), x, y, x + w, y + h, pdf_str(label), down, up)
    )


ROW = 2
screen_w = W * 2 - 8
panel = 190
# The screen sits centred above a 560 unit wide control panel.
page_w, page_h = max(screen_w, 560) + 40, H * ROW + panel + 20
sx = (page_w - screen_w) / 2
ox = (page_w - 560) / 2

annots = []
for j in range(H):
    annots.append(text_field("row_%d" % j, sx, panel + (H - 1 - j) * ROW, screen_w, ROW))
annots.append(text_field("status", ox + 8, panel - 26, 300, 14, "loading"))
keys_k = js_action("key_pressed(event.change); event.change = '';")
annots.append(text_field("keys", ox + 330, 96, 200, 44, "Click here, then type to play", "/AA << /K %d 0 R >> " % keys_k))

# (label, key, x, y, width)
pad = [("^", "w", 46, 120, 36), ("<", "a", 8, 82, 36), ("v", "s", 46, 82, 36), (">", "d", 84, 82, 36),
       ("^", "i", 178, 120, 36), ("<", "j", 140, 82, 36), ("v", "k", 178, 82, 36), (">", "l", 216, 82, 36),
       ("jump", " ", 8, 44, 70), ("dig", "f", 82, 44, 56), ("place", "e", 142, 44, 56),
       ("block", "n", 202, 44, 56), ("respawn", "r", 262, 44, 60)]
for n, (label, key, x, y, w) in enumerate(pad):
    annots.append(button("btn_%d" % n, label, ox + x, y, w, 32, key))

def text(x, y, size, s):
    return "BT /F1 %d Tf %g %g Td %s Tj ET" % (size, ox + x, y, pdf_str(s))

content = "\n".join([
    text(52, 158, 8, "move"),
    text(184, 158, 8, "look"),
    text(330, 160, 18, "rdgeneric"),
    text(330, 146, 8, "Minecraft rd-132211, rewritten in C, running inside a PDF."),
    text(330, 76, 8, "Keys: WASD move, IJKL look, space jump,"),
    text(330, 66, 8, "F dig, E place, 1-7 block, R respawn."),
    text(8, 22, 7, "Needs a Chromium browser (Chrome, Edge, Brave). Other PDF viewers do not run the script."),
    text(8, 12, 7, "No WebAssembly here: the C core was compiled to wasm, then to plain JavaScript by wasm2js."),
])

open_action = js_action(script)
font = add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
contents = add(stream(content))
# The catalog, page tree and page come last because they point at the rest.
catalog_id, pages_id, page_id = len(objects) + 1, len(objects) + 2, len(objects) + 3
add("<< /Type /Catalog /Pages %d 0 R >>" % pages_id)
add("<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % page_id)
add(
    "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Resources << /Font << /F1 %d 0 R >> >> "
    "/Contents %d 0 R /Annots [%s] /AA << /O %d 0 R >> >>"
    % (pages_id, page_w, page_h, font, contents, " ".join("%d 0 R" % a for a in annots), open_action)
)

out_bytes = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
offsets = []
for i, body in enumerate(objects, 1):
    offsets.append(len(out_bytes))
    out_bytes += b"%d 0 obj\n" % i + body + b"\nendobj\n"
xref = len(out_bytes)
out_bytes += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
for off in offsets:
    out_bytes += b"%010d 00000 n \n" % off
out_bytes += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, catalog_id, xref)
open(out, "wb").write(out_bytes)
print("%s: %d bytes, %d objects" % (out, len(out_bytes), len(objects)))
