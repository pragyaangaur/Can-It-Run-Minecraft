"""Puts a binary into an HTML template as base64, so the page is one file
that also works from file://. Usage: inline.py template binary out"""
import base64, sys

template, binary, out = sys.argv[1:4]
html = open(template).read()
data = base64.b64encode(open(binary, "rb").read()).decode()
open(out, "w").write(html.replace("@@WASM_BASE64@@", data))
