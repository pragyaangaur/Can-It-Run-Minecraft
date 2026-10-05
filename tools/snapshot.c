/* Renders one frame to a PPM file so the core can be checked without a
 * screen. A second player stands a few blocks ahead, facing the camera.
 * Usage: snapshot out.ppm [style] [seed] [w] [h] */
#include <stdio.h>
#include <stdlib.h>
#include "../src/rd.h"

static float surface(int x, int z) {
    int y = RD_Y - 1;
    while (y > 0 && rd_blocks()[(y * RD_Z + z) * RD_X + x] == RD_AIR) y--;
    return (float)(y + 1);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "frame.ppm";
    int style = argc > 2 ? atoi(argv[2]) : RD_HILLS;
    unsigned seed = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    int w = argc > 4 ? atoi(argv[4]) : 640, h = argc > 5 ? atoi(argv[5]) : 400;
    rd_init(seed, style);
    int me = rd_player_add(), other = rd_player_add();
    int cx = RD_X / 2, cz = RD_Z / 2;
    rd_player_set(me, cx + 0.5f, surface(cx, cz), cz + 0.5f, 0.3f, -0.12f);
    rd_player_set(other, cx + 1.5f, surface(cx + 1, cz + 4), cz + 4.5f, 3.4f, 0);
    for (int i = 0; i < 30; i++) rd_tick();
    rd_u32 *fb = malloc(sizeof(rd_u32) * w * h);
    rd_render(me, fb, w, h, w);
    FILE *f = fopen(out, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char px[3] = {fb[i] >> 16, fb[i] >> 8, fb[i]};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    (void)other;
    return 0;
}
