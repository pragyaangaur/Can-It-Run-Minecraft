# rdgeneric. `make` builds every platform.
CC      ?= clang
# No fused multiply-add anywhere, so every platform rounds floats the same
# way and the scripted demo plays out identically on all of them.
FP      = -ffp-contract=off
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra $(FP)
WASMLD  ?= /opt/homebrew/bin/wasm-ld
CORE    = src/rd.c src/rd.h src/rd_demo.c src/rd_demo.h
SRC     = src/rd.c src/rd_demo.c

all: rd-term build/rdgeneric.html

rd-term: platforms/term/term.c $(CORE)
	$(CC) $(CFLAGS) -o $@ platforms/term/term.c $(SRC)

WASM = --target=wasm32 -O3 -std=c99 -nostdlib -ffreestanding -fvisibility=hidden $(FP) \
       -fuse-ld=$(WASMLD) -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all

build/rd.wasm: platforms/web/wasm.c $(CORE)
	@mkdir -p build
	clang $(WASM) -Wl,--initial-memory=16777216 -o $@ platforms/web/wasm.c $(SRC)

build/rdgeneric.html: platforms/web/shell.html build/rd.wasm tools/inline.py
	python3 tools/inline.py platforms/web/shell.html build/rd.wasm $@

clean:
	rm -rf build rd-term

.PHONY: all clean
