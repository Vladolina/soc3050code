/*
 * prince.c - PRINCE.  Run, jump, climb, fight, escape.   PURE C.
 *
 * Jordan Mechner's Prince of Persia (1989) made a person move like a person
 * on a 1 MHz Apple II by refusing to use physics where a LOOKUP would do:
 * every movement was a short, hand-drawn animation, and the world was a
 * grid of tiles - 10 across, 3 down per screen.  This is that idea, small,
 * and original: the tiles, the figures and the code are this lesson's own.
 *
 * THE WORLD is 30 x 6 tiles, three screens ("rooms") across and two down,
 * one char per tile (world[] below).  A tile is 12 x 18 pixels with its floor
 * SLAB in the bottom three rows, so a figure 15 pixels tall stands in the
 * open air above a slab and its head just touches the slab overhead.  That
 * one choice is the whole of the geometry:
 *
 *     feet y = 18 * row + 15        the top of the slab it stands on
 *     a slab in the row above       is a CEILING: no jumping under it
 *     a '#' block                   stops walking, and so does a closed gate
 *     a missing slab ('.')          is a hole: walk onto it and you fall
 *
 * The screen follows the prince: cross a room's edge and the view flips to
 * the next room, as the original did.  There is no camera, no scrolling - the
 * room is just (x / 120, y / 54).
 *
 * THE MOVES.  Stick left/right runs; stick up jumps (a running jump if you
 * are running); A held while jumping grabs a ledge, and stick up then pulls
 * you onto it.  Falling one row is safe, two rows costs a heart, three is
 * death - counted in ROWS from where you left the ground, not in pixels,
 * so the arc of a jump never counts against you.  Spikes spring up when you
 * come near: jump them.  A loose floor tile drops half a second after you
 * step on it.  A pressure plate opens the gate for six seconds.  Near a
 * guard the prince draws his sword: A strikes, stick up parries, and a
 * parry is what opens the guard to your riposte.
 *
 * Position is Q4: sixteenths of a pixel, so a speed of 1.625 px a tick is
 * 26 and gravity is 3, with integers only (slide 18).  Movement then steps
 * one WHOLE pixel at a time while checking the tile - at most four steps a
 * tick - so nothing can tunnel through a slab 3 pixels thick.
 *
 * Drawing is incremental.  The room's tiles are kept in a shadow copy of
 * what was last drawn (a char and an animation phase per tile); a frame
 * repaints a tile only if the shadow differs or a figure moved across it,
 * then redraws the two figures.  A prince running along a floor dirties two
 * pages, not eight.
 *
 *   RAM: world 180 + shadow 60 + the rest = 400 bytes (measured).
 */
#include "engine.h"

#define TW      12                      /* a tile: 12 wide ...                  */
#define TH      18                      /* ... 18 tall, slab in rows 15..17     */
#define RC      10                      /* a room: 10 tiles across ...          */
#define RR      3                       /* ... 3 down                           */
#define WC      30                      /* the world: 3 rooms across ...        */
#define WR      6                       /* ... 2 down                           */
#define SX0     4                       /* screen x of room column 0            */
#define SY0     (FIELD_Y + 1)           /* screen y of room row 0               */
#define FH      15                      /* a figure's height                    */
#define HW      3                       /* half its width, for walls            */
#define G       3                       /* gravity, Q4 px per tick per tick     */
#define VMAX    56                      /* terminal velocity, 3.5 px a tick     */

/* '.' air  '=' floor  '#' block  'p' pillar  't' torch  '^' spikes  '~' loose floor
 * '_' plate  'G' gate  'X' exit  'h' potion  'g' guard  'S' start  (all but '.' and
 * '#' carry a floor slab) */
static const char world0[WR][WC + 1] = {
    "..............................",
    "..............................",
    ".............................X",
    "....................=h=g=_=G=#",
    "..............=t====##########",
    "=S==.==^=~=ph=################",
};

/* ---- the figures: 14 x (12 columns x 2 pages), facing right -------------- */
enum { F_STAND, F_RUN_A, F_RUN_B, F_JUMP, F_FALL, F_HANG, F_CLIMB, F_GARDE,
       F_STRIKE, F_PARRY, F_DEAD, F_G_STAND, F_G_STRIKE, F_G_DEAD, NFIG };

static const uint8_t fig[NFIG][24] = {
    /* frame 0: STAND, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ....###..... */
    /*   ..#######... */
    /*   .#.#####.#.. */
    /*   .#.#####.#.. */
    /*   .#.#####.#.. */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ..###.###... */
    { 0x00, 0xC0, 0x20, 0xE6, 0xFF, 0xFF, 0xFF, 0xE6, 0x20, 0xC0, 0x00, 0x00, 0x00, 0x01, 0x40, 0x7F, 0x7F, 0x03, 0x7F, 0x7F, 0x40, 0x01, 0x00, 0x00 },
    /* frame 1: RUN_A, 12 x 15 */
    /*   .....###.... */
    /*   ....#####... */
    /*   ....#####... */
    /*   .....###.... */
    /*   .....###.... */
    /*   ...######... */
    /*   ..#.####.#.. */
    /*   .#..####..#. */
    /*   .#..####.... */
    /*   ....####.... */
    /*   ....##.##... */
    /*   ...##...##.. */
    /*   ..##.....#.. */
    /*   .##......##. */
    /*   ###.....###. */
    { 0x00, 0x80, 0x40, 0x20, 0xE6, 0xFF, 0xFF, 0xFF, 0x26, 0x40, 0x80, 0x00, 0x40, 0x61, 0x70, 0x18, 0x0F, 0x07, 0x03, 0x07, 0x4C, 0x78, 0x60, 0x00 },
    /* frame 2: RUN_B, 12 x 15 */
    /*   .....###.... */
    /*   ....#####... */
    /*   ....#####... */
    /*   .....###.... */
    /*   .....###.... */
    /*   ...######... */
    /*   .#..####.#.. */
    /*   ..#.####..#. */
    /*   ....####.#.. */
    /*   ....####.... */
    /*   .....###.... */
    /*   .....###.... */
    /*   .....###.... */
    /*   .....###.... */
    /*   ....#####... */
    { 0x00, 0x40, 0x80, 0x20, 0xE6, 0xFF, 0xFF, 0xFF, 0x26, 0x40, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x7F, 0x7F, 0x7F, 0x40, 0x01, 0x00, 0x00 },
    /* frame 3: JUMP, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ..#.###.#... */
    /*   ..#######... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ..##...##... */
    /*   ..#.....#... */
    /*   .##.....##.. */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    { 0x00, 0x00, 0x30, 0xE6, 0xFF, 0xFF, 0xFF, 0xE6, 0x30, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0E, 0x03, 0x01, 0x01, 0x01, 0x03, 0x0E, 0x08, 0x00, 0x00 },
    /* frame 4: FALL, 12 x 15 */
    /*   ..#.###.#... */
    /*   ..#.###.#... */
    /*   ..#.###.#... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ..##...##... */
    /*   ..##...##... */
    { 0x00, 0x00, 0x07, 0xE8, 0xFF, 0xFF, 0xFF, 0xE8, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x7F, 0x1F, 0x03, 0x1F, 0x7F, 0x60, 0x00, 0x00, 0x00 },
    /* frame 5: HANG, 12 x 15 */
    /*   ..#.....#... */
    /*   ..##...##... */
    /*   ..#######... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ....###..... */
    /*   ....###..... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...#...#.... */
    { 0x00, 0x00, 0x07, 0x7E, 0xFC, 0xFC, 0xFC, 0x7E, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x3F, 0x03, 0x3F, 0x7C, 0x00, 0x00, 0x00, 0x00 },
    /* frame 6: CLIMB, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   .##.###.##.. */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ..###.###... */
    /*   ............ */
    { 0x00, 0x10, 0x10, 0xE6, 0xFF, 0xFF, 0xFF, 0xE6, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x20, 0x3F, 0x3F, 0x01, 0x3F, 0x3F, 0x20, 0x00, 0x00, 0x00 },
    /* frame 7: GARDE, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ....###..... */
    /*   ..#######..# */
    /*   .#.#####.#.# */
    /*   .#.#####..#. */
    /*   .#.#####.#.. */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ..##...##... */
    /*   .##.....##.. */
    /*   .##.....###. */
    { 0x00, 0xC0, 0x20, 0xE6, 0xFF, 0xFF, 0xFF, 0xE6, 0x20, 0x40, 0x80, 0x60, 0x00, 0x61, 0x70, 0x1F, 0x0F, 0x03, 0x0F, 0x1F, 0x70, 0x61, 0x40, 0x00 },
    /* frame 8: STRIKE, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ....###..... */
    /*   ..#########. */
    /*   .#.#####.### */
    /*   .#.#####.#.. */
    /*   .#.#####.... */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ..##...##... */
    /*   .##.....##.. */
    /*   .##.....##.. */
    /*   .##.....###. */
    { 0x00, 0xC0, 0x20, 0xE6, 0xFF, 0xFF, 0xFF, 0xE6, 0x20, 0xE0, 0x60, 0x40, 0x00, 0x71, 0x78, 0x0F, 0x07, 0x03, 0x07, 0x0F, 0x78, 0x70, 0x40, 0x00 },
    /* frame 9: PARRY, 12 x 15 */
    /*   ....###..#.. */
    /*   ...#####.#.. */
    /*   ...#####.#.. */
    /*   ....###..#.. */
    /*   ....###.##.. */
    /*   ..#######... */
    /*   .#.#####.... */
    /*   .#.#####.... */
    /*   .#.#####.... */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ..##...##... */
    /*   .##.....##.. */
    /*   .##.....###. */
    { 0x00, 0xC0, 0x20, 0xE6, 0xFF, 0xFF, 0xFF, 0xE6, 0x30, 0x1F, 0x00, 0x00, 0x00, 0x61, 0x70, 0x1F, 0x0F, 0x03, 0x0F, 0x1F, 0x70, 0x60, 0x40, 0x00 },
    /* frame 10: DEAD, 12 x 15 */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   .##.....#... */
    /*   ######.##### */
    /*   .########### */
    /*   .##.######## */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x78, 0x78, 0x30, 0x70, 0x70, 0x60, 0x70, 0x78, 0x70, 0x70, 0x70 },
    /* frame 11: G_STAND, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ..#######... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ..#######..# */
    /*   .##.###.##.# */
    /*   .#.#####.#.# */
    /*   .#.#####..#. */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ...##.##.... */
    /*   ..###.###... */
    { 0x00, 0xC0, 0x64, 0xAE, 0xFF, 0xFF, 0xFF, 0xAE, 0x64, 0xC0, 0x00, 0xE0, 0x00, 0x01, 0x40, 0x7F, 0x7F, 0x07, 0x7F, 0x7F, 0x40, 0x00, 0x01, 0x00 },
    /* frame 12: G_STRIKE, 12 x 15 */
    /*   ....###..... */
    /*   ...#####.... */
    /*   ..#######... */
    /*   ...#####.... */
    /*   ....###..... */
    /*   ..#########. */
    /*   .##.###.#### */
    /*   .#.#####.#.. */
    /*   .#.#####.... */
    /*   ...#####.... */
    /*   ...#####.... */
    /*   ...##.##.... */
    /*   ..##...##... */
    /*   .##.....##.. */
    /*   .##.....###. */
    { 0x00, 0xC0, 0x64, 0xAE, 0xFF, 0xFF, 0xFF, 0xAE, 0x64, 0xE0, 0x60, 0x40, 0x00, 0x61, 0x70, 0x1F, 0x0F, 0x07, 0x0F, 0x1F, 0x70, 0x60, 0x40, 0x00 },
    /* frame 13: G_DEAD, 12 x 15 */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   ............ */
    /*   .##....##... */
    /*   ######.##### */
    /*   .########### */
    /*   .#.#.####### */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x78, 0x38, 0x70, 0x30, 0x70, 0x60, 0x78, 0x78, 0x70, 0x70, 0x70 }
};

enum { P_STAND, P_RUN, P_JUMP, P_FALL, P_HANG, P_CLIMB, P_FIGHT, P_STRIKE, P_PARRY, P_DEAD };
enum { G_SLEEP, G_FIGHT, G_STRIKE, G_BLOCK, G_DEAD };

static char     w[WR][WC];              /* the world, copied: floors fall, potions get drunk */
static int32_t  px, py;                 /* the prince: centre x and FEET y, Q4 world pixels  */
static int32_t  vx, vy;                 /* Q4 pixels per tick                                */
static int      face = 1, st, st_t, hp, level, wave, fall_row, up_was, hurt_t, rooms_seen;
static int      climb_c, climb_k, exited, kills;
static int      gate_open, gate_t, spk, plate_on, loose_c, loose_r, loose_t;
static int      gx, gy, gst, ghp, gface, gt, gcd;             /* the guard: px, feet px, ... */
static uint32_t tick;
static int32_t  score;

/* what the screen last showed */
static int      shown_room = -1, need_full;
static int      sh_px, sh_py, sh_gx, sh_gy;
static char     sh_ch[RR][RC];
static uint8_t  sh_an[RR][RC];

static int iabs(int v) { return v < 0 ? -v : v; }
static int sgn(int v)  { return v < 0 ? -1 : 1; }

static char tile(int c, int r)
{
    if ((unsigned)c >= WC || r < 0) { return '#'; }                 /* the world's walls  */
    return r >= WR ? '.' : w[r][c];                                 /* below it: nothing  */
}
static int floorish(char c) { return c != '.' && c != '#'; }       /* has a slab         */
static int blocks(int c, int r)                                     /* stops a walker     */
{
    char t = tile(c, r);
    return t == '#' || (t == 'G' && gate_open < 10);
}

static int  X(void)    { return (int)(px >> 4); }
static int  Y(void)    { return (int)(py >> 4); }
static int  room_of(int x, int y) { return ((y - 8) / TH / RR) * 3 + x / TW / RC; }

static void load_world(void)
{
    int sx = 1, sy = 5;
    for (int r = 0; r < WR; r++) {
        for (int c = 0; c < WC; c++) {
            char t = world0[r][c];
            if (t == 'S') { sx = c;  sy = r;  t = '='; }
            if (t == 'g') { gx = c * TW + 6;  gy = r * TH + 15;  t = '='; }
            w[r][c] = t;
        }
    }
    px = (int32_t)(sx * TW + 6) << 4;  py = (int32_t)(sy * TH + 15) << 4;
    vx = vy = 0;  face = 1;  st = P_STAND;  st_t = 0;  hurt_t = 0;  up_was = 0;
    gst = G_SLEEP;  gface = -1;  gt = gcd = 0;
    ghp = 2 + (level + 1) / 2 + (wave > 2 ? 2 : wave);
    gate_open = gate_t = spk = plate_on = loose_t = 0;  exited = 0;
    rooms_seen = 1 << room_of(X(), Y());
    need_full = 1;
}

static void prince_start(int lvl)
{
    level = lvl;  hp = 3;  score = 0;  wave = 0;  kills = 0;  tick = 0;
    load_world();
}

/* ============================================================================
 *  Moving: one pixel at a time
 * ============================================================================ */
/* Would the body, centred at x with its feet at y, be inside a wall?  Sampled
 * at the feet, the waist and the head, on the side it is moving toward. */
static int wall_at(int x, int y, int dir)
{
    int c = (x + dir * HW) / TW;
    return blocks(c, (y - 1) / TH) || blocks(c, (y - 8) / TH) || blocks(c, (y - FH) / TH);
}

static void walk_x(int32_t dx)
{
    int32_t nx = px + dx;
    int xi = (int)(nx >> 4);
    if (xi < HW || xi > WC * TW - HW || wall_at(xi, Y(), sgn((int)dx))) { vx = 0;  return; }
    px = nx;
}

static void hurt(int n)
{
    if (hurt_t) { return; }
    hp -= n;  hurt_t = 16;  sfx(SFX_HURT);                 /* 16 ticks of grace: shorter than the guard's fastest pause (20), or a second blade would be swallowed */
    if (hp <= 0) { hp = 0;  st = P_DEAD;  st_t = 0;  vx = 0; }
}

static void die(void)
{
    hp = 0;  st = P_DEAD;  st_t = 0;  vx = 0;  sfx(SFX_HURT);
}

/* What a drop costs, in hearts: 0 for a row, 1 for two, death for three. */
int prince_fall_cost(int rows) { return rows >= 3 ? 99 : rows == 2 ? 1 : 0; }

/* Down to a floor.  fall_row is the row it left, so the arc of a jump counts
 * for nothing: only how many floors lower it ends up. */
static void land(int row)
{
    int rows = row - fall_row, cost = prince_fall_cost(rows);
    vy = 0;  vx = 0;  st = P_STAND;  st_t = 0;
    if (cost >= 99) { die();  return; }
    if (cost) { hurt(cost); }
    if (rows >= 1) { sfx(SFX_THUD); }
}

/* One tick in the air: sideways, then up or down a whole pixel at a time. */
static void fly(void)
{
    vy += G;  if (vy > VMAX) { vy = VMAX; }
    if (vx) { walk_x(vx); }
    int32_t ny = py + vy;
    int steps = (int)(ny >> 4) - Y();
    int xc = X() / TW;
    while (steps > 0) {                                     /* falling */
        int y = Y() + 1;
        if (y % TH == 15 && floorish(tile(xc, y / TH))) { py = (int32_t)y << 4;  land(y / TH);  return; }
        if (y > WR * TH + 40) { die();  return; }           /* into the abyss */
        py = (int32_t)y << 4;  steps--;
    }
    while (steps < 0) {                                     /* rising: the head meets a ceiling slab */
        int top = Y() - FH - 1;
        if (top < 0 || (top % TH >= 15 && floorish(tile(xc, top / TH)))) { vy = 0;  break; }
        py = (int32_t)(Y() - 1) << 4;  steps++;
    }
    st = vy < 0 ? P_JUMP : P_FALL;
}

/* Is there a ledge to grab?  Hands are at the head; a slab's top surface is
 * at 18k + 15.  The ledge must be next to us, with open air above it to stand
 * in, and no slab where we are, or we could not have risen to it. */
static int try_grab(void)
{
    int x = X(), c = x / TW, yh = Y() - FH;
    int edge = face > 0 ? (c + 1) * TW : c * TW;
    if (iabs(x - edge) > 5) { return 0; }
    for (int k = 0; k < WR; k++) {
        int top = k * TH + 15;
        if (yh < top - 8 || yh > top + 4) { continue; }
        if (floorish(tile(c + face, k)) && !floorish(tile(c, k)) && tile(c, k) != '#'
            && tile(c + face, k - 1) == '.') {
            px = (int32_t)(face > 0 ? edge - 4 : edge + 4) << 4;
            py = (int32_t)(top + FH) << 4;
            vx = vy = 0;  st = P_HANG;  st_t = 0;  climb_c = c + face;  climb_k = k;
            return 1;
        }
    }
    return 0;
}

/* ============================================================================
 *  The guard, and the sword
 * ============================================================================ */
static int guard_awake(void) { return gst != G_SLEEP && gst != G_DEAD; }

static void guard_think(int d, int same_row)
{
    if (gst == G_DEAD) { return; }
    if (gt) { gt--; }
    if (gcd) { gcd--; }
    if (gst == G_SLEEP) {
        if (same_row && d < 60 && st != P_DEAD) { gst = G_FIGHT;  gface = X() > gx ? 1 : -1;  sfx(SFX_SWORD); }
        return;
    }
    if (st == P_DEAD) { return; }
    if (gst == G_STRIKE) {
        if (gt == 8 && same_row && d <= 22) {                   /* the blade lands 14 ticks (0.28 s) after it is raised: a parry can be reacted to */
            if (st == P_PARRY) { sfx(SFX_SWORD);  gcd += 25; }
            else               { hurt(1);  px += (int32_t)(-sgn(gx - X()) * 4) << 4; }
        }
        if (gt == 0) { gst = G_FIGHT; }
        return;
    }
    if (gst == G_BLOCK) { if (gt == 0) { gst = G_FIGHT; }  return; }
    gface = X() > gx ? 1 : -1;
    if ((tick & 1) == 0) {                                      /* half a pixel a tick: close up or give ground */
        int nx = gx + (d > 24 ? gface : d < 12 ? -gface : 0);
        if (nx != gx && floorish(tile(nx / TW, gy / TH)) && !blocks((nx + gface * HW) / TW, gy / TH - 0)) { gx = nx; }
    }
    if (gcd == 0 && d <= 26 && same_row) {
        if ((int)rng_below(100) < 30) { gst = G_BLOCK;  gt = 14; }        /* a steady 30 % at every level ...  */
        else                          { gst = G_STRIKE; gt = 22; }
        gcd = 56 - 8 * level;  if (gcd < 20) { gcd = 20; }              /* ... but a shorter pause between attacks */
    }
}

/* The prince's blade, on the fourth tick of a strike. */
static void prince_strike(int d)
{
    if (d > 22 || gst == G_DEAD || gst == G_SLEEP) { return; }
    if (gst == G_BLOCK) { sfx(SFX_SWORD);  return; }
    ghp--;  sfx(SFX_HIT);
    {
        int nx = gx + sgn(gx - X()) * 5;
        if (floorish(tile(nx / TW, gy / TH)) && !blocks(nx / TW, gy / TH)) { gx = nx; }
    }
    if (ghp <= 0) { gst = G_DEAD;  kills++;  score += 100 * level; }
    /* A hit does NOT cancel a blade already raised: an earlier version set the
     * guard back to G_FIGHT here, and tapping A every ten ticks then locked him
     * out of ever attacking - parrying was optional and the levels were noise. */
}

/* ============================================================================
 *  One tick
 * ============================================================================ */
static void ground_effects(int xc, int r)
{
    char t = tile(xc, r);
    if (t == '^' && spk >= 4) { die();  return; }                       /* impaled */
    if (t == '~' && loose_t == 0) { loose_c = xc;  loose_r = r;  loose_t = 1; }
    if (t == '_') { plate_on = 1;  gate_t = 300; }
    if (t == 'h') { w[r][xc] = '=';  if (hp < 5) { hp++; }  score += 25;  sfx(SFX_EAT); }
    if (t == 'X') { exited = 1; }
}

static int prince_step(const pad_t *in)
{
    tick++;
    if (hurt_t) { hurt_t--; }
    int left = in->x < -35, right = in->x > 35, up = in->y > 50, down = in->y < -50;
    int up_edge = up && !up_was;  up_was = up;
    int hold = (in->down & PAD_A) != 0, strike = (in->pressed & PAD_A) != 0;
    int xc = X() / TW, r = Y() / TH;

    /* the world's own motion: spikes, the loose floor, the gate */
    int near_spikes = 0;
    for (int dc = -2; dc <= 2; dc++) { near_spikes |= tile(xc + dc, r) == '^'; }
    spk += near_spikes ? (spk < 8 ? 2 : 0) : (spk > 0 ? -2 : 0);
    if (loose_t && ++loose_t > 25) {
        w[loose_r][loose_c] = '.';  loose_t = 0;  sfx(SFX_THUD);
    }
    plate_on = 0;
    if (gate_t) { gate_t--;  if ((tick & 1) == 0 && gate_open < 14) { gate_open++; } }
    else if (gate_open > 0 && tile(xc, r) != 'G') { gate_open--; }

    /* the guard */
    int d = iabs(X() - gx), same_row = (gy / TH == r) && st != P_JUMP && st != P_FALL;
    guard_think(d, same_row);
    int fighting = guard_awake() && same_row && d < 70 && gy / TH == r;

    switch (st) {
    case P_STAND: case P_RUN: {
        if (!floorish(tile(xc, r))) { fall_row = r;  vx = face * 8;  vy = 0;  st = P_FALL;  break; }
        if (fighting) { st = P_FIGHT;  st_t = 0;  break; }
        if (up_edge) {                                          /* jump */
            fall_row = r;
            if (left || right) { vx = face * 26;  vy = -30; } else { vx = 0;  vy = -46; }
            st = P_JUMP;  sfx(SFX_FLAP);  fly();  break;
        }
        st = P_STAND;
        if (left || right) { face = left ? -1 : 1;  walk_x(face * 16);  st = P_RUN; }
        xc = X() / TW;
        ground_effects(xc, r);
        break;
    }
    case P_JUMP: case P_FALL:
        if (hold && try_grab()) { break; }
        fly();
        break;
    case P_HANG:
        if (up) { st = P_CLIMB;  st_t = 0; }
        else if (!hold || down) { fall_row = Y() / TH;  vx = vy = 0;  st = P_FALL; }
        break;
    case P_CLIMB:                                               /* up 15 px, then 10 px onto the ledge */
        if (st_t < 8) { py -= 30; }
        else          { px += (int32_t)face * 20; }
        if (++st_t == 16) {
            px = (int32_t)(climb_c * TW + 6) << 4;  py = (int32_t)(climb_k * TH + 15) << 4;
            st = P_STAND;  st_t = 0;
        }
        break;
    case P_FIGHT: case P_STRIKE: case P_PARRY:
        if (!fighting && st == P_FIGHT) { st = P_STAND;  break; }
        if (!floorish(tile(xc, r))) { fall_row = r;  vx = 0;  vy = 0;  st = P_FALL;  break; }
        face = gx > X() ? 1 : -1;
        if (st == P_STRIKE) {
            if (++st_t == 4) { prince_strike(d); }
            if (st_t >= 10) { st = P_FIGHT;  st_t = 0; }
            break;
        }
        st = up ? P_PARRY : P_FIGHT;
        if (strike && st == P_FIGHT) { st = P_STRIKE;  st_t = 0;  break; }
        if ((left || right) && st == P_FIGHT) { walk_x((left ? -1 : 1) * 11); }
        ground_effects(X() / TW, r);
        break;
    case P_DEAD:                                                /* a short collapse, then the arcade's game-over box */
        st_t++;
        break;
    }

    /* a room seen for the first time is worth something */
    int rm = room_of(X(), Y());
    if (rm >= 0 && rm < 6 && !(rooms_seen & (1 << rm))) { rooms_seen |= 1 << rm;  score += 10; }

    if (exited) {                                               /* through the door: the next, harder, wave */
        int h = hp;
        score += 200 * level;  wave++;
        load_world();
        hp = h < 5 ? h + 1 : 5;
        sfx(SFX_LEVEL);
    }
    return st == P_DEAD && st_t > 60;
}

/* ============================================================================
 *  Drawing
 * ============================================================================ */
static void blit(int x, int y, int id, int dir)
{
    for (int c = 0; c < 12; c++) {
        int sc = dir > 0 ? c : 11 - c;
        unsigned col = fig[id][sc] | ((unsigned)fig[id][12 + sc] << 8);
        for (int r = 0; r < FH; r++) {
            if (((col >> r) & 1) && y + r >= FIELD_Y) { oled_pixel(x + c, y + r, OLED_ON); }
        }
    }
}

/* One tile, from its char and its animation phase.  x0, y0: its top-left. */
static void draw_tile(char t, int an, int x0, int y0)
{
    oled_fill_rect(x0, y0, TW, TH, OLED_OFF);
    if (t == '#') {                                             /* bricks: three courses, staggered joints */
        for (int k = 0; k < 4; k++) { oled_hline(x0, y0 + (k == 3 ? 17 : k * 6), TW, OLED_ON); }
        for (int k = 0; k < 3; k++) {
            for (int j = (k & 1) ? 3 : 0; j < TW; j += 6) { oled_vline(x0 + j, y0 + k * 6 + 1, 5, OLED_ON); }
        }
        return;
    }
    if (t == '.') { return; }
    /* everything else stands on a slab */
    oled_hline(x0, y0 + 15, TW, OLED_ON);
    oled_hline(x0, y0 + 17, TW, OLED_ON);
    oled_pixel(x0, y0 + 16, OLED_ON);  oled_pixel(x0 + 6, y0 + 16, OLED_ON);
    switch (t) {
    case 'p': oled_rect(x0 + 3, y0, 6, 15, OLED_ON);  oled_hline(x0 + 2, y0, 8, OLED_ON);  break;
    case 't':                                                   /* a torch: the flame flickers by phase */
        oled_vline(x0 + 5, y0 + 6, 5, OLED_ON);
        oled_vline(x0 + 5 + (an == 1) - (an == 3), y0 + 2 + (an & 1), 4, OLED_ON);
        oled_pixel(x0 + 4 + an % 3, y0 + 3, OLED_ON);
        break;
    case '^':                                                   /* four spikes, `an` pixels tall */
        for (int k = 0; k < 4; k++) { oled_vline(x0 + 1 + k * 3, y0 + 14 - an, an + 1, OLED_ON); }
        break;
    case '~':                                                   /* a cracked slab: the top edge broken, a wobble when it goes */
        oled_hline(x0 + 5, y0 + 15, 2, OLED_OFF);
        oled_pixel(x0 + 4, y0 + 16, OLED_OFF);  oled_pixel(x0 + 7, y0 + 16, OLED_OFF);
        if (an) { oled_hline(x0 + 5, y0 + 15, 2, OLED_ON); oled_pixel(x0 + 5, y0 + 14, OLED_ON); }
        break;
    case '_': oled_fill_rect(x0 + 3, y0 + 14 - (an ? 0 : 2), 6, an ? 1 : 3, OLED_ON);  break;   /* a plate, pressed or not */
    case 'G':                                                   /* bars rise as the gate opens: an = pixels opened */
        for (int k = 0; k < 4; k++) { oled_vline(x0 + 1 + k * 3, y0, 15 - an, OLED_ON); }
        oled_hline(x0, y0, TW, OLED_ON);
        break;
    case 'X':                                                   /* a doorway with a lintel and steps */
        oled_rect(x0 + 1, y0 + 1, 10, 14, OLED_ON);
        oled_hline(x0 + 2, y0 + 4, 8, OLED_ON);  oled_hline(x0 + 2, y0 + 7, 8, OLED_ON);
        oled_hline(x0 + 2, y0 + 10, 8, OLED_ON);  oled_hline(x0 + 2, y0 + 13, 8, OLED_ON);
        break;
    case 'h':                                                   /* a potion: a flask on the slab */
        oled_fill_rect(x0 + 4, y0 + 10, 5, 5, OLED_ON);  oled_rect(x0 + 5, y0 + 7, 3, 3, OLED_ON);
        oled_pixel(x0 + 6, y0 + 12, OLED_OFF);
        break;
    default: break;
    }
}

static int anim_of(char t)
{
    switch (t) {
    case 't': return (int)((tick >> 3) & 3);
    case '^': return spk;
    case 'G': return gate_open;
    case '_': return plate_on;
    case '~': return loose_t > 10 ? (int)(loose_t & 1) : 0;
    default:  return 0;
    }
}

static int prince_frame(void)
{
    switch (st) {
    case P_RUN:    return ((X() / 5) & 1) ? F_RUN_B : F_RUN_A;
    case P_JUMP:   return F_JUMP;
    case P_FALL:   return F_FALL;
    case P_HANG:   return F_HANG;
    case P_CLIMB:  return F_CLIMB;
    case P_FIGHT:  return F_GARDE;
    case P_STRIKE: return st_t >= 2 && st_t <= 8 ? F_STRIKE : F_GARDE;
    case P_PARRY:  return F_PARRY;
    case P_DEAD:   return F_DEAD;
    default:       return F_STAND;
    }
}

static void prince_draw(int full)
{
    int rm = room_of(X(), Y()), rx = (rm % 3) * RC * TW, ry = (rm / 3) * RR * TH;
    int whole = full || need_full || rm != shown_room;
    if (whole) {
        oled_fill_rect(0, FIELD_Y, OLED_W, OLED_H - FIELD_Y, OLED_OFF);
        shown_room = rm;  need_full = 0;
    }
    int pxs = SX0 + X() - rx - 6, pys = SY0 + Y() - ry - FH;           /* the prince's box, on screen */
    int in_room = gx >= rx && gx < rx + RC * TW && gy > ry && gy <= ry + RR * TH + 15;
    int gxs = SX0 + gx - rx - 6, gys = SY0 + gy - ry - FH;

    for (int r = 0; r < RR; r++) {
        for (int c = 0; c < RC; c++) {
            char t = w[(ry / TH) + r][(rx / TW) + c];
            int an = anim_of(t), x0 = SX0 + c * TW, y0 = SY0 + r * TH;
            /* a figure's old or new box crosses this tile? */
            int hit = aabb(x0, y0, TW, TH, sh_px, sh_py, 12, FH) || aabb(x0, y0, TW, TH, pxs, pys, 12, FH)
                   || (in_room && (aabb(x0, y0, TW, TH, sh_gx, sh_gy, 12, FH) || aabb(x0, y0, TW, TH, gxs, gys, 12, FH)));
            if (whole || hit || t != sh_ch[r][c] || an != sh_an[r][c]) {
                draw_tile(t, an, x0, y0);
                sh_ch[r][c] = t;  sh_an[r][c] = (uint8_t)an;
            }
        }
    }
    if (in_room) { blit(gxs, gys, gst == G_DEAD ? F_G_DEAD : gst == G_STRIKE ? F_G_STRIKE : F_G_STAND, gface); }
    if (!(hurt_t & 4) || st == P_DEAD) { blit(pxs, pys, prince_frame(), face); }       /* blinks when hurt */
    sh_px = pxs;  sh_py = pys;  sh_gx = gxs;  sh_gy = gys;
    hud("PRINCE", score, hp, full);
}

static int32_t prince_score(void) { return score; }

const game_t game_prince = { "PRINCE", prince_start, prince_step, prince_draw, prince_score };

/* ---- for the host test ---------------------------------------------------- */
void prince_peek(peek_t *p)
{
    p->x = X();  p->y = Y();  p->vx = (int)vx;  p->vy = (int)vy;
    p->w = guard_awake() ? iabs(X() - gx) : 999;  p->h = gst == G_DEAD ? 0 : ghp;
    p->count = room_of(X(), Y());  p->lives = hp;  p->gap = st;
    p->tx = face;  p->ty = gst;
}

int prince_tile(int tx, int ty) { return tile(tx, ty); }
