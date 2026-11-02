/*
 * doom.c - DOOM.  A raycaster, eight monsters, one pistol.   PURE C.
 *
 * "Does it run Doom?" - the real one cannot: DOOM1.WAD alone is 4 MB, the
 * engine wants megabytes of RAM, and this chip has 32 KB of flash and 12 KB
 * of RAM.  What fits is what came BEFORE Doom's BSP trees: a RAYCASTER, the
 * technique of Wolfenstein 3D (1992), written here from scratch with integers
 * only.  The map, the sprites and every line of code are this lesson's own;
 * only the idea - a first-person view of a grid, monsters you shoot, a door
 * you bump, an exit switch at the far end - is borrowed.
 *
 * THE VIEW.  For each of the 128 screen columns, fire a ray from the player
 * through the map grid and find the first wall it hits (the DDA walk below).
 * The wall is drawn as a vertical strip whose height is 1 / distance: near
 * walls are tall, far walls short.  That is the whole of "3D" here.  The
 * strip is shaded by distance with an ordered dither, because the panel has
 * one bit per pixel.
 *
 *   world       24 x 16 cells, one char each (lvl[] below); '#' brick,
 *               '%' panel, 'D' door, 'E' exit, z i a h = things on the floor
 *   position    Q8: 256 = one cell, like every position in this arcade
 *   angle       1/8192 of a turn (so a gentle stick still turns the view:
 *               the same truncation bug lesson 12's paddle once had), looked
 *               up in a 129-entry quarter sine table - no float, no libm
 *   ray         direction + plane * camera_x, the Lode Vandevenne method:
 *               with a flat camera plane the distance is PERPENDICULAR to it,
 *               so walls do not bulge (the fisheye of angle-stepped rays)
 *   divisions   three per column (two step lengths, one wall height): the
 *               M0+ has no divide instruction, so each is a library call.
 *               The DDA walk itself is only adds and compares
 *
 * THE MONSTERS are billboards: a 16 x 16 picture scaled to 1/distance,
 * drawn far to near, and hidden column by column where a nearer wall
 * covers them (zbuf[], one byte of distance per screen column).  Two kinds:
 * the ZOMBIE shoots from a distance (hitscan, less accurate the farther you
 * are), the IMP is fast and claws.  They sleep until they see you or hear a
 * shot.  Each is a five-state machine: IDLE, CHASE, ATTACK, PAIN, DEAD.
 *
 *   RAM: map 384 + things 160 + zbuf 128 + the rest = 741 bytes (measured).
 *   Controls: stick up/down = walk, left/right = turn, A (held) = fire.
 *   B is the arcade's pause, so there is no "use": walk into a door to open
 *   it, walk into the framed switch at the far end to leave the level.
 *
 * Drawing writes the FINAL value of every pixel of the view, ON or OFF, and
 * never clears first.  oled_pixel() marks a page dirty only if a byte really
 * changed, so a view that has not changed costs no bus at all.  The view
 * changes nearly every page when you move - the full-repaint worst case of
 * slide 10: 912 bytes a frame on the host, 83 % of a full repaint, where
 * SNAKE sends 129.  The host model (bus time only) gives 47.5 frames a second;
 * the CPU time of 7 040 pixel writes is on top of that and not yet measured.
 * Slide 24b has the numbers.
 */
#include <stdio.h>
#include <string.h>
#include "engine.h"

#define MW      24                      /* map cells across                 */
#define MH      16                      /* map cells down                   */
#define VW      OLED_W                  /* view: 128 columns ...            */
#define VH      (OLED_H - FIELD_Y)      /* ... by 55 rows under the HUD     */
#define HZ      (FIELD_Y + VH / 2)      /* the horizon row                  */
#define WALLK   100                     /* wall height in px at 1 cell away: true cubes for a 66 degree view */
#define SPRK    75                      /* monster height, likewise (0.75 of a wall) */
#define PLANE   169                     /* camera plane = 0.66 x direction: a 66 degree view */
#define NTH     16                      /* monsters + pickups in the world  */
#define TURN    (8192)                  /* angle units in a full turn       */

/* ---- the level: 24 x 16.  S start, z zombie, i imp, a ammo, h health ----- */
static const char lvl[MH][MW + 1] = {
    "########################",
    "#h.....#.........#.....#",
    "#...i..D....z....D..i..E",
    "#......#.....i...#.....#",
    "#......#.........#....h#",
    "###D########D#######D###",
    "#......#..........#....#",
    "#...z..#..%%..%%..#.z..#",
    "#.a....D..%%..%%..D....#",
    "#......#..........#....#",
    "###D####..........######",
    "#......#....i.....#....#",
    "#....a.#..%%..%%..#....#",
    "#..S...D..%%..%%z.D..a.#",
    "#......#.a........#.h..#",
    "########################",
};

/* ---- sin(a) x 256 for a = 0..128 of 512 per turn: one quarter wave ------- */
static const uint16_t sin_q[129] = {
      0,   3,   6,   9,  13,  16,  19,  22,  25,  28,  31,  34,
     38,  41,  44,  47,  50,  53,  56,  59,  62,  65,  68,  71,
     74,  77,  80,  83,  86,  89,  92,  95,  98, 101, 104, 107,
    109, 112, 115, 118, 121, 123, 126, 129, 132, 134, 137, 140,
    142, 145, 147, 150, 152, 155, 157, 160, 162, 165, 167, 170,
    172, 174, 177, 179, 181, 183, 185, 188, 190, 192, 194, 196,
    198, 200, 202, 204, 206, 207, 209, 211, 213, 215, 216, 218,
    220, 221, 223, 224, 226, 227, 229, 230, 231, 233, 234, 235,
    237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248,
    248, 249, 250, 250, 251, 252, 252, 253, 253, 254, 254, 254,
    255, 255, 255, 256, 256, 256, 256, 256, 256,
};

/* The other three quarters are the first one mirrored: 129 numbers, not 512. */
static int isin(int a)                          /* a: 512 per turn, result Q8 */
{
    a &= 511;
    int i = a & 127;
    int v = (a & 128) ? sin_q[128 - i] : sin_q[i];
    return (a & 256) ? -v : v;
}
static int icos(int a) { return isin(a + 128); }

/* ---- the sprites: 16 rows x {ink, mask}; the art is host/doom_art.txt ---- */
enum { SP_ZOMBIE, SP_ZOMBIE_FIRE, SP_IMP_A, SP_IMP_B, SP_IMP_CLAW, SP_CORPSE,
       SP_AMMO, SP_MEDKIT, SP_PISTOL, SP_FLASH, SP_FACE, SP_FACE_HURT, NSPR };

static const uint16_t spr[NSPR][2][16] = {
    /* ZOMBIE  (halo) */
    /*   ................ */
    /*   .....######..... */
    /*   ....########.... */
    /*   ....#-####-#.... */
    /*   ....########.... */
    /*   .....##--##..... */
    /*   ......####...... */
    /*   ....##########.. */
    /*   ...############. */
    /*   ...##.######.##. */
    /*   ...##.######.##. */
    /*   ...##.######.##. */
    /*   .....##....##... */
    /*   .....##....##... */
    /*   .....##....##... */
    /*   ....###....###.. */
    { { 0x0000, 0x07E0, 0x0FF0, 0x0BD0, 0x0FF0, 0x0660, 0x03C0, 0x0FFC, 0x1FFE, 0x1BF6, 0x1BF6, 0x1BF6, 0x0618, 0x0618, 0x0618, 0x0E1C },
      { 0x0FF0, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FFE, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x0F3C, 0x1F3E, 0x1F3E } },
    /* ZOMBIE_FIRE  (halo) */
    /*   ................ */
    /*   .....######..... */
    /*   ....########.... */
    /*   ....#-####-#.... */
    /*   ....########.... */
    /*   .....##--##..... */
    /*   ......####...... */
    /*   ....##########.. */
    /*   ...############. */
    /*   ...##.#######... */
    /*   ...##.########.# */
    /*   ...##.######.### */
    /*   .....##....##.#. */
    /*   .....##....##... */
    /*   .....##....##... */
    /*   ....###....###.. */
    { { 0x0000, 0x07E0, 0x0FF0, 0x0BD0, 0x0FF0, 0x0660, 0x03C0, 0x0FFC, 0x1FFE, 0x1BF8, 0x1BFD, 0x1BF7, 0x061A, 0x0618, 0x0618, 0x0E1C },
      { 0x0FF0, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FFE, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x0F3F, 0x1F3E, 0x1F3E } },
    /* IMP_A  (halo) */
    /*   ...#........#... */
    /*   ...##......##... */
    /*   ....########.... */
    /*   ...##########... */
    /*   ...#-##..##-#... */
    /*   ...##########... */
    /*   ....##-##-##.... */
    /*   .....######..... */
    /*   ...##########... */
    /*   ..#.########.#.. */
    /*   ..#.########.#.. */
    /*   .#..########..#. */
    /*   ....##....##.... */
    /*   ....##....##.... */
    /*   ...###....###... */
    /*   ..###......###.. */
    { { 0x1008, 0x1818, 0x0FF0, 0x1FF8, 0x1668, 0x1FF8, 0x0DB0, 0x07E0, 0x1FF8, 0x2FF4, 0x2FF4, 0x4FF2, 0x0C30, 0x0C30, 0x1C38, 0x381C },
      { 0x3C3C, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x7FFE, 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF, 0x3E7C, 0x7E7E, 0x7E7E } },
    /* IMP_B  (halo) */
    /*   ...#........#... */
    /*   ...##......##... */
    /*   ....########.... */
    /*   ...##########... */
    /*   ...#-##..##-#... */
    /*   ...##########... */
    /*   ....##-##-##.... */
    /*   .....######..... */
    /*   ...##########... */
    /*   ..#.########.#.. */
    /*   .#..########..#. */
    /*   #...########...# */
    /*   .....##..##..... */
    /*   .....##..##..... */
    /*   ....###..###.... */
    /*   ...###....###... */
    { { 0x1008, 0x1818, 0x0FF0, 0x1FF8, 0x1668, 0x1FF8, 0x0DB0, 0x07E0, 0x1FF8, 0x2FF4, 0x4FF2, 0x8FF1, 0x0660, 0x0660, 0x0E70, 0x1C38 },
      { 0x3C3C, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF, 0xDFFB, 0x1FF8, 0x3FFC, 0x3FFC } },
    /* IMP_CLAW  (halo) */
    /*   .#..#........#.. */
    /*   .#..##......##.# */
    /*   .#...########..# */
    /*   ..#.##########.# */
    /*   ..#.#-##..##-#.# */
    /*   ...###########.. */
    /*   ....##-##-##.... */
    /*   .....######..... */
    /*   ...##########... */
    /*   ..############.. */
    /*   .#.##########.#. */
    /*   #...########...# */
    /*   ....##....##.... */
    /*   ....##....##.... */
    /*   ...###....###... */
    /*   ..###......###.. */
    { { 0x4804, 0x4C0D, 0x47F9, 0x2FFD, 0x2B35, 0x1FFC, 0x0DB0, 0x07E0, 0x1FF8, 0x3FFC, 0x5FFA, 0x8FF1, 0x0C30, 0x0C30, 0x1C38, 0x381C },
      { 0xFE1F, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFF, 0x7FFF, 0x3FFE, 0x3FFC, 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF, 0xDFFB, 0x3E7C, 0x7E7E, 0x7E7E } },
    /* CORPSE  (halo) */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ....##.......... */
    /*   ..####-####..... */
    /*   .#####-########. */
    /*   .##-##########-# */
    /*   ..############## */
    { { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0C00, 0x3DE0, 0x7DFE, 0x6FFD, 0x3FFF },
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1E00, 0x7FF0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF } },
    /* AMMO  (halo) */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ....########.... */
    /*   ...#-#-#-#-##... */
    /*   ...#-#-#-#-##... */
    /*   ...############. */
    /*   ...#---------#.. */
    /*   ...############. */
    /*   ................ */
    { { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0FF0, 0x1558, 0x1558, 0x1FFE, 0x1004, 0x1FFE, 0x0000 },
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FF8, 0x3FFC, 0x3FFC, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF } },
    /* MEDKIT  (halo) */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ................ */
    /*   ..############.. */
    /*   ..#----##----#.. */
    /*   ..#----##----#.. */
    /*   ..#--######--#.. */
    /*   ..#--######--#.. */
    /*   ..#----##----#.. */
    /*   ..#----##----#.. */
    /*   ..############.. */
    { { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3FFC, 0x2184, 0x2184, 0x27E4, 0x27E4, 0x2184, 0x2184, 0x3FFC },
      { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE } },
    /* PISTOL  (halo) */
    /*   ................ */
    /*   ......#..#...... */
    /*   ......####...... */
    /*   ......####...... */
    /*   ......####...... */
    /*   .....######..... */
    /*   .....#-##-#..... */
    /*   .....######..... */
    /*   .....######..... */
    /*   ....########.... */
    /*   ....#-####-#.... */
    /*   ....########.... */
    /*   ....#-####-#.... */
    /*   ....########.... */
    /*   ...##########... */
    /*   ...##########... */
    { { 0x0000, 0x0240, 0x03C0, 0x03C0, 0x03C0, 0x07E0, 0x05A0, 0x07E0, 0x07E0, 0x0FF0, 0x0BD0, 0x0FF0, 0x0BD0, 0x0FF0, 0x1FF8, 0x1FF8 },
      { 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x0FF0, 0x0FF0, 0x0FF0, 0x0FF0, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x1FF8, 0x3FFC, 0x3FFC, 0x3FFC } },
    /* FLASH */
    /*   .......##....... */
    /*   ..#....##....#.. */
    /*   ...#..####..#... */
    /*   ....########.... */
    /*   .##..######..##. */
    /*   ..############.. */
    /*   ...##########... */
    /*   #######--####### */
    /*   ...##########... */
    /*   ..############.. */
    /*   .##..######..##. */
    /*   ....########.... */
    /*   ...#..####..#... */
    /*   ..#....##....#.. */
    /*   .......##....... */
    /*   ................ */
    { { 0x0180, 0x2184, 0x13C8, 0x0FF0, 0x67E6, 0x3FFC, 0x1FF8, 0xFE7F, 0x1FF8, 0x3FFC, 0x67E6, 0x0FF0, 0x13C8, 0x2184, 0x0180, 0x0000 },
      { 0x0180, 0x2184, 0x13C8, 0x0FF0, 0x67E6, 0x3FFC, 0x1FF8, 0xFFFF, 0x1FF8, 0x3FFC, 0x67E6, 0x0FF0, 0x13C8, 0x2184, 0x0180, 0x0000 } },
    /* FACE */
    /*   ................ */
    /*   ................ */
    /*   ....########.... */
    /*   ...##########... */
    /*   ..############.. */
    /*   ..##-######-##.. */
    /*   ..#--######--#.. */
    /*   ..############.. */
    /*   ..####-##-####.. */
    /*   ..#####--#####.. */
    /*   ..############.. */
    /*   ..###------###.. */
    /*   ...##########... */
    /*   ....########.... */
    /*   ................ */
    /*   ................ */
    { { 0x0000, 0x0000, 0x0FF0, 0x1FF8, 0x3FFC, 0x37EC, 0x27E4, 0x3FFC, 0x3DBC, 0x3E7C, 0x3FFC, 0x381C, 0x1FF8, 0x0FF0, 0x0000, 0x0000 },
      { 0x0000, 0x0000, 0x0FF0, 0x1FF8, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x1FF8, 0x0FF0, 0x0000, 0x0000 } },
    /* FACE_HURT */
    /*   ................ */
    /*   ................ */
    /*   ....########.... */
    /*   ...##########... */
    /*   ..############.. */
    /*   ..#-#######-#... */
    /*   ..#-#--##--#-#.. */
    /*   ..############.. */
    /*   ..####-##-####.. */
    /*   ..####-##-####.. */
    /*   ..##-########-.. */
    /*   ..###-#--#-###.. */
    /*   ...####-#####... */
    /*   ....###-####.... */
    /*   .......-........ */
    /*   ................ */
    { { 0x0000, 0x0000, 0x0FF0, 0x1FF8, 0x3FFC, 0x2FE8, 0x2994, 0x3FFC, 0x3DBC, 0x3DBC, 0x37F8, 0x3A5C, 0x1EF8, 0x0EF0, 0x0000, 0x0000 },
      { 0x0000, 0x0000, 0x0FF0, 0x1FF8, 0x3FFC, 0x3FF8, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x1FF8, 0x0FF0, 0x0100, 0x0000 } }
};

/* ---- the world ----------------------------------------------------------- */
enum { T_ZOMBIE, T_IMP, T_AMMO, T_MEDKIT };
enum { S_IDLE, S_CHASE, S_ATTACK, S_PAIN, S_DEAD };      /* S_DEAD: a pickup, taken, too */

typedef struct { int16_t x, y, hp; uint8_t type, state, cd, anim; } thing_t;

static char     map[MH][MW];            /* lvl[] copied: doors open, so it changes */
static thing_t  th[NTH];
static int      nth;
static uint8_t  zbuf[VW];               /* nearest wall per column, 1/16 cell  */
static int32_t  px, py;                 /* player, Q8 cells                    */
static int      ang;                    /* player heading, 1/8192 turn         */
static int      hp, ammo, level, wave, kills, fire_cd, flash, hurt, exited;
static uint32_t tick;
static int32_t  score;

static int iabs(int v) { return v < 0 ? -v : v; }
static int solid(char c) { return c == '#' || c == '%' || c == 'D' || c == 'E'; }

static char cell_at(int32_t x, int32_t y)
{
    unsigned cx = (unsigned)(x >> 8), cy = (unsigned)(y >> 8);
    return (cx < MW && cy < MH) ? map[cy][cx] : '#';
}

static void load_map(void)
{
    nth = 0;
    for (int y = 0; y < MH; y++) {
        for (int x = 0; x < MW; x++) {
            char c = lvl[y][x];
            int type = c == 'z' ? T_ZOMBIE : c == 'i' ? T_IMP
                     : c == 'a' ? T_AMMO   : c == 'h' ? T_MEDKIT : -1;
            if (c == 'S') { px = (x << 8) + 128;  py = (y << 8) + 128; }
            if (type >= 0 && nth < NTH) {
                thing_t *t = &th[nth++];
                t->x = (int16_t)((x << 8) + 128);
                t->y = (int16_t)((y << 8) + 128);
                t->type = (uint8_t)type;  t->state = S_IDLE;  t->cd = t->anim = 0;
                t->hp = (int16_t)(type == T_ZOMBIE ? 20 + 5 * wave
                                : type == T_IMP    ? 40 + 5 * wave : 0);
            }
            map[y][x] = (type >= 0 || c == 'S') ? '.' : c;
        }
    }
    ang = 0;  fire_cd = flash = hurt = 0;               /* facing east */
}

static void doom_start(int lvl_)
{
    level = lvl_;  hp = 100;  ammo = 40;  score = 0;  wave = 0;  kills = 0;
    tick = 0;  exited = 0;
    load_map();
}

/* ============================================================================
 *  Walking into things
 * ============================================================================
 * A body is a square of half-width r.  It may stand where none of its four
 * corners is inside a solid cell.  Moving x and y separately lets it SLIDE
 * along a wall instead of sticking to it.  The player opens a door by
 * bumping any of its corners into one, and leaves the level the same way
 * through the exit; monsters treat both as walls. */
static int blocked(int32_t x, int32_t y, int r, int is_player)
{
    for (int k = 0; k < 4; k++) {
        int32_t cx = x + ((k & 1) ? r : -r), cy = y + ((k & 2) ? r : -r);
        char c = cell_at(cx, cy);
        if (!solid(c)) { continue; }
        if (is_player && c == 'D') {
            map[(cy >> 8)][(cx >> 8)] = '.';  sfx(SFX_DOOR);  continue;
        }
        if (is_player && c == 'E') { exited = 1; }
        return 1;
    }
    return 0;
}

static void walk(int32_t *x, int32_t *y, int32_t dx, int32_t dy, int r, int is_player)
{
    if (!blocked(*x + dx, *y, r, is_player)) { *x += dx; }
    if (!blocked(*x, *y + dy, r, is_player)) { *y += dy; }
}

/* ============================================================================
 *  The raycaster
 * ============================================================================
 * cast() walks the ray cell by cell.  `side` x/y is the distance the ray has
 * travelled when it next crosses a vertical/horizontal grid line; whichever is
 * smaller is the next line it meets.  ddx/ddy are the distances between
 * successive vertical/horizontal lines: 1/|rdx| and 1/|rdy| in Q8, because the
 * ray is scaled so its component along the view direction is exactly 1 - which
 * is what makes the result a perpendicular distance, not a straight-line one. */
static int hit_side, hit_u;
static char hit_c;

static int32_t cast(int32_t ox, int32_t oy, int32_t rdx, int32_t rdy)
{
    int mx = (int)(ox >> 8), my = (int)(oy >> 8);
    int32_t ddx = rdx ? 65536 / iabs((int)rdx) : 1 << 22;
    int32_t ddy = rdy ? 65536 / iabs((int)rdy) : 1 << 22;
    int sx = rdx < 0 ? -1 : 1, sy = rdy < 0 ? -1 : 1;
    int32_t sdx = (rdx < 0 ? (ox - (mx << 8)) : (((mx + 1) << 8) - ox)) * ddx >> 8;
    int32_t sdy = (rdy < 0 ? (oy - (my << 8)) : (((my + 1) << 8) - oy)) * ddy >> 8;
    int side = 0;
    for (int i = 0; i < MW + MH; i++) {
        if (sdx < sdy) { sdx += ddx;  mx += sx;  side = 0; }
        else           { sdy += ddy;  my += sy;  side = 1; }
        char c = ((unsigned)mx < MW && (unsigned)my < MH) ? map[my][mx] : '#';
        if (solid(c)) { hit_c = c;  break; }
    }
    int32_t perp = side ? sdy - ddy : sdx - ddx;
    if (perp < 16) { perp = 16; }
    /* where along the wall the ray struck, 0..15: which column of the texture */
    int32_t wx = side ? ox + (perp * rdx >> 8) : oy + (perp * rdy >> 8);
    hit_u = (int)((wx & 255) >> 4);
    hit_side = side;
    return perp;
}

/* Can (x0,y0) see (x1,y1)?  The same ray walk, along the segment between them
 * and scaled so that t = 1 (256) is the far end: if the first wall is at 1 or
 * beyond, nothing is in the way.  It must be the SAME walk the pistol uses:
 * an earlier version probed every quarter cell, and a pillar's corner thinner
 * than that slipped between two probes - so a zombie "saw" the player through
 * the corner while every bullet struck the pillar. */
int doom_los(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    return cast(x0, y0, x1 - x0, y1 - y0) >= 250;
}

/* One pixel of a wall: the texture says lit or dark, the dither thins it out
 * with distance.  u, v run 0..15 across and down one cell of wall. */
static int wall_px(char c, int u, int v, int shade, int x, int y)
{
    int lit;
    switch (c) {
    case '%':  lit = !(u > 2 && u < 13 && v > 3 && v < 12) || v == 7 || v == 8;  break;
    case 'D':  lit = (v & 3) != 0 && u != 0 && u != 15;                          break;
    case 'E':  lit = u < 2 || u > 13 || v < 2 || v > 13 || (u > 4 && u < 11 && v > 4 && v < 11); break;
    default:   lit = (v & 7) != 0 && ((u + (((v >> 3) & 1) << 3)) & 15) != 0;    break;   /* bricks */
    }
    if (!lit) { return 0; }
    switch (shade) {
    case 0:  return 1;
    case 1:  return (x & 1) || (y & 1);              /* 3 of 4 pixels */
    case 2:  return (x ^ y) & 1;                     /* 1 of 2        */
    default: return !(x & 1) && !(y & 1);            /* 1 of 4        */
    }
}

/* The floor: dots that thin out towards the horizon, so it reads as flat. */
static int floor_px(int x, int y)
{
    int r = y - HZ;
    if (r > 18) { return !(x & 3) && !(y & 1); }
    if (r > 9)  { return !(x & 7) && !(y & 3); }
    return 0;
}

static void draw_walls(int fx, int fy)
{
    int rpx = (-fy * PLANE) >> 8, rpy = (fx * PLANE) >> 8;     /* the camera plane */
    for (int c = 0; c < VW; c++) {
        int cam = (2 * c - (VW - 1)) * 2;                      /* -254..254 ~ -1..1 */
        int32_t perp = cast(px, py, fx + ((rpx * cam) >> 8), fy + ((rpy * cam) >> 8));
        zbuf[c] = perp >= 4080 ? 255 : (uint8_t)(perp >> 4);
        int wh = (int)(((int32_t)WALLK << 8) / perp);          /* wall height, px */
        if (wh < 1) { wh = 1; }
        int top = HZ - wh / 2, bot = top + wh;
        int32_t vstep = ((int32_t)16 << 12) / wh;              /* texture rows per px, Q12 */
        int shade = perp < 384 ? 0 : perp < 768 ? 1 : perp < 1280 ? 2 : 3;
        if (hit_side && shade < 3) { shade++; }                /* the other faces are darker */
        for (int y = FIELD_Y; y < OLED_H; y++) {
            int on;
            if (y < top)       { on = 0; }
            else if (y >= bot) { on = floor_px(c, y); }
            else               { on = wall_px(hit_c, hit_u, (int)(((y - top) * vstep) >> 12), shade, c, y); }
            oled_pixel(c, y, on ? OLED_ON : OLED_OFF);
        }
    }
}

/* ---- sprites: scaled with nearest-neighbour, hidden behind nearer walls --- */
static void blit16(int x, int y, int id)                       /* 1:1, for the weapon and the face */
{
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            if ((spr[id][1][r] >> (15 - c)) & 1) {
                oled_pixel(x + c, y + r, ((spr[id][0][r] >> (15 - c)) & 1) ? OLED_ON : OLED_OFF);
            }
        }
    }
}

static int frame_of(const thing_t *t, int i)
{
    switch (t->type) {
    case T_AMMO:   return SP_AMMO;
    case T_MEDKIT: return SP_MEDKIT;
    case T_ZOMBIE: return t->state == S_DEAD ? SP_CORPSE
                        : t->state == S_ATTACK && t->anim < 9 ? SP_ZOMBIE_FIRE : SP_ZOMBIE;
    default:       return t->state == S_DEAD ? SP_CORPSE
                        : t->state == S_ATTACK ? SP_IMP_CLAW
                        : ((tick >> 3) + (uint32_t)i) & 1 ? SP_IMP_B : SP_IMP_A;
    }
}

static void draw_things(int fx, int fy)
{
    int order[NTH], dep[NTH], n = 0;
    for (int i = 0; i < nth; i++) {
        if (th[i].type >= T_AMMO && th[i].state == S_DEAD) { continue; }    /* taken */
        int32_t dx = th[i].x - px, dy = th[i].y - py;
        int32_t d = (dx * fx + dy * fy) >> 8;                   /* how far ahead */
        if (d < 48 || d > 14 * 256) { continue; }
        int j = n++;                                            /* insertion sort, far first */
        while (j > 0 && dep[j - 1] < d) { dep[j] = dep[j - 1];  order[j] = order[j - 1];  j--; }
        dep[j] = (int)d;  order[j] = i;
    }
    for (int k = 0; k < n; k++) {
        const thing_t *t = &th[order[k]];
        int32_t d = dep[k];
        int32_t dx = t->x - px, dy = t->y - py;
        int32_t l = (dx * -fy + dy * fx) >> 8;                   /* how far to the right */
        int h = (int)(((int32_t)SPRK << 8) / d);
        int left = VW / 2 + (int)(l * 97 / d) - h / 2;           /* 97 = 64 / 0.66 */
        int bottom = HZ + (int)(((int32_t)WALLK << 8) / d) / 2;  /* where the floor is, there */
        int top = bottom - h;
        int fr = frame_of(t, order[k]);
        int32_t step = ((int32_t)16 << 8) / h;                   /* picture px per screen px */
        for (int c = left < 0 ? 0 : left; c < left + h && c < VW; c++) {
            if (zbuf[c] < (d >> 4)) { continue; }                /* a wall is nearer */
            int tx = (int)(((c - left) * step) >> 8);
            for (int y = top < FIELD_Y ? FIELD_Y : top; y < top + h && y < OLED_H; y++) {
                int ty = (int)(((y - top) * step) >> 8);
                if ((spr[fr][1][ty] >> (15 - tx)) & 1) {
                    oled_pixel(c, y, ((spr[fr][0][ty] >> (15 - tx)) & 1) ? OLED_ON : OLED_OFF);
                }
            }
        }
    }
}

static void doom_draw(int full)
{
    int a = ang >> 4;
    int fx = icos(a), fy = isin(a);
    draw_walls(fx, fy);
    draw_things(fx, fy);

    if (hurt) {                                          /* a red flash, in 1 bit: a frame */
        oled_rect(0, FIELD_Y, OLED_W, VH, OLED_ON);
        oled_rect(1, FIELD_Y + 1, OLED_W - 2, VH - 2, OLED_ON);
    }
    for (int k = 2; k <= 3; k++) {                       /* the crosshair, XOR: visible on lit and dark */
        oled_pixel(VW / 2 - k, HZ, OLED_XOR);  oled_pixel(VW / 2 + k, HZ, OLED_XOR);
        oled_pixel(VW / 2, HZ - k, OLED_XOR);  oled_pixel(VW / 2, HZ + k, OLED_XOR);
    }
    if (flash) { blit16(56, OLED_H - 16 - 13, SP_FLASH); }
    blit16(56, OLED_H - 16 + (flash ? 2 : 0), SP_PISTOL);

    char buf[12];
    oled_fill_rect(0, OLED_H - 17, 36, 17, OLED_OFF);              /* the face and its number */
    blit16(0, OLED_H - 16, (hurt || hp < 40) ? SP_FACE_HURT : SP_FACE);
    snprintf(buf, sizeof buf, "%d", hp < 0 ? 0 : hp);
    oled_text(17, OLED_H - 8, buf, OLED_ON);
    snprintf(buf, sizeof buf, "A%d", ammo);                        /* the ammunition */
    int w = 6 * (int)strlen(buf);
    oled_fill_rect(OLED_W - w - 2, OLED_H - 9, w + 2, 9, OLED_OFF);
    oled_text(OLED_W - w - 1, OLED_H - 8, buf, OLED_ON);

    hud("DOOM", score, (hp + 19) / 20, full);
}

/* ============================================================================
 *  The monsters
 * ============================================================================ */
static void hurt_player(int dmg)
{
    hp -= dmg;  hurt = 6;  sfx(SFX_HURT);
}

/* The distance to the player in Q8 cells, without a square root: the longer
 * leg plus half the shorter is within 12 % of the truth, and that is plenty
 * for "is it close enough to claw?". */
static int32_t dist_to(int32_t dx, int32_t dy)
{
    int ax = iabs((int)dx), ay = iabs((int)dy);
    return ax > ay ? ax + (ay >> 1) : ay + (ax >> 1);
}

static void resolve_attack(thing_t *t, int32_t dist)
{
    if (t->type == T_ZOMBIE) {
        /* a rifle shot: surer when near, and only if it can still see you */
        int chance = 85 - 8 * (int)(dist >> 8);
        if (chance < 20) { chance = 20; }
        if (doom_los(t->x, t->y, px, py) && (int)rng_below(100) < chance) {
            hurt_player(3 + level + (int)rng_below(4));
        }
    } else if (dist < 300) {                                       /* a claw: reach is 1.2 cells */
        hurt_player(5 + 2 * level + (int)rng_below(4));
    }
}

static void think(thing_t *t, int i)
{
    int32_t dx = px - t->x, dy = py - t->y;
    int32_t dist = dist_to(dx, dy);
    if (t->cd) { t->cd--; }
    switch (t->state) {
    case S_IDLE:
        if (dist < 9 * 256 && doom_los(t->x, t->y, px, py)) { t->state = S_CHASE; }
        return;
    case S_PAIN:
        if (--t->anim == 0) { t->state = S_CHASE; }
        return;
    case S_ATTACK:
        if (--t->anim == 6) { resolve_attack(t, dist); }
        if (t->anim == 0)   { t->state = S_CHASE; }
        return;
    default: break;
    }
    /* CHASE */
    if (t->type == T_ZOMBIE) {
        int seen = dist < 10 * 256 && doom_los(t->x, t->y, px, py);
        if (seen && dist > 2 * 256 && dist < 8 * 256 && t->cd == 0) {
            t->state = S_ATTACK;  t->anim = 12;  t->cd = (uint8_t)(60 + rng_below(50));
            return;
        }
        if (seen && dist < 3 * 256) { return; }                    /* stand and shoot */
    } else if (dist < 230) {                                       /* in claw range */
        if (t->cd == 0) { t->state = S_ATTACK;  t->anim = 12;  t->cd = 40; }
        return;
    }
    int spd = (t->type == T_IMP ? 7 : 4) + level + (wave > 3 ? 3 : wave);   /* Q8 per tick */
    int lead = iabs((int)dx) > iabs((int)dy) ? iabs((int)dx) : iabs((int)dy);
    if (lead == 0) { return; }
    int32_t x = t->x, y = t->y;
    walk(&x, &y, dx * spd / lead, dy * spd / lead, 77, 0);         /* the longer axis moves `spd` */
    t->x = (int16_t)x;  t->y = (int16_t)y;
    (void)i;
}

static void shoot(void)
{
    ammo--;  fire_cd = 10;  flash = 3;  sfx(SFX_SHOT);
    int a = ang >> 4, fx = icos(a), fy = isin(a);
    int32_t reach = cast(px, py, fx, fy);                      /* the wall straight ahead: shots stop there */
    int best = -1;
    for (int i = 0; i < nth; i++) {
        thing_t *t = &th[i];
        if (t->type > T_IMP || t->state == S_DEAD) { continue; }
        int32_t dx = t->x - px, dy = t->y - py;
        int32_t d = (dx * fx + dy * fy) >> 8;
        int32_t l = (dx * -fy + dy * fx) >> 8;
        if (d > 32 && d < reach && iabs((int)l) < 80) { best = i;  reach = d; }   /* 80 = 0.31 cell wide */
        if (t->state == S_IDLE && dist_to(dx, dy) < 7 * 256) { t->state = S_CHASE; }   /* the noise */
    }
    if (best < 0) { return; }
    thing_t *t = &th[best];
    t->hp = (int16_t)(t->hp - (8 + (int)rng_below(9)));      /* the pistol: 8..16 */
    sfx(SFX_HIT);
    if (t->hp <= 0) {
        t->state = S_DEAD;  kills++;
        score += (t->type == T_IMP ? 20 : 10) * level;
    } else {
        t->state = S_PAIN;  t->anim = 6;
    }
}

static int doom_step(const pad_t *in)
{
    tick++;
    if (hurt)    { hurt--; }
    if (flash)   { flash--; }
    if (fire_cd) { fire_cd--; }

    ang = (ang + in->x * 80 / 100 + TURN) % TURN;      /* full stick = 80/8192 turn a tick: 3.5 degrees */
    int a = ang >> 4;
    int32_t spd = (int32_t)in->y * 14 / 100;           /* full stick = 14/256 cell a tick: 2.7 cells/s  */
    walk(&px, &py, (icos(a) * spd) >> 8, (isin(a) * spd) >> 8, 64, 1);

    /* A is still down from pressing "play" in the menu: ignore it for a moment */
    if ((in->down & PAD_A) && fire_cd == 0 && tick > 12) {
        if (ammo > 0) { shoot(); } else { fire_cd = 25;  sfx(SFX_MOVE); }     /* *click* */
    }

    for (int i = 0; i < nth; i++) {
        thing_t *t = &th[i];
        if (t->state == S_DEAD) { continue; }
        if (t->type <= T_IMP) { think(t, i);  continue; }
        if (iabs(px - t->x) + iabs(py - t->y) < 100) {           /* a pickup: within 0.4 cell */
            if (t->type == T_AMMO && ammo < 99)      { ammo = ammo + 10 > 99 ? 99 : ammo + 10; }
            else if (t->type == T_MEDKIT && hp < 100) { hp = hp + 25 > 100 ? 100 : hp + 25; }
            else { continue; }
            t->state = S_DEAD;  score += 5;  sfx(SFX_EAT);
        }
    }

    if (exited) {                                        /* through the exit: the next, harder, wave */
        exited = 0;  wave++;
        score += 100 * level;
        int h = hp + 25, m = ammo + 15;
        load_map();
        hp = h > 100 ? 100 : h;  ammo = m > 99 ? 99 : m;
        sfx(SFX_LEVEL);
    }
    return hp <= 0;
}

static int32_t doom_score(void) { return score; }

const game_t game_doom = { "DOOM", doom_start, doom_step, doom_draw, doom_score };

/* ---- for the host test ---------------------------------------------------- */
void doom_peek(peek_t *p)
{
    int live = 0;
    for (int i = 0; i < nth; i++) { live += th[i].type <= T_IMP && th[i].state != S_DEAD; }
    p->x = (int)px;  p->y = (int)py;                    /* Q8 cells                          */
    p->vx = icos(ang >> 4);  p->vy = isin(ang >> 4);    /* the way it faces, Q8              */
    p->w = wave;  p->h = live;                          /* waves cleared; monsters alive     */
    p->count = kills;  p->lives = hp;  p->gap = ammo;
    p->tx = 23;  p->ty = 2;                             /* the exit switch                   */
}

int doom_cell(int cx, int cy)
{
    return ((unsigned)cx < MW && (unsigned)cy < MH) ? map[cy][cx] : '#';
}

/* Thing i: -1 past the end, 0 if dead or taken, else 1 + its type (1 zombie,
 * 2 imp, 3 ammo, 4 health) and its Q8 position. */
int doom_thing(int i, int *x, int *y)
{
    if (i >= nth) { return -1; }
    if (th[i].state == S_DEAD) { return 0; }
    *x = th[i].x;  *y = th[i].y;
    return 1 + th[i].type;
}
