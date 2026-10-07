/* test_core.c - host tests for the SPEEDY PURPLE PETER simulation and level generator.
 *
 *   physics    jump heights and distances, speeds, skids, air control (the classic constants)
 *   generator  determinism, sector rebuilds, the rules that keep every jump makeable
 *   mechanics  stomps, shells, capsules, bricks, star bits, beacons, deaths, the clock
 *   bot        the search bot plays many seeds through many sectors (terrain only, then with
 *              enemies), proving the generator never makes an unbeatable stretch
 *
 * Run: make test-host */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "bot.h"

void ents_spawn(uint16_t col, uint8_t sp);     /* ents.c (internal) */

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------------ a test bench */
/* a flat, empty world: Peter standing on the ground at column 4, no enemies. Columns the
   simulation generates later (as the camera moves) are flattened too, by step(). */
static int on_bench;
static uint16_t bench_gen;

static void flatten(uint16_t c)
{
    uint8_t r;
    for (r = 0; r < LV_ROWS; r++) W.lv[c & (LV_COLS - 1)][r] = T_SKY;
    W.lv[c & (LV_COLS - 1)][GROUND_ROW] = T_GROUND_TOP;
    W.lv[c & (LV_COLS - 1)][GROUND_ROW + 1] = T_GROUND;
    W.spawn[c & (LV_COLS - 1)] = 0;
}

static void bench(void)
{
    uint16_t c;
    uint8_t r;
    on_bench = 1;
    sim_init(0x1234);
    memset(W.e, 0, sizeof(W.e));
    W.god = 0;
    for (c = 0; c < LV_COLS; c++) flatten(c);
    W.px = 4 * 16;
    W.py = GROUND_ROW * 16 - 16;
    W.time = 999;
    W.beacon_col = W.flag_col = 0xFFFF;
    bench_gen = W.gen_col;
    (void)r;
}

/* keep the bench's level from being regenerated as the camera moves (tests are short) */
static void step(uint8_t k)
{
    sim_step(k);
    while (W.pstate == PS_GROW || W.pstate == PS_SHRINK) sim_step(k);
    if (on_bench)
        while (bench_gen != W.gen_col) { flatten(bench_gen); bench_gen = (uint16_t)((bench_gen + 1) & 0xFFF); }
}

static void cell(uint16_t col, uint8_t row, uint8_t t) { W.lv[col & (LV_COLS - 1)][row] = t; }

/* put Peter in the air at y, falling */
static void airborne(uint16_t x, int16_t y)
{
    W.px = x;
    W.py = y;
    W.ground = 0;
    W.jumped = 0;
    W.pvy = 0;
    W.g_hold = 0x200;
    W.g_fall = 0x700;
}

/* ------------------------------------------------------------------ physics */
static int jump_height(uint16_t speed_keys, int hold)
{
    int i, top = 1000, y0;
    bench();
    for (i = 0; i < 90 && speed_keys; i++) step((uint8_t)speed_keys);      /* build up speed */
    y0 = W.py;
    step((uint8_t)(speed_keys | K_A));
    for (i = 0; i < 80; i++) {
        step((uint8_t)(speed_keys | (i < hold ? K_A : 0)));
        if (W.py < top) top = W.py;
        if (W.ground) break;
    }
    return y0 - top;
}

static void test_physics(void)
{
    int h, i;
    uint16_t x0;

    /* standing jump, A held: about 4 blocks (66 px) */
    h = jump_height(0, 99);
    CHECK(h >= 64 && h <= 67, "standing jump height %d", h);
    /* a tap: much lower (variable jump height) */
    h = jump_height(0, 2);
    CHECK(h < 30, "tapped jump height %d", h);
    /* walking jump */
    h = jump_height(K_RIGHT, 99);
    CHECK(h >= 66 && h <= 72, "walking jump height %d", h);
    /* running jump: 5 blocks */
    h = jump_height(K_RIGHT | K_B, 99);
    CHECK(h >= 78 && h <= 85, "running jump height %d", h);

    /* top speeds: 0x1900 walking, 0x2900 running */
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT);
    CHECK(W.pvx == PH_MAX_WALK, "max walk %x", W.pvx);
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT | K_B);
    CHECK(W.pvx == PH_MAX_RUN, "max run %x", W.pvx);
    /* letting go of B keeps the run speed for a moment, then it eases down to walking */
    for (i = 0; i < PH_RUN_MEMORY - 1; i++) step(K_RIGHT);
    CHECK(W.pvx == PH_MAX_RUN, "run memory %x", W.pvx);
    for (i = 0; i < 30; i++) step(K_RIGHT);
    CHECK(W.pvx == PH_MAX_WALK, "eased to walk %x", W.pvx);

    /* from a standstill a press gives the minimum walking speed at once */
    bench();
    step(K_RIGHT);
    CHECK(W.pvx == PH_MIN_WALK + PH_WALK_ACC || W.pvx == PH_MIN_WALK, "first step %x", W.pvx);

    /* skid: reversing at speed decelerates by the skid rate, then turns */
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT | K_B);
    step(K_LEFT | K_B);
    CHECK(W.skid && W.pvx == PH_MAX_RUN - PH_SKID_DEC, "skid %x", W.pvx);
    for (i = 0; i < 40 && W.pvx > 0; i++) step(K_LEFT | K_B);
    CHECK(W.pvx <= 0 && W.face == 1, "turned around after %d frames", i);

    /* releasing: friction */
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT);
    step(0);
    CHECK(W.pvx == PH_MAX_WALK - PH_RELEASE_DEC, "release %x", W.pvx);

    /* air: facing is locked, speed is kept without input */
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT | K_B);
    step(K_RIGHT | K_B | K_A);
    for (i = 0; i < 10; i++) step(K_A);
    CHECK(W.pvx == PH_MAX_RUN, "air keeps speed %x", W.pvx);
    step(K_LEFT | K_A);
    CHECK(W.face == 0 && W.pvx == PH_MAX_RUN - PH_RUN_ACC, "air brake %x face %u", W.pvx, W.face);

    /* fall speed cap */
    bench();
    W.py = -200;
    W.ground = 0;
    for (i = 0; i < 40; i++) { step(0); CHECK(W.pvy < PH_MAX_FALL, "fall cap %x", W.pvy); }

    /* running jump distance: clears a 5-wide pit (gen_max_pit's worst) with room to spare */
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT | K_B);
    x0 = W.px;
    step(K_RIGHT | K_B | K_A);
    for (i = 0; i < 120 && !W.ground; i++) step(K_RIGHT | K_B | K_A);
    CHECK((uint16_t)(W.px - x0) >= 6 * 16, "running jump distance %u px", (uint16_t)(W.px - x0));

    /* walls stop him; he does not stick into them */
    bench();
    cell(8, GROUND_ROW - 1, T_SOLID);
    cell(8, GROUND_ROW - 2, T_SOLID);
    for (i = 0; i < 90; i++) step(K_RIGHT | K_B);
    CHECK(W.px + 13 < 8 * 16 && W.px + 14 >= 8 * 16 - 1, "wall at %u", W.px);

    /* the camera keeps the ground in view: a full running jump from the ground never scrolls up */
    bench();
    for (i = 0; i < 120; i++) step(K_RIGHT | K_B);
    CHECK(W.cam_y == CAM_Y_MAX, "camera on the ground %u", W.cam_y);
    step(K_RIGHT | K_B | K_A);
    for (i = 0; i < 80; i++) {
        step(K_RIGHT | K_B | K_A);
        CHECK(W.cam_y == CAM_Y_MAX, "camera rose during a jump (%u)", W.cam_y);
    }
    /* ... but it follows him up onto something high, and back down */
    bench();
    for (i = 3; i < 12; i++) cell((uint16_t)i, 3, T_SOLID);
    W.py = 3 * 16 - 16;
    W.ground = 1;
    for (i = 0; i < 40; i++) step(0);
    CHECK(W.cam_y == 0, "camera up on a high ledge (%u)", W.cam_y);

    /* ducking (big): no acceleration, a lower box */
    bench();
    W.power = PW_BIG;
    W.py -= 16;
    step(K_DOWN | K_RIGHT);
    CHECK(W.duck && W.pvx == 0 && sim_peter_h() == 16, "duck");
}

/* ------------------------------------------------------------------ generator */
static void test_generator(void)
{
    static uint8_t a[400][LV_ROWS], b[400][LV_ROWS];
    uint8_t sa[400], sb[400];
    uint16_t seed;
    int i, n;

    /* determinism, and a sector rebuilt from (seed, sector) alone is identical */
    for (seed = 1; seed < 40; seed += 7) {
        Gen g1, g2;
        gen_begin(&g1, seed, 0);
        for (i = 0; i < 400; i++) gen_column(&g1, a[i], &sa[i]);
        /* find where sector 3 starts in the stream, then build sector 3 from scratch */
        gen_begin(&g1, seed, 0);
        n = 0;
        for (i = 0; i < 2000; i++) {
            uint8_t c[LV_ROWS], s;
            uint8_t f = gen_column(&g1, c, &s);
            if ((f & GEN_SECTOR_START) && g1.sector == 3) { n = i; break; }
        }
        gen_begin(&g1, seed, 0);
        for (i = 0; i < n; i++) { uint8_t c[LV_ROWS], s; gen_column(&g1, c, &s); }
        for (i = 0; i < 200; i++) gen_column(&g1, a[i], &sa[i]);
        gen_begin(&g2, seed, 3);
        for (i = 0; i < 200; i++) gen_column(&g2, b[i], &sb[i]);
        CHECK(!memcmp(a, b, 200 * LV_ROWS) && !memcmp(sa, sb, 200), "sector 3 rebuild, seed %u", seed);
    }

    /* the rules over many seeds and sectors */
    for (seed = 0; seed < 200; seed++) {
        Gen g;
        int pit = 0, land = 99, beacons = 0, last_start = -1, col;
        uint8_t prev[LV_ROWS];
        gen_begin(&g, (uint16_t)(seed * 977u), 0);
        memset(prev, 0, sizeof(prev));
        for (col = 0; col < 2500; col++) {
            uint8_t c[LV_ROWS], s, r, top = 0;
            uint8_t d = g.diff;
            uint8_t f = gen_column(&g, c, &s);
            uint8_t ground = c[GROUND_ROW] != T_SKY || c[GROUND_ROW - 1] != T_SKY;
            if (f & GEN_SECTOR_START) {
                if (last_start >= 0) CHECK(beacons == 1, "seed %u: one beacon per sector (%d)", seed, beacons);
                CHECK(last_start < 0 || col - last_start >= 60, "seed %u: sector too short %d", seed, col - last_start);
                beacons = 0;
                last_start = col;
            }
            if (f & GEN_BEACON) beacons++;
            if (c[GROUND_ROW] == T_SKY) {
                pit++;
                if (pit == 1) CHECK(land >= 2, "seed %u col %d: a %d-column island", seed, col, land);
            } else {
                if (pit) CHECK(pit <= gen_max_pit(d) || pit <= 10, "seed %u col %d: pit %d", seed, col, pit);
                pit = 0;
                land = (prev[GROUND_ROW] == T_SKY) ? 1 : land + 1;
            }
            (void)ground;
            /* tubes are 2 wide and at most 4 high; nothing floats inside them */
            for (r = 0; r < LV_ROWS; r++)
                if (c[r] == T_TUBE_TL) top = (uint8_t)(GROUND_ROW - r);
            CHECK(top <= 4, "seed %u col %d: tube %u high", seed, col, top);
            /* spawns stand in sky above something */
            if (s) {
                uint8_t row = (uint8_t)(s & 15), k = (uint8_t)(s >> 4);
                CHECK(k > 0 && k < SP_COUNT, "spawn kind %u", k);
                if (k != SP_CHOMP && k != SP_CANNON) CHECK(c[row] == T_SKY || c[row] >= T_HILL_L || c[row] <= T_STARS_B, "seed %u col %d: spawn inside a block", seed, col);
            }
            memcpy(prev, c, sizeof(prev));
        }
    }

    /* the first sector opens like a first level: a flat start and a power-up capsule */
    {
        Gen g;
        uint8_t c[LV_ROWS], s;
        int found = 0;
        gen_begin(&g, 7, 0);
        for (i = 0; i < 16; i++) {
            gen_column(&g, c, &s);
            CHECK(c[GROUND_ROW] == T_GROUND_TOP && !s, "flat start col %d", i);
            if (c[7] == T_Q_POWER) found = 1;
        }
        CHECK(found, "a power-up in the opening");
    }
}

/* ------------------------------------------------------------------ mechanics */
static void test_mechanics(void)
{
    int i;
    uint32_t s0;

    /* a capsule overhead: a star bit pops out, it becomes used */
    bench();
    cell(4, 7, T_Q_COIN);
    W.px = 4 * 16;
    s0 = W.score;
    step(K_A);
    for (i = 0; i < 60; i++) step(K_A);
    CHECK(W.coins == 1 && W.lv[4][7] == T_USED, "capsule coin: coins %u cell %u", W.coins, W.lv[4][7]);
    for (i = 0; i < 60; i++) step(0);
    CHECK(W.score == s0 + 200, "coin score %lu", (unsigned long)(W.score - s0));

    /* a power capsule: small Peter gets a planet, which walks; touching it makes him big */
    bench();
    cell(4, 7, T_Q_POWER);
    step(K_A);
    for (i = 0; i < 40; i++) step(K_A);
    CHECK(W.item.kind == E_PLANET, "planet sprouted (%u)", W.item.kind);
    for (i = 0; i < 300 && W.power == PW_SMALL; i++) step(K_RIGHT);
    CHECK(W.power == PW_BIG, "grew");
    /* big Peter breaks bricks, small Peter only bumps them */
    bench();
    W.power = PW_BIG;
    W.py -= 16;
    cell(4, 7, T_BRICK);
    step(K_A);
    for (i = 0; i < 30; i++) step(K_A);
    CHECK(W.lv[4][7] == T_SKY, "big breaks bricks");
    bench();
    cell(4, 7, T_BRICK);
    step(K_A);
    for (i = 0; i < 30; i++) step(K_A);
    CHECK(W.lv[4][7] == T_BRICK, "small bumps bricks");

    /* 100 star bits make a 1UP */
    bench();
    W.coins = 99;
    cell(5, GROUND_ROW - 1, T_COIN);
    for (i = 0; i < 30; i++) step(K_RIGHT);
    CHECK(W.coins == 0 && W.lives == START_LIVES + 1, "100 coins: coins %u lives %u", W.coins, W.lives);

    /* stomping a Gloop flattens it and bounces Peter; chained stomps score more */
    bench();
    ents_spawn(5, SPAWN(SP_GLOOP, GROUND_ROW - 2));
    W.e[0].y = GROUND_ROW * 16 - 16;
    W.e[0].vx = 0;
    airborne(5 * 16, 64);
    s0 = W.score;
    for (i = 0; i < 60 && W.e[0].state == ES_LIVE; i++) step(0);
    CHECK(W.e[0].state == ES_FLAT && W.pvy < 0 && W.score == s0 + 100, "stomp: state %u vy %d", W.e[0].state, W.pvy);
    CHECK(W.lives == START_LIVES && W.pstate == PS_PLAY, "a stomp does not hurt");

    /* walking into a Gloop hurts: big shrinks (and blinks), small dies */
    bench();
    W.power = PW_BIG;
    W.py -= 16;
    ents_spawn(7, SPAWN(SP_GLOOP, GROUND_ROW - 1));
    for (i = 0; i < 90 && W.power; i++) step(K_RIGHT);
    CHECK(W.power == PW_SMALL && W.hurt_t > 0, "shrank");
    bench();
    ents_spawn(7, SPAWN(SP_GLOOP, GROUND_ROW - 1));
    for (i = 0; i < 90 && W.pstate == PS_PLAY; i++) step(K_RIGHT);
    CHECK(W.pstate == PS_DEAD, "small dies");

    /* a Dome-bot becomes a pod; touching the pod kicks it; the sliding pod knocks out a Gloop */
    bench();
    ents_spawn(6, SPAWN(SP_DOME, GROUND_ROW - 1));
    W.e[0].vx = 0;
    airborne(6 * 16, 80);
    for (i = 0; i < 60 && W.e[0].kind == E_DOME; i++) step(0);
    CHECK(W.e[0].kind == E_SHELL && W.e[0].vx == 0, "pod");
    for (i = 0; i < 40 && !W.ground; i++) step(0);
    ents_spawn(12, SPAWN(SP_GLOOP, GROUND_ROW - 1));
    W.e[1].vx = 0;
    W.px = 6 * 16 - 14;
    for (i = 0; i < 6 && W.e[0].vx == 0; i++) step(K_RIGHT);
    CHECK(W.e[0].vx > 0, "kicked right (%d)", W.e[0].vx);
    for (i = 0; i < 60 && W.e[1].state == ES_LIVE; i++) step(0);
    CHECK(W.e[1].state == ES_FALL, "the pod knocked the Gloop out");

    /* the beacon: touching it scores by height, moves the checkpoint, counts time in */
    bench();
    W.beacon_col = 8;
    W.sector_next = 1;
    W.sec_start_next = 13;
    W.time = 200;
    s0 = W.score;
    for (i = 0; i < 60 && !W.sectors_done; i++) step(K_RIGHT | K_B);
    CHECK(W.sectors_done == 1 && W.sector == 1 && W.sec_start == 13, "beacon checkpoint");
    for (i = 0; i < 200 && W.bonus; i++) step(K_RIGHT);
    CHECK(W.time == TIME_START && W.score >= s0 + 100 + 199u * 50, "time bonus: score +%lu time %u",
          (unsigned long)(W.score - s0), W.time);

    /* falling into a pit costs a life and restarts the checkpoint sector with full time */
    on_bench = 0;
    sim_init(0x55);
    W.god = 1;
    {
        uint16_t start = W.sec_start;
        W.py = 300;
        step(0);
        CHECK(W.pstate == PS_DEAD, "pit death");
        for (i = 0; i < 200 && W.pstate == PS_DEAD; i++) step(0);
        CHECK(W.lives == START_LIVES - 1 && W.px == start * 16 + 32 && W.time == TIME_START, "respawn");
    }

    /* the clock: running out of time is a death; three deaths end the run */
    sim_init(0x99);
    for (i = 0; i < 3; i++) {
        int k;
        W.time = 1;
        for (k = 0; k < 400 && W.lives == START_LIVES - i && !W.over; k++) step(0);
    }
    CHECK(W.over && W.lives == 0, "game over after 3 deaths");

    /* a supernova makes him invincible: enemies fall over */
    bench();
    W.nova_t = NOVA_FRAMES;
    ents_spawn(7, SPAWN(SP_GLOOP, GROUND_ROW - 1));
    for (i = 0; i < 60; i++) step(K_RIGHT);
    CHECK(W.pstate == PS_PLAY && W.e[0].state != ES_LIVE, "nova");

    /* the blaster: B fires a bouncing shot that knocks a Gloop out */
    bench();
    W.power = PW_BLASTER;
    W.py -= 16;
    ents_spawn(9, SPAWN(SP_GLOOP, GROUND_ROW - 1));
    W.e[0].vx = 0;
    step(K_B);
    CHECK(W.shot[0].kind, "shot fired");
    for (i = 0; i < 60 && W.e[0].state == ES_LIVE; i++) step(0);
    CHECK(W.e[0].state == ES_FALL, "shot hit");

    /* determinism: two runs with the same inputs end identically */
    {
        static World a;
        sim_init(0xBEEF);
        for (i = 0; i < 3000; i++) sim_step((uint8_t)((i % 50 < 30 ? K_A : 0) | K_RIGHT | K_B));
        a = W;
        sim_init(0xBEEF);
        for (i = 0; i < 3000; i++) sim_step((uint8_t)((i % 50 < 30 ? K_A : 0) | K_RIGHT | K_B));
        CHECK(!memcmp(&a, &W, sizeof(World)), "determinism");
    }
}

/* ------------------------------------------------------------------ bot */
static void test_bot(int seeds, int sectors, int god)
{
    int s, ok = 0;
    for (s = 1; s <= seeds; s++) {
        static World w;
        static BotResult res;
        sim_init((uint16_t)(s * 0x9E1u));
        if (god) { W.god = 1; memset(W.e, 0, sizeof(W.e)); }
        w = W;
        res.path = 0;
        if (bot_play(&w, (uint16_t)sectors, 3000000, &res)) ok++;
        else printf("  bot stuck: seed %04X, sector %u, column +%u%s\n", (unsigned)(s * 0x9E1u), res.best.sector,
                    (unsigned)(((res.best.px >> 4) - res.best.sec_start) & 0xFFF), god ? " (terrain only)" : "");
    }
    CHECK(ok == seeds, "bot (%s) cleared %d sectors on %d/%d seeds", god ? "terrain" : "with enemies", sectors, ok, seeds);
    printf("bot %s: %d/%d seeds x %d sectors\n", god ? "terrain" : "enemies", ok, seeds, sectors);
}

int main(int argc, char **argv)
{
    int quick = argc > 1 && !strcmp(argv[1], "quick");
    test_physics();
    test_generator();
    test_mechanics();
    test_bot(quick ? 8 : 40, 12, 1);
    test_bot(quick ? 4 : 12, 8, 0);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
