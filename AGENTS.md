# rdgeneric (Miniaturised Minecraft)

Written 5 October 2026. Read `README.md` first for what the project is and what each port does. This file holds the context that the README does not.

## What it is and where it got to

rdgeneric is a clean-room recreation of Minecraft rd-132211 as one freestanding C file, `src/rd.c`, built so that it can be ported the way doomgeneric is. It was built in one session on 5 October 2026 to answer whether Minecraft can be run "anywhere" the way Doom is, and to survey where it has run before. The survey with sources is in the README.

Five platforms work and were tested on this Mac. They are the terminal, the telnet server with shared multiplayer, the browser (wasm), the PDF (Chromium only) and a bare-metal x86 Multiboot kernel run in QEMU. The microcontroller targets only compile. `make test` builds everything and checks each target.

Later on 5 October a scripted demo (`src/rd_demo.c`) was added, recorded on the four visual platforms and cut into one video, `docs/rdgeneric.mp4` and `docs/rdgeneric.gif`. The repository is public at github.com/pragyaangaur/Can-It-Run-Minecraft.

## How it was built

The core has no includes and no libc calls. It has its own floor, sine and square root, and every large array is static. Freestanding targets have to supply `memset` and `memcpy`, because clang emits calls to them for struct copies. `platforms/web/wasm.c` and `platforms/baremetal/kernel.c` both do this.

The wasm is built with Apple clang and Homebrew's `wasm-ld`, with no Emscripten. The PDF build compiles a second wasm with the full world and a 192 by 120 framebuffer, runs it through binaryen's `wasm2js`, strips the ES module `export` lines and embeds the result as the page's open action. `tools/make_pdf.py` writes the PDF by hand. The field geometry and the six equal-width shade characters come from DoomPDF.

The x86 kernel sets VGA mode 13h by writing registers (the table is from the OSDev wiki), remaps the PIC, runs the PIT at 1000 Hz through a one-entry IDT and polls the PS/2 port. It reads the current CS for the IDT gate instead of loading its own GDT. It prints fps to COM1, which is how the tests read it.

## How the video is made

`tools/record/record_all.sh` serves `build/`, runs the four recorders and calls `composite.py`. Every recorder steps its platform two ticks per frame and waits for that frame before capturing, so the clips line up exactly. The browser page takes `#demo` and exposes `rdDemo()`. The PDF takes `!` and `.` in its keyboard field. `rd-term --demo` steps on `.` and ends each frame with an invisible title escape that names the frame. The kernel steps on `.` and prints `F n x y z` to COM1 after each frame. The compositor keeps the game picture at the same place on a 1920 by 1080 stage and changes what is drawn around it.

## Traps

- After changing flags in the Makefile, rebuild from clean. The 486 test once ran a stale i686 binary that used `cmov` and triple-faulted. The x86 target now depends on the Makefile for that reason.
- The kernel's `tapped[]` latch matters. In slow emulation a key press and its release can arrive in the same poll, and without the latch the key never registers.
- Some embedded browser panes download PDFs instead of showing them. Use `tools/pdf_screenshot.py`, which drives Playwright's Chromium in headless mode, to see the PDF.
- In QEMU, `-monitor none -serial stdio` sends stdin to the serial port, so a piped `quit` never reaches the monitor and QEMU keeps running. Use `-monitor stdio -serial file:...` as `tests/run.sh` does.
- Apple clang has no RISC-V target, so the ESP32-C3 and BL602 cannot be size-checked without a full LLVM.
- Determinism across platforms rests on three things. Terrain is generated with integer maths, every build passes `-ffp-contract=off`, and `generate()` clears the level before building. Two real bugs were found while recording: `generate()` used to leave the previous world's blocks in place, and `rd_player_add()` kept a stale `on_ground` flag. Either one made the x86 demo drift from the others.
- Capturing `rd-term` through a macOS pty is slow, about half a second per truecolor frame, so the terminal recorder takes around ten minutes. It is not hung.
- The xterm.js canvas renderer ignores Playwright's device scale factor. Use scale 1 and a bigger font, or the screenshot shows only the top left quarter.
- Chrome's PDF viewer keeps typed characters in a text field whatever the keystroke script sets, so the PDF recorder presses Backspace after every key.
- The physics constants in `tick_player` were written from memory of public decompilations of rd-132211 and have not been checked against the original jar. The README says so. Keep it that way unless someone checks them.

## Next steps, roughly in order of payoff

1. A Raspberry Pi Pico port with a cheap SPI LCD. This would be the first time the core draws on a microcontroller, and the frame rate there is unknown because the M0+ has no FPU.
2. A fixed-point build of the renderer, which would help the Pico and every other FPU-less target.
3. Boot the x86 ELF on a real old PC through GRUB and record the frame rate.
4. Find out whether anyone else has put Minecraft in a PDF. The searches on 5 October 2026 found nothing, so the README says only that.
