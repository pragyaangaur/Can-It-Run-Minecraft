/* Bare-metal x86 platform for rdgeneric. No operating system and no BIOS
 * calls: the kernel programs the VGA card into 320x200 with 256 colours by
 * writing its registers, reads the PS/2 keyboard port and counts PIT
 * interrupts for time. Boots from GRUB or with qemu -kernel. */
#include "../../src/rd.h"
#include "../../src/rd_demo.h"

#define VW 320
#define VH 200
#ifndef SCALE
#define SCALE 2 /* render at 160x100 and double it */
#endif
#define RW (VW / SCALE)
#define RH (VH / SCALE)

volatile unsigned ticks;
void isr_timer(void);

static inline void outb(unsigned short p, unsigned char v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(p)); }
static inline unsigned char inb(unsigned short p) {
    unsigned char v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

void *memset(void *d, int c, unsigned long n) {
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n) {
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

/* ---- VGA mode 13h, register values from the OSDev wiki ---- */

static unsigned char mode13[] = {
    0x63,                                           /* misc */
    0x03, 0x01, 0x0F, 0x00, 0x0E,                   /* sequencer */
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, /* crtc */
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF, /* graphics */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,       /* attribute */
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00};

static void set_mode13(void) {
    unsigned char *r = mode13;
    outb(0x3C2, *r++);
    for (int i = 0; i < 5; i++) outb(0x3C4, (unsigned char)i), outb(0x3C5, *r++);
    outb(0x3D4, 0x03), outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11), outb(0x3D5, inb(0x3D5) & 0x7F);
    r[0x03] |= 0x80;
    r[0x11] &= 0x7F;
    for (int i = 0; i < 25; i++) outb(0x3D4, (unsigned char)i), outb(0x3D5, *r++);
    for (int i = 0; i < 9; i++) outb(0x3CE, (unsigned char)i), outb(0x3CF, *r++);
    for (int i = 0; i < 21; i++) (void)inb(0x3DA), outb(0x3C0, (unsigned char)i), outb(0x3C0, *r++);
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
    /* A 6x6x6 colour cube. */
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        int c = i < 216 ? i : 215;
        outb(0x3C9, (unsigned char)(c / 36 * 63 / 5));
        outb(0x3C9, (unsigned char)(c / 6 % 6 * 63 / 5));
        outb(0x3C9, (unsigned char)(c % 6 * 63 / 5));
    }
}

/* ---- interrupts: only the timer is unmasked ---- */

static struct { unsigned short lo, sel; unsigned char zero, type; unsigned short hi; } idt[256];
static struct __attribute__((packed)) { unsigned short limit; unsigned int base; } idtr;

static void start_timer(void) {
    unsigned short cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    unsigned int h = (unsigned int)isr_timer;
    idt[32].lo = (unsigned short)h, idt[32].hi = (unsigned short)(h >> 16);
    idt[32].sel = cs, idt[32].type = 0x8E;
    idtr.limit = sizeof idt - 1, idtr.base = (unsigned int)idt;
    __asm__ volatile("lidt %0" ::"m"(idtr));
    /* Move the PICs to vectors 32 and 40, then mask all but IRQ0. */
    outb(0x20, 0x11), outb(0xA0, 0x11);
    outb(0x21, 0x20), outb(0xA1, 0x28);
    outb(0x21, 0x04), outb(0xA1, 0x02);
    outb(0x21, 0x01), outb(0xA1, 0x01);
    outb(0x21, 0xFE), outb(0xA1, 0xFF);
    /* PIT channel 0 at 1000 Hz. */
    outb(0x43, 0x34);
    outb(0x40, 1193 & 0xFF), outb(0x40, 1193 >> 8);
    __asm__ volatile("sti");
}

/* ---- keyboard, scan code set 1, which reports releases ---- */

/* down[] is the live state. tapped[] remembers a press until the next tick
 * uses it, so a key pressed and released within one slow frame still counts. */
static unsigned char down[256], tapped[256];
/* Each "." press steps the scripted demo by two ticks. The first one also
 * starts it. */
static unsigned demo_presses;

static void poll_keys(rd_input *in) {
    static int ext = 0;
    while (inb(0x64) & 1) {
        unsigned char sc = inb(0x60);
        if (sc == 0xE0) { ext = 1; continue; }
        int code = (sc & 0x7F) | (ext ? 0x80 : 0), pressed = !(sc & 0x80);
        ext = 0;
        if (pressed && !down[code]) {
            if (code >= 0x02 && code <= 0x08) in->select = code - 1; /* 1..7 */
            if (code == 0x13) in->reset = 1;                         /* R */
            if (code == 0x21) in->dig = 1;                           /* F */
            if (code == 0x12) in->place = 1;                         /* E */
            if (code == 0x34) demo_presses++;                        /* . */
        }
        down[code] = (unsigned char)pressed;
        if (pressed) tapped[code] = 1;
    }
}

/* ---- main ---- */

static rd_u32 frame[RW * RH];

static const unsigned char bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void present(void) {
    unsigned char *vga = (unsigned char *)0xA0000;
    for (int y = 0; y < VH; y++)
        for (int x = 0; x < VW; x++) {
            rd_u32 c = frame[(y / SCALE) * RW + x / SCALE];
            int t = bayer[(y & 3) * 4 + (x & 3)] * 255;
            int r = (int)((c >> 16 & 255) * 80 + t) / 4080;
            int g = (int)((c >> 8 & 255) * 80 + t) / 4080;
            int b = (int)((c & 255) * 80 + t) / 4080;
            if (r > 5) r = 5;
            if (g > 5) g = 5;
            if (b > 5) b = 5;
            vga[y * VW + x] = (unsigned char)(r * 36 + g * 6 + b);
        }
}

/* COM1, for an fps line that QEMU can show with -serial stdio. */
static void serial_puts(const char *s) {
    while (*s) {
        while (!(inb(0x3FD) & 0x20)) {}
        outb(0x3F8, (unsigned char)*s++);
    }
}
static void serial_num(unsigned n) {
    char b[12];
    int i = 11;
    b[i] = 0;
    do b[--i] = (char)('0' + n % 10), n /= 10; while (n);
    serial_puts(b + i);
}

static unsigned char cmos(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void kmain(void) {
    unsigned int cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~4u) | 2u | 32u; /* FPU on: clear EM, set MP and NE */
    __asm__ volatile("mov %0, %%cr0; fninit" ::"r"(cr0));

    set_mode13();
    start_timer();
    rd_init((rd_u32)cmos(0) * 3600 + cmos(2) * 60 + cmos(4) + 1, RD_HILLS);
    rd_set_view_distance(32);
    int me = rd_player_add();

    unsigned last = ticks, acc = 0, frames = 0, fps_t = ticks;
    rd_input in;
    memset(&in, 0, sizeof in);
    unsigned demo_done = 0;
    int demo = 0;
    for (;;) {
        poll_keys(&in);
        if (demo_presses != demo_done) {
            /* Demo mode: one frame per key press, then a serial line with
             * the frame number and position for the recorder to wait on. */
            if (!demo) me = rd_demo_start(), demo = 1;
            else rd_demo_step(2);
            demo_done++;
            rd_render(me, frame, RW, RH, RW);
            present();
            float x, y, z;
            rd_player_pos(me, &x, &y, &z);
            serial_puts("F ");
            serial_num(demo_done);
            serial_puts(" ");
            serial_num((unsigned)(x * 1000));
            serial_puts(" ");
            serial_num((unsigned)(y * 1000));
            serial_puts(" ");
            serial_num((unsigned)(z * 1000));
            serial_puts("\r\n");
            continue;
        }
        if (demo) continue;
        unsigned now = ticks;
        acc += (now - last) * 60;
        last = now;
        if (acc > 60 * 250) acc = 60 * 250;
        while (acc >= 1000) {
            poll_keys(&in);
#define K(c) (down[c] | tapped[c])
            in.forward = (float)(K(0x11) - K(0x1F));                       /* W S */
            in.strafe = (float)(K(0x20) - K(0x1E));                        /* D A */
            in.jump = K(0x39);                                             /* space */
            in.yaw = (K(0xCD) - K(0xCB) + K(0x26) - K(0x24)) * 0.04f;      /* right left, L J */
            in.pitch = (K(0xC8) - K(0xD0) + K(0x17) - K(0x25)) * 0.03f;    /* up down, I K */
#undef K
            rd_player_input(me, &in);
            in.dig = in.place = in.reset = in.select = 0;
            memset(tapped, 0, sizeof tapped);
            rd_tick();
            acc -= 1000;
        }
        rd_render(me, frame, RW, RH, RW);
        present();
        frames++;
        if (ticks - fps_t >= 2000) {
            serial_puts("rdgeneric: ");
            serial_num(frames * 1000 / (ticks - fps_t));
            serial_puts(" fps\r\n");
            frames = 0;
            fps_t = ticks;
        }
    }
}
