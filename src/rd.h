/* rdgeneric: a portable recreation of Minecraft rd-132211.
 *
 * The core is freestanding C99. It includes no headers, calls no libc
 * function and never allocates. A platform supplies a framebuffer and an
 * input struct, calls rd_tick() sixty times a second and rd_render() when
 * it wants a picture. That is the whole porting surface, in the spirit of
 * doomgeneric.
 *
 * The world size is fixed at compile time. The defaults match rd-132211:
 * 256 x 256 blocks across and 64 tall, 4 MB. Small targets build with, for
 * example, -DRD_X=32 -DRD_Y=32 -DRD_Z=32.
 */
#ifndef RD_H
#define RD_H

#ifndef RD_X
#define RD_X 256
#endif
#ifndef RD_Y
#define RD_Y 64 /* vertical */
#endif
#ifndef RD_Z
#define RD_Z 256
#endif
#ifndef RD_MAX_PLAYERS
#define RD_MAX_PLAYERS 8
#endif

typedef unsigned char rd_u8;
typedef unsigned int rd_u32;

enum {
    RD_AIR = 0,
    RD_STONE,
    RD_GRASS,
    RD_DIRT,
    RD_COBBLE,
    RD_PLANKS,
    RD_LOG,
    RD_LEAVES,
    RD_NUM_BLOCKS
};

/* RD_FLAT is the rd-132211 level: solid up to two thirds of the height with
 * grass on top. RD_HILLS adds terrain and trees. */
enum { RD_FLAT = 0, RD_HILLS = 1 };

typedef struct {
    float forward; /* -1..1 */
    float strafe;  /* -1..1, positive is right */
    float yaw;     /* radians to turn, positive is right, used once */
    float pitch;   /* radians to turn, positive is up, used once */
    int jump;      /* held */
    int dig;       /* set to break the targeted block, used once */
    int place;     /* set to place the selected block, used once */
    int select;    /* 1..RD_NUM_BLOCKS-1 picks a block, 0 keeps it */
    int reset;     /* respawn at a random spot, the R key in rd-132211 */
} rd_input;

void rd_init(rd_u32 seed, int style);

int rd_player_add(void); /* returns an id, or -1 when the world is full */
void rd_player_remove(int id);

/* Held values replace the previous ones. One-shot values (turning, dig,
 * place, select, reset) accumulate until the next rd_tick() uses them. */
void rd_player_input(int id, const rd_input *in);

void rd_tick(void); /* advances every player by 1/60 s */

/* Draws what player id sees into fb as 0x00RRGGBB pixels. Other players
 * appear as figures. */
void rd_render(int id, rd_u32 *fb, int w, int h, int stride);

void rd_set_view_distance(int blocks); /* lower it on slow hardware */

/* The level in rd-132211's layout, index (y * RD_Z + z) * RD_X + x. */
rd_u8 *rd_blocks(void);
int rd_world_bytes(void);
void rd_relight(void); /* call after writing into rd_blocks() directly */

int rd_selected(int id);
void rd_player_pos(int id, float *x, float *y, float *z);
void rd_player_set(int id, float x, float y, float z, float yaw, float pitch);

#endif
