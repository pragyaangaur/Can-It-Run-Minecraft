/* Prints the player's position after every step of the demo, in the same
 * format the x86 kernel writes to its serial port, for comparison. */
#include <stdio.h>
#include "../src/rd_demo.h"

int main(void) {
    int me = rd_demo_start();
    float x, y, z;
    for (int i = 0; i * 2 <= RD_DEMO_TICKS; i++) {
        if (i) rd_demo_step(2);
        rd_player_pos(me, &x, &y, &z);
        printf("F %d %u %u %u\n", i + 1, (unsigned)(x * 1000), (unsigned)(y * 1000), (unsigned)(z * 1000));
    }
    return 0;
}
