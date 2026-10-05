# rdgeneric. `make` builds every platform.
CC      ?= clang
# No fused multiply-add anywhere, so every platform rounds floats the same
# way and the scripted demo plays out identically on all of them.
FP      = -ffp-contract=off
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra $(FP)
WASMLD  ?= /opt/homebrew/bin/wasm-ld
CORE    = src/rd.c src/rd.h src/rd_demo.c src/rd_demo.h
SRC     = src/rd.c src/rd_demo.c

all: rd-term build/rdgeneric.html build/rdgeneric.pdf build/rdgeneric-x86.elf

rd-term: platforms/term/term.c $(CORE)
	$(CC) $(CFLAGS) -o $@ platforms/term/term.c $(SRC)

WASM = --target=wasm32 -O3 -std=c99 -nostdlib -ffreestanding -fvisibility=hidden $(FP) \
       -fuse-ld=$(WASMLD) -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all

build/rd.wasm: platforms/web/wasm.c $(CORE)
	@mkdir -p build
	clang $(WASM) -Wl,--initial-memory=16777216 -o $@ platforms/web/wasm.c $(SRC)

build/rdgeneric.html: platforms/web/shell.html build/rd.wasm tools/inline.py
	python3 tools/inline.py platforms/web/shell.html build/rd.wasm $@

# The PDF build: the same world, a small framebuffer, and the wasm turned
# into plain JavaScript by wasm2js because PDFium has no WebAssembly.
PDF_W ?= 192
PDF_H ?= 120
build/rd-pdf.wasm: platforms/web/wasm.c $(CORE)
	@mkdir -p build
	clang $(WASM) -DMAX_W=$(PDF_W) -DMAX_H=$(PDF_H) -Wl,--initial-memory=6291456 \
	  -o $@ platforms/web/wasm.c $(SRC)

build/rd-pdf.js: build/rd-pdf.wasm
	wasm2js -O2 $< -o $@

build/rdgeneric.pdf: build/rd-pdf.js platforms/pdf/runtime.js tools/make_pdf.py
	python3 tools/make_pdf.py build/rd-pdf.js platforms/pdf/runtime.js $@ $(PDF_W) $(PDF_H)

# Bare-metal x86: a Multiboot kernel. Run with `make run-x86`.
X86 = --target=i386-unknown-none-elf -march=i486 -O2 -std=c99 -ffreestanding -nostdlib \
      -fno-pic -fno-stack-protector -Wall -Wextra $(FP)
build/rdgeneric-x86.elf: Makefile platforms/baremetal/boot.S platforms/baremetal/kernel.c platforms/baremetal/link.ld $(CORE)
	@mkdir -p build/x86
	clang $(X86) -c platforms/baremetal/boot.S -o build/x86/boot.o
	clang $(X86) -c platforms/baremetal/kernel.c -o build/x86/kernel.o
	clang $(X86) -c src/rd.c -o build/x86/rd.o
	clang $(X86) -c src/rd_demo.c -o build/x86/rd_demo.o
	ld.lld -m elf_i386 -T platforms/baremetal/link.ld -o $@ build/x86/*.o

run-x86: build/rdgeneric-x86.elf
	qemu-system-i386 -cpu 486 -kernel $< -m 64

clean:
	rm -rf build rd-term

.PHONY: all clean run-x86
