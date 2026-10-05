/* The demo path: a list of segments, each holding one set of inputs for a
 * stretch of ticks. Freestanding like the core. */
#include "rd_demo.h"

#define DEMO_SEED 5
#define DEMO_VIEW 40

typedef struct {
    int t1;           /* segment runs until this tick */
    float fwd, strafe;
    float yaw, pitch; /* radians per tick */
    int jump;
    int dig_every;    /* 0 for none */
    int pillar;       /* jump and place a block underneath, every 48 ticks */
    int aim;          /* 1 turns towards the pillar, 2 towards the open start */
    int select;       /* block to hold from this segment on, 0 keeps it */
} seg;

static const seg path[] = {
    /* walk out across the open ground with a hop, then look around */
    {60, 1, 0, 0.003f, 0, 0, 0, 0, 0, 0},
    {95, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {200, 0, 0, -0.014f, 0.002f, 0, 0, 0, 0, 0},
    {260, 0, 0, 0.01f, -0.004f, 0, 0, 0, 0, 0},
    /* look down and dig a pit in front */
    {320, 0, 0, 0, -0.014f, 0, 0, 0, 0, 0},
    {410, 0, 0, 0, -0.001f, 0, 22, 0, 0, 0},
    /* look straight down and build a pillar of planks under our feet */
    {460, 0, 0, 0.004f, -0.02f, 0, 0, 0, 0, RD_PLANKS},
    {748, 0, 0, 0, 0, 0, 0, 1, 0, 0},
    /* stand on top and turn a full circle */
    {800, 0, 0, 0.004f, 0.026f, 0, 0, 0, 0, 0},
    {1050, 0, 0, 0.0252f, 0, 0, 0, 0, 0, 0},
    /* step off and walk away */
    {1080, 0, 0, 0, 0, 0, 0, 0, 2, 0},
    {1150, 1, 0, 0, 0, 0, 0, 0, 2, 0},
    /* turn back to the pillar and circle it */
    {1230, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {RD_DEMO_TICKS, 0, 1, 0, 0, 0, 0, 0, 1, 0},
};

#define PI 3.14159265f

static int me = -1, tick = 0;
static float pillar_x, pillar_z, pillar_top, start_x, start_z;

static int column_top(int x, int z) {
    int y = RD_Y - 1;
    while (y > 0 && rd_blocks()[(y * RD_Z + z) * RD_X + x] == RD_AIR) y--;
    return y;
}

/* Finds the spot near the middle with the most open ground around it, so
 * the walk does not run into trees. */
static void open_spot(int *bx, int *bz) {
    int best = -1;
    *bx = RD_X / 2, *bz = RD_Z / 2;
    for (int cz = RD_Z / 2 - 24; cz <= RD_Z / 2 + 24; cz += 2)
        for (int cx = RD_X / 2 - 24; cx <= RD_X / 2 + 24; cx += 2) {
            int clear = 0;
            for (int r = 1; r < 16 && clear == r - 1; r++) {
                int ok = 1;
                for (int dz = -r; dz <= r && ok; dz++)
                    for (int dx = -r; dx <= r && ok; dx++) {
                        int x = cx + dx, z = cz + dz;
                        if (x < 0 || z < 0 || x >= RD_X || z >= RD_Z) continue;
                        int b = rd_blocks()[(column_top(x, z) * RD_Z + z) * RD_X + x];
                        if (b == RD_LEAVES || b == RD_LOG) ok = 0;
                    }
                if (ok) clear = r;
            }
            if (clear > best) best = clear, *bx = cx, *bz = cz;
        }
}

static float atan2_(float y, float x) {
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax < 1e-6f && ay < 1e-6f) return 0;
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = PI / 2 - r;
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}

static float sqrt_(float v) {
    float r = v > 1 ? v : 1;
    for (int i = 0; i < 12; i++) r = 0.5f * (r + v / r);
    return r;
}

static float wrap(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

static float limit(float v, float m) { return v > m ? m : v < -m ? -m : v; }

static float yaw_now, pitch_now;

int rd_demo_start(void) {
    rd_init(DEMO_SEED, RD_HILLS);
    rd_set_view_distance(DEMO_VIEW);
    me = rd_player_add();
    int x, z;
    open_spot(&x, &z);
    yaw_now = 0.5f, pitch_now = -0.05f;
    start_x = x + 0.5f, start_z = z + 0.5f;
    rd_player_set(me, x + 0.5f, (float)(column_top(x, z) + 1), z + 0.5f, yaw_now, pitch_now);
    tick = 0;
    return me;
}

int rd_demo_step(int ticks) {
    for (int i = 0; i < ticks; i++) {
        int s = 0;
        while (s < (int)(sizeof path / sizeof path[0]) - 1 && tick >= path[s].t1) s++;
        const seg *g = &path[s];
        int t0 = s ? path[s - 1].t1 : 0, local = tick - t0;
        float px, py, pz;
        rd_player_pos(me, &px, &py, &pz);
        if (g->pillar) pillar_x = px, pillar_z = pz, pillar_top = py;
        rd_input in = {0};
        in.forward = g->fwd;
        in.strafe = g->strafe;
        in.yaw = g->yaw;
        in.pitch = g->pitch;
        if (g->aim) {
            /* Ease towards the target. */
            float tx = g->aim == 1 ? pillar_x : start_x, tz = g->aim == 1 ? pillar_z : start_z;
            float dx = tx - px, dz = tz - pz;
            float want_yaw = atan2_(dx, dz);
            float want_pitch = g->aim == 1 ? atan2_(pillar_top - 3.5f - (py + 1.62f), sqrt_(dx * dx + dz * dz)) : -0.1f;
            in.yaw = limit(wrap(want_yaw - yaw_now) * 0.08f, 0.035f);
            in.pitch = limit((want_pitch - pitch_now) * 0.08f, 0.02f);
        }
        yaw_now += in.yaw;
        pitch_now += in.pitch;
        if (pitch_now > 1.55f) pitch_now = 1.55f;
        if (pitch_now < -1.55f) pitch_now = -1.55f;
        in.jump = g->jump || (g->pillar && local % 48 == 0);
        in.select = local == 0 ? g->select : 0;
        in.dig = g->dig_every && local % g->dig_every == g->dig_every - 1;
        /* The jump peaks about 1.07 blocks up after 20 ticks, just clear of
         * the cell below the feet. */
        in.place = g->pillar && local % 48 == 20;
        rd_player_input(me, &in);
        rd_tick();
        tick++;
    }
    return tick;
}
