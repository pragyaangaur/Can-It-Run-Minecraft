#!/bin/bash
# Records the scripted demo on every platform and builds the video and GIF.
# Needs a built tree (make), QEMU, ffmpeg, gifski and Playwright's Chromium.
# Writes the 1080p master to video/rdgeneric.mp4 (not committed), and a
# 720p copy and a GIF to docs/.
# Usage: tools/record/record_all.sh REC_DIR
set -eu
cd "$(dirname "$0")/../.."
rec=$1
out=video/rdgeneric.mp4
mkdir -p "$rec" video

python3 -m http.server 8766 --directory build >/dev/null 2>&1 &
server=$!
trap 'kill $server' EXIT
sleep 1

python3 tools/record/record_web.py "http://localhost:8766/rdgeneric.html#demo" "$rec/web"
python3 tools/record/record_pdf.py "http://localhost:8766/rdgeneric.pdf#view=FitH" "$rec/pdf"
python3 tools/record/record_term.py "$rec/term"
python3 tools/record/record_x86.py "$rec/x86"
python3 tools/record/composite.py "$rec" "$out"

ffmpeg -loglevel error -y -i "$out" -vf scale=1280:-2 -c:v libx264 -preset slow -crf 24 \
    -pix_fmt yuv420p -movflags +faststart docs/rdgeneric.mp4

# The GIF stays under GitHub's comfortable size for README images.
mkdir -p "$rec/gif"
ffmpeg -loglevel error -y -i "$out" -vf "fps=10,scale=640:-1:flags=lanczos" "$rec/gif/f%04d.png"
gifski --quality 75 --fps 10 --width 640 -o docs/rdgeneric.gif "$rec"/gif/f*.png
