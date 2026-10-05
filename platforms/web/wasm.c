/* WebAssembly glue for rdgeneric. Built with plain clang and wasm-ld, no
 * Emscripten and no libc. JavaScript reads the framebuffer straight out of
 * linear memory. */
#include "../../src/rd.h"
#include "../../src/rd_demo.h"

#ifndef MAX_W
#define MAX_W 1280
#endif
#ifndef MAX_H
#define MAX_H 800
#endif
static rd_u32 fb[MAX_W * MAX_H];
static rd_input in;
static int me;

/* clang may emit calls to these for struct copies and zeroing. */
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

#define EXPORT __attribute__((visibility("default")))

EXPORT void init(int seed, int style, int view) {
    rd_init((rd_u32)seed, style);
    rd_set_view_distance(view);
    me = rd_player_add();
}
EXPORT void set_input(float forward, float strafe, int jump) {
    in.forward = forward, in.strafe = strafe, in.jump = jump;
}
EXPORT void turn(float yaw, float pitch) { in.yaw += yaw, in.pitch += pitch; }
EXPORT void action(int dig, int place, int select, int reset) {
    in.dig |= dig, in.place |= place, in.reset |= reset;
    if (select) in.select = select;
}
EXPORT void tick(void) {
    rd_player_input(me, &in);
    in.yaw = in.pitch = 0;
    in.dig = in.place = in.reset = in.select = 0;
    rd_tick();
}
EXPORT rd_u32 *render(int w, int h) {
    if (w > MAX_W) w = MAX_W;
    if (h > MAX_H) h = MAX_H;
    rd_render(me, fb, w, h, w);
    /* Canvas wants RGBA bytes in memory, the core writes 0x00RRGGBB. */
    for (int i = 0; i < w * h; i++) {
        rd_u32 c = fb[i];
        fb[i] = 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
    }
    return fb;
}
/* The scripted demo, stepped by the caller so frames line up with every
 * other platform. */
EXPORT void demo_start(void) { me = rd_demo_start(); }
EXPORT int demo_step(int ticks) { return rd_demo_step(ticks); }
EXPORT int selected(void) { return rd_selected(me); }
EXPORT rd_u8 *blocks(void) { return rd_blocks(); }
EXPORT int world_bytes(void) { return rd_world_bytes(); }
EXPORT void relight(void) { rd_relight(); }
