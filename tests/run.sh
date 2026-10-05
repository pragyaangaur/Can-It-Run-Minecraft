#!/bin/bash
# Builds every target and checks each one does what the README says.
# QEMU and Playwright are optional. Their checks are skipped when absent.
set -u
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

make -s all >/dev/null 2>"$tmp/make.log" && ok "make all" || { bad "make all"; cat "$tmp/make.log"; exit 1; }

# Core renders a varied frame with a second player in view.
clang -O2 -std=c99 -o "$tmp/snap" tools/snapshot.c src/rd.c &&
    "$tmp/snap" "$tmp/f.ppm" 1 5 160 100 &&
    python3 -c "
import sys; d=open(sys.argv[1],'rb').read().split(b'\n',3)[3]
cols={d[i:i+3] for i in range(0,len(d),3)}; sys.exit(0 if len(cols)>200 else 1)" "$tmp/f.ppm" &&
    ok "core renders" || bad "core renders"

# Wasm stays small.
size=$(wc -c < build/rd.wasm | tr -d " ")
[ "$size" -lt 40000 ] && ok "wasm is $size bytes" || bad "wasm is $size bytes"

node tests/pdf_script.js build/rdgeneric.pdf && ok "pdf script runs" || bad "pdf script runs"

# Telnet server serves a frame and a status line.
./rd-term --serve 23999 --seed 2 >/dev/null 2>&1 &
pid=$!
for i in $(seq 1 50); do nc -z 127.0.0.1 23999 2>/dev/null && break; sleep 0.1; done
python3 tests/telnet_probe.py 23999 "$tmp/t.ppm" www | grep -q "players 1" && ok "telnet server" || bad "telnet server"
kill $pid 2>/dev/null; wait $pid 2>/dev/null

tools/mcu_check.sh | grep -q "Pico" && ok "embedded targets compile" || bad "embedded targets compile"

if command -v qemu-system-i386 >/dev/null; then
    ( sleep 14; echo quit ) | qemu-system-i386 -cpu 486 -kernel build/rdgeneric-x86.elf -m 64 \
        -display none -monitor stdio -serial file:"$tmp/serial" >/dev/null 2>&1
    grep -q "fps" "$tmp/serial" && ok "boots on a 486 in qemu ($(tail -1 "$tmp/serial" | tr -d '\r'))" || bad "boots in qemu"
    # The scripted demo on the 486 build has to track the native build, or
    # the platforms in the video would drift apart.
    clang -O2 -std=c99 -ffp-contract=off -o "$tmp/trace" tools/demo_trace.c src/rd_demo.c src/rd.c
    "$tmp/trace" > "$tmp/native.txt"
    python3 tools/record/record_x86.py "$tmp/x86" 751 --no-dumps >/dev/null 2>&1
    python3 - "$tmp/native.txt" "$tmp/x86/trace.txt" <<'PY' && ok "demo on x86 matches native to 0.002 blocks" || bad "demo on x86 matches native"
import sys
a = [l.split() for l in open(sys.argv[1])]
b = [l.split() for l in open(sys.argv[2])]
worst = max(abs(int(p) - int(q)) for r, s in zip(a, b) for p, q in zip(r[2:], s[2:]))
sys.exit(0 if len(a) == len(b) == 751 and worst <= 2 else 1)
PY
else
    echo "skip qemu not installed"
fi
rm -rf "$tmp"
exit $fail
