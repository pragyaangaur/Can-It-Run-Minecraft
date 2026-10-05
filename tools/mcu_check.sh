#!/bin/bash
# Cross-compiles the core for microcontrollers and reports flash and RAM.
# Nothing here links or runs, it proves the core needs no libc or OS.
SIZE=$(xcrun --find llvm-size 2>/dev/null || command -v llvm-size)
DEFS="-DRD_X=32 -DRD_Z=32 -DRD_Y=24 -DRD_MAX_PLAYERS=1"
out=${TMPDIR:-/tmp}/rd-mcu
mkdir -p "$out"
check() {
    name=$1; shift
    clang "$@" -Os -std=c99 -ffp-contract=off -ffreestanding -nostdlib -Wall -Wextra $DEFS -c src/rd.c -o "$out/$name.o" || return
    "$SIZE" "$out/$name.o" | awk -v n="$name" 'NR==2 {printf "%-34s flash %6d B   RAM %6d B\n", n, $1+$2, $2+$3}'
}
check "RP2040 (Cortex-M0+, Pico)"     --target=thumbv6m-none-eabi -mcpu=cortex-m0plus
check "RP2350 (Cortex-M33, Pico 2)"   --target=thumbv8m.main-none-eabihf -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16
check "Game Boy Advance (ARM7TDMI)"   --target=armv4t-none-eabi -mcpu=arm7tdmi -mthumb
check "386 real hardware"             --target=i386-unknown-none-elf -march=i386
