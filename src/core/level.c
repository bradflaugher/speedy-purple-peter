/* level.c - SPEEDY PURPLE PETER level generator (portable: SDCC + gcc).
 *
 * A sector is: a short start stretch, a run of "segments" (the vocabulary of a classic first
 * level: capsule rows, tubes, pits, stair pyramids, high brick rows, enemies on the flats),
 * then the end staircase and the beacon pole. Segments never overlap, and a short flat stretch
 * separates hazards, so every jump the generator makes is one the physics can make (the host
 * tests prove it by playing every sector with a search bot). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "level.h"

#if defined(__SDCC) && defined(SPP_PROFILE)       /* profiling stamps (see sim_int.h) */
extern uint8_t dbg_gen_ly[4];
#define GPROF(i) (dbg_gen_ly[i] = *(volatile uint8_t *)0xFF44)
#else
#define GPROF(i) ((void)0)
#endif

#define SKY T_SKY

/* xorshift16 (7, 9, 8), written byte by byte: the Game Boy shifts one bit at a time */
static uint8_t rnd(Gen *g)
{
    uint8_t lo = (uint8_t)g->rng, hi = (uint8_t)(g->rng >> 8);
    hi ^= (uint8_t)((hi << 7) | (lo >> 1));     /* x ^= x << 7 */
    lo ^= (uint8_t)(lo << 7);
    lo ^= (uint8_t)(hi >> 1);                   /* x ^= x >> 9 */
    hi ^= lo;                                   /* x ^= x << 8 */
    g->rng = (uint16_t)((uint16_t)hi << 8 | lo);
    return (uint8_t)(lo ^ hi);
}

/* 0..n-1 */
/* (a * b) >> 8: the Game Boy has no multiply instruction, and SDCC's 16-bit one is slow, so on
   the Game Boy it is 8 shift-and-adds in assembly (a in A, b in E, the result in A) */
#if defined(__SDCC) && defined(__PORT_sm83)
static uint8_t mulhi8(uint8_t a, uint8_t b) __naked
{
    (void)a; (void)b;
    __asm
        ld  d, #0
        ld  hl, #0
        ld  b, #8
    1$:
        add hl, hl
        rla
        jr  nc, 2$
        add hl, de
    2$:
        dec b
        jr  nz, 1$
        ld  a, h
        ret
    __endasm;
}
#else
static uint8_t mulhi8(uint8_t a, uint8_t b)
{
    return (uint8_t)(((uint16_t)a * b) >> 8);
}
#endif

static uint8_t rr(Gen *g, uint8_t n)
{
    return mulhi8(rnd(g), n);
}

/* lo..hi inclusive */
static uint8_t rrange(Gen *g, uint8_t lo, uint8_t hi)
{
    return (uint8_t)(lo + rr(g, (uint8_t)(hi - lo + 1)));
}

/* percent chance */
static uint8_t chance(Gen *g, uint8_t pct)
{
    return (uint8_t)(rr(g, 100) < pct);
}

uint8_t gen_max_pit(uint8_t d) GEN_BANKED
{
    if (d == 0) return 2;
    if (d < 4) return 3;
    if (d < 8) return 4;
    return 5;
}

static uint8_t enemy_kind(Gen *g)
{
    uint8_t r = rr(g, 100), d = g->diff;
    if (d >= 2 && r < 10 + d * 2) return SP_JET;
    if (d >= 1 && r < 25 + d * 3) return SP_DOME_RED;
    if (r < 40) return SP_DOME;
    return SP_GLOOP;
}

static void start_seg(Gen *g, uint8_t seg)
{
    uint8_t d = g->diff;
    g->run_before = (g->seg == SEG_FLAT || g->seg == SEG_START) ? g->len : 0;
    g->seg = seg;
    g->pos = 0;
    g->a = g->b = g->c = g->d = 0;
    switch (seg) {
    case SEG_START:
        g->len = g->sector == 0 ? 16 : 6;
        break;
    case SEG_FLAT:              /* a: enemy kind, b: where, c: 1 = a pair */
        g->len = rrange(g, 3, 7);
        if (chance(g, (uint8_t)(35 + (d > 9 ? 45 : d * 5)))) {
            g->a = enemy_kind(g);
            g->b = rr(g, (uint8_t)(g->len - 2));
            g->c = (uint8_t)(g->a == SP_GLOOP && chance(g, 30 + d * 4));
        }
        break;
    case SEG_PIT:                /* (a pit wider than 3 needs a 3-column run-up) */
        g->len = rrange(g, 2, (uint8_t)(g->run_before < 3 && gen_max_pit(d) > 3 ? 3 : gen_max_pit(d)));
        break;
    case SEG_QROW:              /* a: variant, b: power column, c: upper capsule, d: enemy */
        g->a = rr(g, 5);
        g->len = g->a == 0 ? 1 : 5;
        g->b = 0xFF;
        if (g->power_left && chance(g, 60)) {
            g->b = g->a == 0 ? 0 : (uint8_t)(1 + rr(g, 2) * 2);   /* a '?' column (1 or 3) */
            if (g->a == 3) g->b = rr(g, 5);                       /* any brick */
            if (g->a == 4) g->b = 0xFF;                           /* coin row: none */
            if (g->b != 0xFF) g->power_left--;
        }
        g->c = (uint8_t)(g->a == 1 && chance(g, 50));
        g->d = chance(g, (uint8_t)(30 + d * 4)) ? enemy_kind(g) : 0;
        if (g->d == SP_JET) g->d = SP_GLOOP;
        break;
    case SEG_HIGH:              /* low "B?B", then a long high brick row; b: pit under it */
        g->len = rrange(g, 9, 12);
        g->b = (uint8_t)(d >= 1 && chance(g, 50));
        g->c = rrange(g, 5, (uint8_t)(g->len - 2));           /* '?' in the high row */
        g->d = chance(g, (uint8_t)(40 + d * 4));              /* gloops up there */
        break;
    case SEG_TUBE:              /* a: height, b: chomper */
        g->len = 4;
        /* a 4-high tube needs a run-up (a standing jump only just clears it) */
        g->a = rrange(g, 2, (uint8_t)(g->run_before >= 3 ? 4 : 3));
        g->b = (uint8_t)(d >= 1 && chance(g, (uint8_t)(20 + d * 6)));
        break;
    case SEG_STAIRS:            /* a: height, b: gap (0 = a one-column plateau) */
        g->a = rrange(g, 2, 4);
        g->b = d == 0 ? 0 : rr(g, (uint8_t)(d < 3 ? 3 : 4));
        g->len = (uint8_t)(g->a * 2 + (g->b ? g->b : 1));
        break;
    case SEG_BRIDGE:            /* a wide pit with a floating platform in the middle (3 clear
                                   columns on each side, so you jump onto it, never into its
                                   underside), 3 rows up; b: a gloop on it; c: extra ground
                                   columns first, so there is always a 3-column run-up */
        g->c = (uint8_t)(g->run_before < 3 ? 3 - g->run_before : 0);
        g->len = (uint8_t)(rrange(g, 8, 10) + g->c);
        g->a = 8;
        g->b = (uint8_t)(chance(g, 40));
        break;
    case SEG_COINS:             /* a: shape (0 row, 1 arc over a pit) */
        g->a = (uint8_t)(d >= 1 && chance(g, 50));
        g->len = g->a ? 4 : rrange(g, 3, 6);
        break;
    case SEG_CANNON:            /* a: height (1-2) */
        g->len = 3;
        g->a = rrange(g, 1, 2);
        break;
    case SEG_END:               /* a: staircase height */
        g->a = (uint8_t)(4 + (d > 8 ? 4 : d / 2));
        g->len = (uint8_t)(g->a + 1 + 3 + 1 + 3);
        break;
    }
}

static uint8_t pick_body_seg(Gen *g)
{
    /* weights by difficulty */
    uint8_t d = g->diff;
    uint8_t w[SEG_COUNT];
    uint8_t i, sum = 0, r;
    for (i = 0; i < SEG_COUNT; i++) w[i] = 0;
    w[SEG_FLAT] = 3;
    w[SEG_PIT] = (uint8_t)(2 + (d > 6 ? 3 : d / 2));
    w[SEG_QROW] = 4;
    w[SEG_HIGH] = 1;
    w[SEG_TUBE] = 3;
    w[SEG_STAIRS] = 2;
    w[SEG_BRIDGE] = d >= 2 ? 2 : 0;
    w[SEG_COINS] = 1;
    w[SEG_CANNON] = d >= 3 ? 2 : 0;
    for (i = 0; i < SEG_COUNT; i++) sum = (uint8_t)(sum + w[i]);
    r = rr(g, sum);
    for (i = 0; i < SEG_COUNT; i++) {
        if (r < w[i]) return i;
        r = (uint8_t)(r - w[i]);
    }
    return SEG_FLAT;
}

static void next_seg(Gen *g)
{
    if (g->seg == SEG_END) {            /* next sector */
        gen_begin(g, g->seed, (uint16_t)(g->sector + 1));
        return;
    }
    if (g->gap_next) {                  /* breathing room between hazards */
        g->gap_next = 0;
        start_seg(g, SEG_FLAT);
        g->len = rrange(g, 2, 3);       /* never a one-column island */
        if (g->len < 3) g->a = 0;       /* too short for an enemy */
        else if (g->a) g->b = 0, g->c = 0;
        return;
    }
    if (g->segs_left == 0) {
        start_seg(g, SEG_END);
        return;
    }
    g->segs_left--;
    start_seg(g, pick_body_seg(g));
    if (g->seg != SEG_FLAT) g->gap_next = 1;
}

void gen_begin(Gen *g, uint16_t seed, uint16_t sector) GEN_BANKED
{
    uint16_t h = (uint16_t)(seed * 0x9E37u + sector * 0x7F4Bu + 0x1234u);
    h ^= (uint16_t)(h >> 7);
    h = (uint16_t)(h * 0x2B9u + sector);
    if (!h) h = 0xACE1u;
    g->seed = seed;
    g->sector = sector;
    g->rng = h;
    g->diff = (uint8_t)(sector > 15 ? 15 : sector);
    g->segs_left = (uint8_t)(12 + (g->diff > 8 ? 8 : g->diff));
    g->power_left = (uint8_t)(g->diff < 4 ? 2 : 1);
    g->col = 0;
    g->m48 = 0;
    g->m96 = 0;
    g->gap_next = 0;
    g->seg = SEG_END;
    g->len = 0;
    rnd(g);
    start_seg(g, SEG_START);
}

/* --------------------------------------------------------------------- columns */
static void stack(uint8_t *c, uint8_t h)       /* hull blocks h high on the ground */
{
    uint8_t r;
    for (r = 0; r < h; r++) c[GROUND_ROW - 1 - r] = T_SOLID;
}

/* the background decoration repeats every 48 columns: for each column, the hill / bush cells of
   rows 8-10 (only on solid ground) and one nebula-cloud cell (row, tile) */
#define S_ SKY
static const uint8_t deco_ground[48][3] = {
    { S_, S_, T_HILL_L }, { S_, T_HILL_L, T_HILL_FILL }, { T_HILL_TOP, T_HILL_FILL, T_HILL_CRATER },
    { S_, T_HILL_R, T_HILL_FILL }, { S_, S_, T_HILL_R },
    { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ },
    { S_, S_, T_BUSH_L }, { S_, S_, T_BUSH_M }, { S_, S_, T_BUSH_R },
    { S_, S_, S_ }, { S_, S_, S_ },
    { S_, S_, T_HILL_L }, { S_, T_HILL_TOP, T_HILL_CRATER }, { S_, S_, T_HILL_R },
    { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ },
    { S_, S_, T_BUSH_L }, { S_, S_, T_BUSH_M }, { S_, S_, T_BUSH_M }, { S_, S_, T_BUSH_R },
    { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ },
    { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ },
    { S_, S_, S_ }, { S_, S_, S_ },
    { S_, S_, T_BUSH_L }, { S_, S_, T_BUSH_M }, { S_, S_, T_BUSH_R },
    { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }, { S_, S_, S_ }
};
static const uint8_t deco_cloud[48][2] = {         /* row (0 = none), tile */
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
    { 2, T_CLOUD_L }, { 2, T_CLOUD_M }, { 2, T_CLOUD_R },
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
    { 1, T_CLOUD_L }, { 1, T_CLOUD_M }, { 1, T_CLOUD_M }, { 1, T_CLOUD_R },
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
    { 0, 0 }, { 0, 0 }, { 0, 0 },
    { 3, T_CLOUD_L }, { 3, T_CLOUD_M }, { 3, T_CLOUD_R },
    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
};
#undef S_

static void decor(Gen *g, uint8_t *c)
{
    uint8_t m = g->m48, r, k;
    const uint8_t *d;
    /* moon hills and crystal bushes stand on the ground */
    if (c[GROUND_ROW] == T_GROUND_TOP) {
        d = deco_ground[m];
        if (d[0] && c[8] == SKY) c[8] = d[0];
        if (d[1] && c[9] == SKY) c[9] = d[1];
        if (d[2] && c[10] == SKY) c[10] = d[2];
    }
    /* nebula clouds */
    r = deco_cloud[m][0];
    if (r && c[r] == SKY) c[r] = deco_cloud[m][1];
    /* a ringed planet now and then */
    if (g->m96 == 28 && c[1] == SKY && c[2] == SKY) { c[1] = T_PLANET_TL; c[2] = T_PLANET_BL; }
    else if (g->m96 == 29 && c[1] == SKY && c[2] == SKY) { c[1] = T_PLANET_TR; c[2] = T_PLANET_BR; }
    /* a sparse star (an 8-bit hash of the column: cheap on the Game Boy) */
    k = (uint8_t)(g->col + (g->col << 2) + (g->col << 5) + (uint8_t)g->sector + ((uint8_t)g->sector << 3));
    k = (uint8_t)(k ^ (k >> 3) ^ 0x5A);
    if ((k & 0x30) == 0) {
        r = (uint8_t)(k & 7);
        if (c[r] == SKY) c[r] = (k & 0x40) ? T_STARS_A : T_STARS_B;
    }
}

void gen_prepare(Gen *g) GEN_BANKED
{
    while (g->pos >= g->len) next_seg(g);
}

uint8_t gen_column(Gen *g, uint8_t *c, uint8_t *spawn) GEN_BANKED
{
    uint8_t r, p, ret = 0, h;
    GPROF(0);
    while (g->pos >= g->len) next_seg(g);
    GPROF(1);
    if (g->seg == SEG_START && g->pos == 0) ret |= GEN_SECTOR_START;
    p = g->pos;
    *spawn = 0;
    for (r = 0; r < GROUND_ROW; r++) c[r] = SKY;
    c[GROUND_ROW] = T_GROUND_TOP;
    c[GROUND_ROW + 1] = T_GROUND;

    switch (g->seg) {
    case SEG_START:
        if (g->sector == 0 && p == 12) c[7] = T_Q_POWER;
        break;
    case SEG_FLAT:
        if (g->a && (p == g->b || (g->c && p == (uint8_t)(g->b + 2))))
            *spawn = SPAWN(g->a, GROUND_ROW - 1);
        break;
    case SEG_PIT:
        c[GROUND_ROW] = c[GROUND_ROW + 1] = SKY;
        break;
    case SEG_QROW:
        switch (g->a) {
        case 0: c[7] = p == g->b ? T_Q_POWER : T_Q_COIN; break;
        case 1:                                  /* B ? B ? B, maybe a '?' up high */
            c[7] = (p & 1) ? (p == g->b ? T_Q_POWER : T_Q_COIN) : T_BRICK;
            if (g->c && p == 2) c[3] = T_Q_COIN;
            break;
        case 2:                                  /* ?  ?  ? (spaced) */
            if (!(p & 1)) c[7] = (p == g->b) ? T_Q_POWER : T_Q_COIN;
            break;
        case 3:                                  /* bricks, one hiding something */
            c[7] = p == g->b ? T_BRICK_POWER : p == 2 ? T_BRICK_COIN : T_BRICK;
            break;
        default:                                 /* a row of star bits */
            c[7] = T_COIN;
            break;
        }
        if (g->d && p == g->len - 1) *spawn = SPAWN(g->d, GROUND_ROW - 1);
        break;
    case SEG_HIGH:
        if (p < 3) c[7] = p == 1 ? T_Q_COIN : T_BRICK;
        if (p >= 3) c[3] = p == g->c ? T_Q_COIN : (p == 4 && g->diff >= 2 && (g->sector & 1)) ? T_BRICK_NOVA : T_BRICK;
        if (g->b && (p == 5 || p == 6)) c[GROUND_ROW] = c[GROUND_ROW + 1] = SKY;
        if (g->d && (p == 6 || p == 9) && p < g->len) *spawn = SPAWN(SP_GLOOP, 2);
        break;
    case SEG_TUBE:
        h = g->a;
        if (p == 1 || p == 2) {
            for (r = 0; r < h; r++)
                c[GROUND_ROW - 1 - r] = r == h - 1 ? (p == 1 ? T_TUBE_TL : T_TUBE_TR) : (p == 1 ? T_TUBE_L : T_TUBE_R);
            if (p == 1 && g->b) *spawn = SPAWN(SP_CHOMP, GROUND_ROW - h);
        }
        break;
    case SEG_STAIRS:
        if (p < g->a) stack(c, (uint8_t)(p + 1));
        else if (g->b == 0 && p == g->a) stack(c, g->a);
        else if (g->b && p < g->a + g->b) c[GROUND_ROW] = c[GROUND_ROW + 1] = SKY;
        else stack(c, (uint8_t)(g->len - p));
        break;
    case SEG_BRIDGE:
        if (p < g->c) break;                     /* the run-up */
        p = (uint8_t)(p - g->c);
        c[GROUND_ROW] = c[GROUND_ROW + 1] = SKY;
        if (p >= 3 && p < g->len - g->c - 3) {
            c[g->a] = T_BRICK;
            if (g->b && p == 4 && g->len - g->c >= 10) *spawn = SPAWN(SP_GLOOP, g->a - 1);
        }
        break;
    case SEG_COINS:
        if (g->a) {                              /* arc over a two-wide pit */
            if (p == 1 || p == 2) c[GROUND_ROW] = c[GROUND_ROW + 1] = SKY;
            c[(p == 0 || p == 3) ? 8 : 7] = T_COIN;
        } else {
            c[8] = T_COIN;
        }
        break;
    case SEG_CANNON:
        if (p == 1) {
            for (r = 0; r < g->a; r++) c[GROUND_ROW - 1 - r] = r == g->a - 1 ? T_CANNON_TOP : T_CANNON;
            *spawn = SPAWN(SP_CANNON, GROUND_ROW - g->a);
        }
        break;
    case SEG_END:
        h = g->a;
        if (p < h) stack(c, (uint8_t)(p + 1));
        else if (p == h) stack(c, h);
        else if (p == h + 4) {                   /* the beacon */
            c[GROUND_ROW - 1] = T_SOLID;
            for (r = 2; r < GROUND_ROW - 1; r++) c[r] = T_POLE;
            c[1] = T_POLE_TOP;
            ret |= GEN_BEACON;
        }
        break;
    }
    GPROF(2);
    decor(g, c);
    GPROF(3);
    g->pos++;
    g->col++;
    if (++g->m48 == 48) g->m48 = 0;
    if (++g->m96 == 96) g->m96 = 0;
    return ret;
}
