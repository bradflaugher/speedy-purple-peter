/* sim.h - SPEEDY PURPLE PETER simulation (portable C: SDCC for the Game Boy, gcc for tests).
 *
 * One call to sim_step() is one 60 Hz frame. A run is a pure function of the seed and the
 * input sequence: no clocks, no hidden randomness. The whole state is one plain struct, so the
 * host tools can copy it to search ahead (the test bot) and the ROM tests can read it by symbol.
 *
 * Units. Positions are pixels (x wraps at 65536: compare with (int16_t)(a - b)), y grows down,
 * row r of the level spans y = 16r .. 16r+15. Peter's speeds are in 1/4096 px per frame, the
 * same resolution the 1985 original uses (its "0x01900" = 0x1900 here), so the physics constants
 * below are exactly the classic ones. Enemies and items use 1/256 px per frame. */
#ifndef SPP_SIM_H
#define SPP_SIM_H

#include <stdint.h>
#include "level.h"

/* On the Game Boy the simulation lives in switchable ROM banks: its entry points are banked. */
#if defined(__SDCC) && defined(__PORT_sm83)
#define SIM_BANKED __banked
#else
#define SIM_BANKED
#endif

/* joypad bits (the same as GBDK's J_*) */
#define K_RIGHT  0x01
#define K_LEFT   0x02
#define K_UP     0x04
#define K_DOWN   0x08
#define K_A      0x10
#define K_B      0x20
#define K_SELECT 0x40
#define K_START  0x80

/* ---- Peter's physics (1/4096 px per frame, per frame) ---- */
#define PH_MIN_WALK     0x0130
#define PH_WALK_ACC     0x0098
#define PH_RUN_ACC      0x00E4
#define PH_RELEASE_DEC  0x00D0
#define PH_SKID_DEC     0x01A0
#define PH_SKID_TURN    0x0900
#define PH_MAX_WALK     0x1900
#define PH_MAX_RUN      0x2900
#define PH_AIR_BACK_FAST 0x1D00   /* takeoff speed at or above which backward air control is strong */
#define PH_MAX_FALL     0x4800
#define PH_FALL_RESET   0x4000
#define PH_STOMP_VY     (-0x4000)
#define PH_RUN_MEMORY   10         /* frames B keeps counting after release */

#define LV_COLS   32               /* the level ring (columns kept in memory) */
#define VIEW_W    160
#define VIEW_H    128              /* the playfield under the 16 px HUD */
#define CAM_LEAD  64               /* Peter's screen x where scrolling starts */
#define CAM_Y_MAX 80               /* (13 rows * 16) - 128 */

#define MAX_ENTS  6
#define MAX_SHOTS 2
#define MAX_FX    8
#define MAX_DIRTY 8

#define TIME_START   300
#define TIME_FRAMES  24            /* frames per time unit */
#define NOVA_FRAMES  640
#define HURT_FRAMES  120
#define GROW_FRAMES  48
#define START_LIVES  3

/* Peter's power */
enum { PW_SMALL, PW_BIG, PW_BLASTER };

/* Peter's state */
enum { PS_PLAY, PS_GROW, PS_SHRINK, PS_DEAD, PS_OVER };

/* entity kinds */
enum {
    E_NONE, E_GLOOP, E_DOME, E_DOME_RED, E_JET, E_SHELL, E_SHELL_RED, E_CHOMP, E_COMET, E_CANNON,
    E_CELL, E_BLASTER, E_NOVA, E_1UP
};
/* entity states */
enum { ES_LIVE, ES_FLAT, ES_FALL, ES_SPROUT };

/* fx kinds (visual only) */
enum { FX_NONE, FX_COIN, FX_SCORE, FX_SHARD, FX_PUFF, FX_BUMP };

/* score steps: 100 200 400 500 800 1000 2000 4000 5000 8000 1UP */
enum { SC_100, SC_200, SC_400, SC_500, SC_800, SC_1000, SC_2000, SC_4000, SC_5000, SC_8000, SC_1UP };

/* sound events (bits of World.sfx, set during a step) */
#define EV_JUMP      0x0001
#define EV_JUMP_BIG  0x0002
#define EV_STOMP     0x0004
#define EV_KICK      0x0008
#define EV_BUMP      0x0010
#define EV_BREAK     0x0020
#define EV_COIN      0x0040
#define EV_SPROUT    0x0080
#define EV_POWERUP   0x0100
#define EV_POWERDOWN 0x0200
#define EV_1UP       0x0400
#define EV_SHOT      0x0800
#define EV_CHECKPOINT 0x1000      /* passed a sector checkpoint */
#define EV_TICK      0x2000
#define EV_LAUNCH    0x4000
#define EV_DIE       0x8000
/* music events (World.mev) */
#define MEV_NOVA     0x01          /* invincibility began */
#define MEV_NOVA_END 0x02
#define MEV_HURRY    0x04
#define MEV_RESPAWN  0x08          /* back in the run after a death: restart the main song */
#define MEV_GAMEOVER 0x10

typedef struct {
    uint8_t kind, state;
    uint16_t x;
    int16_t y;
    uint8_t xs, ys;          /* subpixels (1/256) */
    int16_t vx, vy;          /* 1/256 px per frame */
    uint8_t t;               /* state timer */
    uint8_t t2;              /* second timer (shell kick grace, chomper phase, ...) */
    uint8_t chain;           /* moving shell: its kill chain */
    uint8_t ground;
} Ent;

typedef struct {
    uint8_t kind;
    uint8_t t;
    uint16_t x;
    int16_t y;
    int8_t vx, vy;           /* px per frame (shards, coins) */
    uint8_t v;               /* score step / sprite variant / bump cell's final tile */
    uint8_t col, row;        /* FX_BUMP: the cell */
} Fx;

typedef struct {
    /* the level ring: [column & 31][row]; rows 13-15 unused (a power-of-two stride). It is the
       first field: the Game Boy's tile lookup (sim_int.h) addresses it as _W. */
    uint8_t lv[LV_COLS][16];

    /* Peter */
    uint16_t px, pxs;        /* x px, x sub (1/4096) */
    int16_t py;
    uint16_t pys;
    int16_t pvx, pvy;        /* 1/4096 px per frame */
    uint8_t power, pstate, ground, face, duck, run_t, skid;
    uint16_t takeoff;        /* |vx| when he left the ground */
    uint16_t g_hold, g_fall; /* gravities chosen at takeoff */
    uint8_t jumped;          /* left the ground by jumping (A gravity applies) */
    uint8_t hurt_t;          /* invulnerable after a hit (flashing) */
    uint16_t nova_t;
    uint8_t chain;           /* stomp chain while airborne */
    uint8_t anim;            /* run cycle frame 0..2 */
    uint16_t anim_t;         /* run cycle distance (1/256 px) */
    uint8_t shoot_t;         /* the blaster pose */
    uint8_t dead_pit;        /* died by falling (no death hop) */
    uint8_t state_t;         /* grow / shrink / death timers */
    uint8_t keys, prev;      /* input this frame and last */

    /* camera */
    uint16_t cam_x;
    uint8_t cam_y;

    /* the level ring */
    uint8_t spawn[LV_COLS];
    uint16_t gen_col;        /* next column to generate (0..4095, wraps) */
    uint16_t spawn_col;      /* next column whose spawn is pending */
    Gen gen;
    uint16_t sec_start;      /* first column of the checkpoint sector */
    uint16_t sec_start_next; /* first column of the sector being generated */
    uint16_t sector;         /* the checkpoint sector */
    uint16_t sector_next;    /* sector of the generator */
    uint16_t check_col;      /* the next checkpoint's column, 0xFFFF until generated or passed */
    uint16_t rngs;           /* the simulation's own random stream (comet timing) */

    Ent e[MAX_ENTS];
    uint8_t n_ents;          /* enemies about (counted each frame) */
    uint8_t bump_i;          /* which slots' turn it is to check for walkers bumping */
    Ent item;                /* one power-up at a time */
    Ent shot[MAX_SHOTS];
    Fx fx[MAX_FX];
    uint8_t n_fx;            /* effects in use (the loops skip them all when 0) */

    /* the multi-coin brick being emptied */
    uint8_t mc_col, mc_row, mc_t, mc_n;

    /* run */
    uint16_t seed;
    uint32_t score;
    uint8_t sdig[7];         /* the score's decimal digits (most significant first) */
    uint8_t score_rev;       /* bumped whenever the score changes */
    uint8_t coins, lives;
    uint16_t time;           /* time units left */
    uint8_t time_sub;
    uint16_t bonus;          /* time units still being counted into the score at a checkpoint */
    uint8_t hurry;
    uint16_t dist;           /* furthest column reached, counted from the start (saturates) */
    uint16_t far_col;        /* furthest camera column so far (absolute, wraps) */
    uint16_t sectors_done;
    uint32_t frames;         /* frames in play (the speedrun clock) */
    uint8_t over;            /* game over */
    uint8_t god;             /* test hook: no enemies, no harm (pits and the clock still kill) */
    uint8_t mode;            /* MODE_*: how much of the running the game does for you */

    /* output of the last step */
    uint16_t sfx;
    uint8_t mev;
    uint8_t dirty_n;
    uint8_t dirty_col[MAX_DIRTY], dirty_row[MAX_DIRTY];
    uint8_t redraw;          /* the whole level view changed (respawn) */
    uint8_t frozen;          /* the world is paused this frame (grow/shrink/death) */
} World;

extern World W;          /* the world (one global: much faster code on the Game Boy) */

/* play modes (picked before a run; the score board keeps a best per mode) */
enum {
    MODE_CLASSIC,            /* B runs, as in the classic game */
    MODE_SPRINT,             /* always running: just steer and jump (B still shoots) */
    MODE_AUTORUN,            /* always running right: just jump (and shoot, and duck) */
    MODE_COUNT
};

void sim_init(uint16_t seed, uint8_t mode) SIM_BANKED;
void sim_step(uint8_t keys) SIM_BANKED;
/* restart the checkpoint sector (w->sector at column w->sec_start): small Peter, full time */
void sim_respawn(void) SIM_BANKED;

/* the cell at a world column (0..4095) and row; out-of-ring columns read as sky */
uint8_t sim_cell(uint16_t col, uint8_t row) SIM_BANKED;
/* what the cell looks like (hidden blocks and bouncing blocks show as sky, look-alikes merged) */
uint8_t sim_visual(uint16_t col, uint8_t row) SIM_BANKED;
/* Peter's height in px (16 small / ducking, 32 big) */
uint8_t sim_peter_h(void) SIM_BANKED;

extern const uint16_t score_value[11];

#endif
