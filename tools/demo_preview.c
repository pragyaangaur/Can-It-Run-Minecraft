/* Plays the demo natively and writes every nth frame as a PPM, so the path
 * can be tuned without any platform. Usage: demo_preview DIR [every] [w h] */
#include <stdio.h>
#include <stdlib.h>
#include "../src/rd_demo.h"

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    int every = argc > 2 ? atoi(argv[2]) : 2;
    int w = argc > 3 ? atoi(argv[3]) : 320, h = argc > 4 ? atoi(argv[4]) : 200;
    int me = rd_demo_start();
    rd_u32 *fb = malloc(sizeof(rd_u32) * w * h);
    for (int n = 0, t = 0; t < RD_DEMO_TICKS; n++) {
        rd_render(me, fb, w, h, w);
        char path[512];
        snprintf(path, sizeof path, "%s/f%04d.ppm", dir, n);
        FILE *f = fopen(path, "wb");
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            unsigned char px[3] = {fb[i] >> 16, fb[i] >> 8, fb[i]};
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        t = rd_demo_step(every);
    }
    float x, y, z;
    rd_player_pos(me, &x, &y, &z);
    printf("end at %.3f %.3f %.3f\n", x, y, z);
    return 0;
}
