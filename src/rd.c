/* rdgeneric core. See rd.h. Freestanding: no includes beyond rd.h. */
#include "rd.h"

#define PI 3.14159265f
#define EYE 1.62f
#define HALF_W 0.3f
#define TALL 1.8f
#define REACH 5.0f

/* ---- small maths, since there is no libm ---- */

static int ifloor(float v) {
    int i = (int)v;
    return (v < (float)i) ? i - 1 : i;
}
static float fabs_(float v) { return v < 0 ? -v : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static float sin_(float x) {
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    if (x > PI / 2) x = PI - x;
    if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float cos_(float x) { return sin_(x + PI / 2); }

static float sqrt_(float v) {
    if (v <= 0) return 0;
    union { float f; rd_u32 i; } u = {v};
    u.i = 0x1fbd1df5 + (u.i >> 1);
    float r = u.f;
    r = 0.5f * (r + v / r);
    r = 0.5f * (r + v / r);
    return 0.5f * (r + v / r);
}

static rd_u32 hash3(rd_u32 a, rd_u32 b, rd_u32 c) {
    rd_u32 h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    return h ^ (h >> 15);
}

static rd_u32 rng_state = 1;
static float frand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (rng_state >> 8) * (1.0f / 16777216.0f);
}

/* ---- the level ---- */

static rd_u8 blocks[RD_X * RD_Y * RD_Z];
static rd_u8 light_depth[RD_X * RD_Z]; /* first y that sees the sky */
static rd_u32 world_seed;

static int idx(int x, int y, int z) { return (y * RD_Z + z) * RD_X + x; }
static int inside(int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < RD_X && y < RD_Y && z < RD_Z;
}
static int get(int x, int y, int z) { return inside(x, y, z) ? blocks[idx(x, y, z)] : RD_AIR; }

static void relight_column(int x, int z) {
    int y = RD_Y - 1;
    while (y >= 0 && blocks[idx(x, y, z)] == RD_AIR) y--;
    light_depth[x + z * RD_X] = (rd_u8)(y + 1);
}

/* rd-132211's light rule: a cell is lit when nothing solid is above it. */
static int is_lit(int x, int y, int z) {
    if (x < 0 || z < 0 || x >= RD_X || z >= RD_Z) return 1;
    return y >= light_depth[x + z * RD_X];
}

static void set_block(int x, int y, int z, int b) {
    if (!inside(x, y, z)) return;
    blocks[idx(x, y, z)] = (rd_u8)b;
    relight_column(x, z);
}

void rd_relight(void) {
    for (int z = 0; z < RD_Z; z++)
        for (int x = 0; x < RD_X; x++) relight_column(x, z);
}

rd_u8 *rd_blocks(void) { return blocks; }
int rd_world_bytes(void) { return (int)sizeof blocks; }

/* Value noise in integers, 0..1023, at a grid spacing of `scale` cells.
 * Terrain is built from it in integer maths so every platform makes the
 * same world. A 486's x87 unit keeps floats at 80 bits and would round a
 * few column heights differently. */
static rd_u32 smooth10(rd_u32 f) { return f * f * (3072 - 2 * f) >> 20; }

static int value_noise(int x, int z, int scale, rd_u32 salt) {
    int ix = x / scale, iz = z / scale;
    rd_u32 fx = smooth10((rd_u32)(x % scale) * 1024 / scale);
    rd_u32 fz = smooth10((rd_u32)(z % scale) * 1024 / scale);
    int a = (int)(hash3(ix, iz, salt) & 1023), b = (int)(hash3(ix + 1, iz, salt) & 1023);
    int c = (int)(hash3(ix, iz + 1, salt) & 1023), d = (int)(hash3(ix + 1, iz + 1, salt) & 1023);
    int top = a + (b - a) * (int)fx / 1024, bot = c + (d - c) * (int)fx / 1024;
    return top + (bot - top) * (int)fz / 1024;
}

static void grow_tree(int x, int y, int z) {
    int trunk = 4 + (int)(hash3(x, z, world_seed + 7) % 2);
    for (int dy = -2; dy <= 1; dy++) {
        int r = dy >= 0 ? 1 : 2;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (r == 2 && fabs_((float)dx) == 2 && fabs_((float)dz) == 2 &&
                    (hash3(x + dx, z + dz, y + dy) & 1))
                    continue;
                int yy = y + trunk + dy;
                if (get(x + dx, yy, z + dz) == RD_AIR && inside(x + dx, yy, z + dz))
                    blocks[idx(x + dx, yy, z + dz)] = RD_LEAVES;
            }
    }
    for (int i = 0; i < trunk && y + i < RD_Y; i++) blocks[idx(x, y + i, z)] = RD_LOG;
}

static void generate(int style) {
    /* Start from empty air, since rd_init may be called on a used level. */
    for (int i = 0; i < RD_X * RD_Y * RD_Z; i++) blocks[i] = RD_AIR;
    for (int z = 0; z < RD_Z; z++)
        for (int x = 0; x < RD_X; x++) {
            int top;
            if (style == RD_FLAT) {
                top = RD_Y * 2 / 3;
            } else {
                int n = (value_noise(x, z, 32, world_seed) * 6 + value_noise(x, z, 12, world_seed + 1) * 3 +
                         value_noise(x, z, 5, world_seed + 2)) / 10;
                top = (RD_Y * 35 + n * RD_Y * 35 / 1024) / 100;
            }
            if (top > RD_Y - 1) top = RD_Y - 1;
            for (int y = 0; y <= top; y++) {
                int b = RD_STONE;
                if (y == top) b = RD_GRASS;
                else if (style != RD_FLAT && y > top - 4) b = RD_DIRT;
                blocks[idx(x, y, z)] = (rd_u8)b;
            }
        }
    if (style != RD_FLAT) {
        for (int z = 3; z < RD_Z - 3; z++)
            for (int x = 3; x < RD_X - 3; x++) {
                if (hash3(x, z, world_seed + 3) % 97) continue;
                int y = RD_Y - 1;
                while (y > 0 && blocks[idx(x, y, z)] == RD_AIR) y--;
                if (blocks[idx(x, y, z)] == RD_GRASS && y + 8 < RD_Y) grow_tree(x, y + 1, z);
            }
    }
    rd_relight();
}

/* ---- textures, made procedurally at start ---- */

#define TRANSPARENT 0x01000000u
enum { FACE_TOP, FACE_SIDE, FACE_BOTTOM };
static rd_u32 tex[RD_NUM_BLOCKS][3][16 * 16];

static rd_u32 shade_rgb(rd_u32 c, float k) {
    int r = (int)(((c >> 16) & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)((c & 255) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (rd_u32)(r << 16 | g << 8 | b);
}

static rd_u32 texel(int block, int face, int u, int v) {
    float n = (hash3(u, v, block * 3 + face) & 255) / 255.0f;
    switch (block) {
    case RD_STONE: {
        float blob = value_noise(u, v, 3, 11) > 737 ? 0.82f : 1.0f;
        return shade_rgb(0x7d7d7d, (0.86f + n * 0.18f) * blob);
    }
    case RD_DIRT: return shade_rgb(0x8a5f3c, 0.8f + n * 0.3f);
    case RD_GRASS:
        if (face == FACE_TOP) return shade_rgb(0x62a83f, 0.8f + n * 0.3f);
        if (face == FACE_SIDE && v < 3 + (int)(hash3(u, 0, 77) % 3)) return shade_rgb(0x62a83f, 0.75f + n * 0.3f);
        return shade_rgb(0x8a5f3c, 0.8f + n * 0.3f);
    case RD_COBBLE: {
        int row = v / 4, col = (u + row * 3) / 5;
        int edge = (v % 4 == 0) || ((u + row * 3) % 5 == 0);
        float k = 0.8f + (hash3(row, col, 5) & 255) / 255.0f * 0.3f;
        return shade_rgb(0x8c8c8c, edge ? 0.55f : k * (0.92f + n * 0.12f));
    }
    case RD_PLANKS: {
        int edge = (v % 4 == 3) || u == (v / 4 * 7 + 3) % 16;
        return shade_rgb(0xb08a54, edge ? 0.62f : 0.88f + n * 0.14f);
    }
    case RD_LOG:
        if (face != FACE_SIDE) {
            float du = u - 7.5f, dv = v - 7.5f;
            int ring = (int)sqrt_(du * du + dv * dv);
            if (ring >= 7) return shade_rgb(0x5f4a2c, 0.9f + n * 0.15f);
            return shade_rgb(0xae8d58, (ring & 1) ? 0.84f : 1.0f);
        }
        return shade_rgb(0x5f4a2c, (u % 4 == 0 ? 0.7f : 0.92f) + n * 0.16f);
    case RD_LEAVES:
        if (n > 0.78f) return TRANSPARENT;
        return shade_rgb(0x3f8f2f, 0.65f + n * 0.45f);
    }
    return 0xff00ff;
}

static void make_textures(void) {
    for (int b = 1; b < RD_NUM_BLOCKS; b++)
        for (int f = 0; f < 3; f++)
            for (int v = 0; v < 16; v++)
                for (int u = 0; u < 16; u++) tex[b][f][v * 16 + u] = texel(b, f, u, v);
}

/* ---- players ---- */

typedef struct {
    int active;
    float x, y, z; /* feet, centre of the box */
    float xd, yd, zd;
    float yaw, pitch;
    int on_ground;
    int selected;
    rd_input in;
    int has_target;
    int tx, ty, tz, nx, ny, nz;
} player;

static player players[RD_MAX_PLAYERS];
static float view_distance = 48;

void rd_set_view_distance(int blocks_) { view_distance = (float)(blocks_ < 4 ? 4 : blocks_); }

/* rd-132211 drops the player from above the level at a random spot. This
 * puts them on the surface there instead, so nobody waits for the fall. */
static void respawn(player *p) {
    p->x = 2 + frand() * (RD_X - 4);
    p->z = 2 + frand() * (RD_Z - 4);
    p->y = (float)light_depth[(int)p->x + (int)p->z * RD_X];
    p->xd = p->yd = p->zd = 0;
    p->on_ground = 0;
}

void rd_init(rd_u32 seed, int style) {
    world_seed = seed;
    rng_state = seed ? seed : 1;
    make_textures();
    generate(style);
    for (int i = 0; i < RD_MAX_PLAYERS; i++) players[i].active = 0;
}

int rd_player_add(void) {
    for (int i = 0; i < RD_MAX_PLAYERS; i++)
        if (!players[i].active) {
            player *p = &players[i];
            rd_input none = {0};
            p->active = 1;
            p->yaw = frand() * 2 * PI;
            p->pitch = 0;
            p->selected = RD_STONE;
            p->in = none;
            p->has_target = 0;
            respawn(p);
            return i;
        }
    return -1;
}

void rd_player_remove(int id) {
    if (id >= 0 && id < RD_MAX_PLAYERS) players[id].active = 0;
}

void rd_player_input(int id, const rd_input *in) {
    if (id < 0 || id >= RD_MAX_PLAYERS) return;
    rd_input *s = &players[id].in;
    s->forward = in->forward;
    s->strafe = in->strafe;
    s->jump = in->jump;
    s->yaw += in->yaw;
    s->pitch += in->pitch;
    s->dig |= in->dig;
    s->place |= in->place;
    s->reset |= in->reset;
    if (in->select) s->select = in->select;
}

int rd_selected(int id) { return players[id].selected; }
void rd_player_set(int id, float x, float y, float z, float yaw, float pitch) {
    player *p = &players[id];
    p->x = x, p->y = y, p->z = z, p->yaw = yaw, p->pitch = pitch;
    p->xd = p->yd = p->zd = 0;
    p->on_ground = 0;
}
void rd_player_pos(int id, float *x, float *y, float *z) {
    *x = players[id].x;
    *y = players[id].y;
    *z = players[id].z;
}

typedef struct { float x0, y0, z0, x1, y1, z1; } box;

static box player_box(const player *p) {
    box b = {p->x - HALF_W, p->y, p->z - HALF_W, p->x + HALF_W, p->y + TALL, p->z + HALF_W};
    return b;
}

static float clip_x(box c, box b, float xa) {
    if (b.y1 <= c.y0 || b.y0 >= c.y1 || b.z1 <= c.z0 || b.z0 >= c.z1) return xa;
    if (xa > 0 && b.x1 <= c.x0 && c.x0 - b.x1 < xa) xa = c.x0 - b.x1;
    if (xa < 0 && b.x0 >= c.x1 && c.x1 - b.x0 > xa) xa = c.x1 - b.x0;
    return xa;
}
static float clip_y(box c, box b, float ya) {
    if (b.x1 <= c.x0 || b.x0 >= c.x1 || b.z1 <= c.z0 || b.z0 >= c.z1) return ya;
    if (ya > 0 && b.y1 <= c.y0 && c.y0 - b.y1 < ya) ya = c.y0 - b.y1;
    if (ya < 0 && b.y0 >= c.y1 && c.y1 - b.y0 > ya) ya = c.y1 - b.y0;
    return ya;
}
static float clip_z(box c, box b, float za) {
    if (b.x1 <= c.x0 || b.x0 >= c.x1 || b.y1 <= c.y0 || b.y0 >= c.y1) return za;
    if (za > 0 && b.z1 <= c.z0 && c.z0 - b.z1 < za) za = c.z0 - b.z1;
    if (za < 0 && b.z0 >= c.z1 && c.z1 - b.z0 > za) za = c.z1 - b.z0;
    return za;
}

/* Entity.move from rd-132211: clip against every cube the swept box
 * touches, y first, then x, then z. */
static void move(player *p, float xa, float ya, float za) {
    float xo = xa, yo = ya, zo = za;
    box b = player_box(p);
    box e = b;
    if (xa < 0) e.x0 += xa; else e.x1 += xa;
    if (ya < 0) e.y0 += ya; else e.y1 += ya;
    if (za < 0) e.z0 += za; else e.z1 += za;
    int x0 = ifloor(e.x0), x1 = ifloor(e.x1 + 1), y0 = ifloor(e.y0), y1 = ifloor(e.y1 + 1);
    int z0 = ifloor(e.z0), z1 = ifloor(e.z1 + 1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 > RD_X) x1 = RD_X;
    if (y1 > RD_Y) y1 = RD_Y;
    if (z1 > RD_Z) z1 = RD_Z;

#define EACH_CUBE                                       \
    for (int y = y0; y < y1; y++)                       \
        for (int z = z0; z < z1; z++)                   \
            for (int x = x0; x < x1; x++)               \
                if (blocks[idx(x, y, z)] != RD_AIR)
#define CUBE ((box){(float)x, (float)y, (float)z, x + 1.0f, y + 1.0f, z + 1.0f})

    EACH_CUBE ya = clip_y(CUBE, b, ya);
    b.y0 += ya, b.y1 += ya;
    EACH_CUBE xa = clip_x(CUBE, b, xa);
    b.x0 += xa, b.x1 += xa;
    EACH_CUBE za = clip_z(CUBE, b, za);
    b.z0 += za, b.z1 += za;
#undef EACH_CUBE
#undef CUBE

    p->on_ground = yo != ya && yo < 0;
    if (xo != xa) p->xd = 0;
    if (yo != ya) p->yd = 0;
    if (zo != za) p->zd = 0;
    p->x = (b.x0 + b.x1) / 2;
    p->y = b.y0;
    p->z = (b.z0 + b.z1) / 2;
}

/* ---- ray casting, used both to draw and to pick ---- */

typedef struct {
    int block, x, y, z; /* cell hit */
    int px, py, pz;     /* cell the ray came from */
    int axis, step;     /* face crossed */
    float t;
} hit;

/* The texel a ray hits, shared by the walk (for leaf holes) and the
 * drawing, so both always agree on which texel was hit. */
static rd_u32 face_texel(int block, int axis, int step, float hx, float hy, float hz, float *u_out,
                         float *v_out) {
    float u, v;
    if (axis == 0) u = hz, v = -hy;
    else if (axis == 1) u = hx, v = hz;
    else u = hx, v = -hy;
    u -= ifloor(u), v -= ifloor(v);
    if (u_out) *u_out = u, *v_out = v;
    int face = axis != 1 ? FACE_SIDE : step < 0 ? FACE_TOP : FACE_BOTTOM;
    return tex[block][face][((int)(v * 16) & 15) * 16 + ((int)(u * 16) & 15)];
}

/* Walks the grid from o along d (Amanatides and Woo). When see_through is
 * set, transparent texels of leaves let the ray carry on. */
static int cast(float ox, float oy, float oz, float dx, float dy, float dz, float max_t,
                int see_through, hit *h) {
    int x = ifloor(ox), y = ifloor(oy), z = ifloor(oz);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    float ax = fabs_(dx), ay = fabs_(dy), az = fabs_(dz);
    float tdx = ax > 1e-6f ? 1 / ax : 1e30f;
    float tdy = ay > 1e-6f ? 1 / ay : 1e30f;
    float tdz = az > 1e-6f ? 1 / az : 1e30f;
    float tx = (sx > 0 ? (x + 1 - ox) : (ox - x)) * tdx;
    float ty = (sy > 0 ? (y + 1 - oy) : (oy - y)) * tdy;
    float tz = (sz > 0 ? (z + 1 - oz) : (oz - z)) * tdz;
    float t = 0;
    int axis = 1;
    for (;;) {
        int px = x, py = y, pz = z;
        if (tx < ty && tx < tz) {
            t = tx, tx += tdx, x += sx, axis = 0;
        } else if (ty < tz) {
            t = ty, ty += tdy, y += sy, axis = 1;
        } else {
            t = tz, tz += tdz, z += sz, axis = 2;
        }
        if (t > max_t) return 0;
        if (!inside(x, y, z)) {
            /* Leaving the level for good ends the walk. */
            if ((x < 0 && sx < 0) || (x >= RD_X && sx > 0) || (z < 0 && sz < 0) ||
                (z >= RD_Z && sz > 0) || (y < 0 && sy < 0) || (y >= RD_Y && sy > 0))
                return 0;
            continue;
        }
        int b = blocks[idx(x, y, z)];
        if (b == RD_AIR) continue;
        h->block = b, h->x = x, h->y = y, h->z = z;
        h->px = px, h->py = py, h->pz = pz;
        h->axis = axis;
        h->step = axis == 0 ? sx : axis == 1 ? sy : sz;
        h->t = t;
        if (see_through && b == RD_LEAVES) {
            if (face_texel(b, axis, h->step, ox + dx * t, oy + dy * t, oz + dz * t, 0, 0) ==
                TRANSPARENT)
                continue;
        }
        return 1;
    }
}

static void update_target(player *p) {
    float cp = cos_(p->pitch);
    hit h;
    p->has_target = cast(p->x, p->y + EYE, p->z, sin_(p->yaw) * cp, sin_(p->pitch),
                         cos_(p->yaw) * cp, REACH, 0, &h);
    if (!p->has_target) return;
    p->tx = h.x, p->ty = h.y, p->tz = h.z;
    p->nx = h.px - h.x, p->ny = h.py - h.y, p->nz = h.pz - h.z;
}

static int box_hits_players(int x, int y, int z) {
    for (int i = 0; i < RD_MAX_PLAYERS; i++) {
        if (!players[i].active) continue;
        box b = player_box(&players[i]);
        if (b.x1 > x && b.x0 < x + 1 && b.y1 > y && b.y0 < y + 1 && b.z1 > z && b.z0 < z + 1)
            return 1;
    }
    return 0;
}

static void tick_player(player *p) {
    rd_input *in = &p->in;
    p->yaw += in->yaw;
    p->pitch = clampf(p->pitch + in->pitch, -1.55f, 1.55f);
    if (in->select > 0 && in->select < RD_NUM_BLOCKS) p->selected = in->select;
    if (in->reset || p->y < -64) respawn(p);

    /* Player.tick from rd-132211, at its 60 Hz timer. */
    if (in->jump && p->on_ground) p->yd = 0.12f;
    float xa = in->strafe, za = in->forward;
    float dist = xa * xa + za * za;
    if (dist >= 0.01f) {
        float speed = p->on_ground ? 0.02f : 0.005f;
        dist = speed / sqrt_(dist);
        xa *= dist, za *= dist;
        float s = sin_(p->yaw), c = cos_(p->yaw);
        p->xd += xa * c + za * s;
        p->zd += za * c - xa * s;
    }
    p->yd -= 0.005f;
    move(p, p->xd, p->yd, p->zd);
    p->xd *= 0.91f, p->yd *= 0.98f, p->zd *= 0.91f;
    if (p->on_ground) p->xd *= 0.8f, p->zd *= 0.8f;

    update_target(p);
    if (p->has_target && in->dig) {
        set_block(p->tx, p->ty, p->tz, RD_AIR);
        update_target(p);
    } else if (p->has_target && in->place) {
        int x = p->tx + p->nx, y = p->ty + p->ny, z = p->tz + p->nz;
        if (inside(x, y, z) && get(x, y, z) == RD_AIR && !box_hits_players(x, y, z))
            set_block(x, y, z, p->selected);
        update_target(p);
    }
    in->yaw = in->pitch = 0;
    in->dig = in->place = in->reset = 0;
    in->select = 0;
}

void rd_tick(void) {
    for (int i = 0; i < RD_MAX_PLAYERS; i++)
        if (players[i].active) tick_player(&players[i]);
}

/* ---- drawing ---- */

#define SKY 0x80ccff /* rd-132211's clear colour, 0.5 0.8 1.0 */

static const rd_u32 shirts[8] = {0x2a9d8f, 0xe76f51, 0x8e44ad, 0xf4a261,
                                 0x3a86ff, 0xd62828, 0x2b9348, 0xffbe0b};

static rd_u32 lerp_rgb(rd_u32 a, rd_u32 b, float k) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return (rd_u32)((int)(ar + (br - ar) * k) << 16 | (int)(ag + (bg - ag) * k) << 8 |
                    (int)(ab + (bb - ab) * k));
}

/* Ray against another player's box. Returns the colour, or 0 for a miss. */
static rd_u32 hit_figure(const player *q, int qi, float ox, float oy, float oz, float dx,
                         float dy, float dz, float *t_out) {
    box b = player_box(q);
    float lo = 0, hi = *t_out;
    int axis = -1;
    float o[3] = {ox, oy, oz}, d[3] = {dx, dy, dz};
    float mn[3] = {b.x0, b.y0, b.z0}, mx[3] = {b.x1, b.y1, b.z1};
    for (int a = 0; a < 3; a++) {
        if (fabs_(d[a]) < 1e-6f) {
            if (o[a] < mn[a] || o[a] > mx[a]) return 0;
            continue;
        }
        float t0 = (mn[a] - o[a]) / d[a], t1 = (mx[a] - o[a]) / d[a];
        if (t0 > t1) { float s = t0; t0 = t1; t1 = s; }
        if (t0 > lo) lo = t0, axis = a;
        if (t1 < hi) hi = t1;
        if (lo > hi) return 0;
    }
    if (axis < 0) return 0;
    *t_out = lo;
    float hy = oy + dy * lo - q->y;
    rd_u32 c = hy > 1.35f ? 0xc99a6d : hy > 0.75f ? shirts[qi & 7] : 0x3b4a8c;
    return shade_rgb(c, axis == 1 ? 1.0f : axis == 2 ? 0.8f : 0.6f);
}

static void draw_hud(const player *p, rd_u32 *fb, int w, int h, int stride) {
    int cx = w / 2, cy = h / 2, arm = h / 40 + 1;
    for (int i = -arm; i <= arm; i++) {
        rd_u32 *a = &fb[cy * stride + cx + i], *b = &fb[(cy + i) * stride + cx];
        *a = ~*a & 0xffffff;
        if (i) *b = ~*b & 0xffffff;
    }
    int s = h / 9 < 6 ? 6 : h / 9, x0 = 2, y0 = h - s - 2;
    for (int y = -1; y <= s; y++)
        for (int x = -1; x <= s; x++) {
            rd_u32 c = 0x000000;
            if (x >= 0 && y >= 0 && x < s && y < s) {
                c = tex[p->selected][FACE_SIDE][(y * 16 / s) * 16 + x * 16 / s];
                if (c == TRANSPARENT) c = 0x204020;
            }
            if (x0 + x >= 0 && y0 + y >= 0 && x0 + x < w && y0 + y < h)
                fb[(y0 + y) * stride + x0 + x] = c;
        }
}

void rd_render(int id, rd_u32 *fb, int w, int h, int stride) {
    const player *p = &players[id];
    float ox = p->x, oy = p->y + EYE, oz = p->z;
    float sy_ = sin_(p->yaw), cy_ = cos_(p->yaw), sp = sin_(p->pitch), cp = cos_(p->pitch);
    float fx = sy_ * cp, fy = sp, fz = cy_ * cp;   /* forward */
    float rx = cy_, rz = -sy_;                      /* right */
    float ux = -sy_ * sp, uy = cp, uz = -cy_ * sp;  /* up */
    float tan_v = 0.62f, tan_h = tan_v * w / h;
    float far = view_distance, fog_start = far * 0.55f;

    for (int j = 0; j < h; j++) {
        float v = (1 - 2 * (j + 0.5f) / h) * tan_v;
        rd_u32 *row = fb + j * stride;
        for (int i = 0; i < w; i++) {
            float u = (2 * (i + 0.5f) / w - 1) * tan_h;
            float dx = fx + rx * u + ux * v, dy = fy + uy * v, dz = fz + rz * u + uz * v;
            /* Sky gets slightly paler towards the horizon. */
            float horizon = clampf(1 - fabs_(dy) * 1.2f, 0, 1);
            rd_u32 sky = lerp_rgb(SKY, 0xc8e6ff, horizon * 0.6f);
            rd_u32 c = sky;
            hit hh;
            float t = far;
            if (cast(ox, oy, oz, dx, dy, dz, far, 1, &hh)) {
                t = hh.t;
                float uu, vv;
                c = face_texel(hh.block, hh.axis, hh.step, ox + dx * t, oy + dy * t, oz + dz * t, &uu, &vv);
                /* rd-132211 shades y faces 1.0, z faces 0.8, x faces 0.6. */
                float k = hh.axis == 1 ? 1.0f : hh.axis == 2 ? 0.8f : 0.6f;
                if (!is_lit(hh.px, hh.py, hh.pz)) k *= 0.6f;
                c = shade_rgb(c, k);
                if (p->has_target && hh.x == p->tx && hh.y == p->ty && hh.z == p->tz) {
                    float e = 0.07f;
                    if (uu < e || uu > 1 - e || vv < e || vv > 1 - e) c = 0x101010;
                }
            }
            for (int q = 0; q < RD_MAX_PLAYERS; q++) {
                if (q == id || !players[q].active) continue;
                rd_u32 fc = hit_figure(&players[q], q, ox, oy, oz, dx, dy, dz, &t);
                if (fc) c = fc;
            }
            if (t > fog_start) c = lerp_rgb(c, sky, clampf((t - fog_start) / (far - fog_start), 0, 1));
            row[i] = c;
        }
    }
    draw_hud(p, fb, w, h, stride);
}
