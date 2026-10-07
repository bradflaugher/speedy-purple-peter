/* sim_int.h - SPEEDY PURPLE PETER simulation internals shared by sim.c and ents.c.
 * The helpers are HELPER so each Game Boy ROM bank gets its own copy (a call across
 * banks is slow, and a bank cannot read another bank's tables). */
#ifndef SPP_SIM_INT_H
#define SPP_SIM_INT_H
#include <string.h>
#include "sim.h"

#define w (&W)

/* profiling stamps (LY), in a Game Boy build with -DSPP_PROFILE; nothing otherwise */
#if defined(__SDCC) && defined(SPP_PROFILE)
extern uint8_t dbg_sim_ly[16];
#define PROF(i) (dbg_sim_ly[i] = *(volatile uint8_t *)0xFF44)
#else
#define PROF(i) ((void)0)
#endif

/* one private copy per translation unit (per ROM bank); not all are used by both */
#if defined(__GNUC__)
#define HELPER static __attribute__((unused))
#else
#define HELPER static
#endif
#define COLMASK 0x0FFF

/* hot functions keep their locals in static RAM (SST) on the Game Boy: direct addressing is much
   faster than the stack there, and nothing in the simulation is re-entrant */
#ifdef __SDCC
#define SST static
#else
#define SST
#endif
#define ABS16(v) ((uint16_t)((v) < 0 ? -(v) : (v)))

/* in fifties (see add_score): 100 200 400 500 800 1000 2000 4000 5000 8000 */
static const uint8_t score_tab[11] = { 2, 4, 8, 10, 16, 20, 40, 80, 100, 160, 0 };

/* solid cells: ground .. tubes, and the comet launcher */
static const uint8_t solid_tab[T_COUNT] = {
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,   /* sky .. tube */
    0, 0, 0, 0, 1, 1,                                    /* coin, hidden, pole, top, cannon */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0          /* decor */
};
#define solid(t) solid_tab[t]

HELPER uint8_t cell_at(uint16_t col, uint8_t row)
{
    if (row >= LV_ROWS) return T_SKY;
    /* valid: the 32 columns just behind gen_col (8-bit arithmetic is enough: 4096 = 16 * 256) */
    if ((uint8_t)((uint8_t)w->gen_col - (uint8_t)col - 1) >= LV_COLS) return T_SKY;
    return w->lv[(uint8_t)col & (LV_COLS - 1)][row];
}

/* The cell at a pixel position (above or below the level: sky). Every position the simulation
 * asks about is within a few columns of the camera, well inside the ring (the camera's column
 * - 10 .. + 21), so there is no ring check here. This is the hottest function of the game: on
 * the Game Boy it is hand-written (x in DE, y in BC, the cell in A). */
#if defined(__SDCC) && defined(__PORT_sm83)
HELPER uint8_t cell_px(uint16_t x, int16_t y) __naked
{
    (void)x; (void)y;
    __asm
        ld  a, b
        or  a
        jr  nz, 1$              ; y < 0 or y >= 256: sky
        ld  a, c
        cp  #208
        jr  nc, 1$              ; below the level: sky
        swap a
        and #0x0F
        ld  c, a                ; c = row
        ld  a, e
        swap a
        and #0x0F
        ld  b, a
        ld  a, d
        swap a
        and #0x10               ; bit 4 of the column (bits 0-3 come from e)
        or  b                   ; a = column & 31
        ld  l, a
        ld  h, #0
        add hl, hl
        add hl, hl
        add hl, hl
        add hl, hl              ; * 16
        ld  a, l
        or  c
        ld  l, a
        ld  de, #_W
        add hl, de
        ld  a, (hl)
        ret
    1$:
        xor a                   ; T_SKY
        ret
    __endasm;
}
#else
HELPER uint8_t cell_px(uint16_t x, int16_t y)
{
    if ((uint16_t)y >= LV_ROWS * 16) return T_SKY;
    return w->lv[(uint8_t)(x >> 4) & (LV_COLS - 1)][(uint8_t)y >> 4];
}
#endif

#define solid_px(x, y) solid_tab[cell_px((x), (y))]

HELPER void mark(uint16_t col, uint8_t row)
{
    if (w->dirty_n < MAX_DIRTY) {
        w->dirty_col[w->dirty_n] = (uint8_t)col;     /* low 8 bits; the renderer knows the window */
        w->dirty_row[w->dirty_n] = row;
        w->dirty_n++;
    } else {
        w->redraw = 1;
    }
}

HELPER void set_cell(uint16_t col, uint8_t row, uint8_t t)
{
    w->lv[col & (LV_COLS - 1)][row] = t;
    mark(col, row);
}

HELPER uint16_t rng(void)
{
    uint16_t x = w->rngs;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    w->rngs = x;
    return x;
}

/* ------------------------------------------------------------------ score & fx */

HELPER Fx *fx_new(uint8_t kind)
{
    uint8_t i;
    for (i = 0; i < MAX_FX; i++)
        if (!w->fx[i].kind) break;
    if (i == MAX_FX) {                  /* recycle a cosmetic one, never a bump */
        for (i = 0; i < MAX_FX; i++)
            if (w->fx[i].kind != FX_BUMP) break;
        if (i == MAX_FX) return 0;
    } else {
        w->n_fx++;
    }
    memset(&w->fx[i], 0, sizeof(Fx));
    w->fx[i].kind = kind;
    return &w->fx[i];
}

/* The score is kept twice: as a number, and as 7 decimal digits for the HUD (the Game Boy has
   no divide, and turning a 32-bit number into digits every frame is far too slow). */
HELPER void add_score(uint8_t k)    /* k fifties: every award is a multiple of 50, under 10,000 */
{
    SST uint8_t *d;
    SST uint8_t v, c, h, th;
    SST uint16_t pts;
    pts = (uint16_t)((uint16_t)k << 1);                    /* k * 50 = k * 2 + k * 16 + k * 32 */
    pts = (uint16_t)(pts + (pts << 3) + (pts << 4));
    /* the digits to add: tens 0 or 5, then k / 2 hundreds; a decimal add from the tens up,
       stopping as soon as nothing is carried */
    h = (uint8_t)(k >> 1);
    th = 0;
    while (h >= 10) { h -= 10; th++; }
    d = &w->sdig[5];
    c = 0;
    if (k & 1) {
        v = (uint8_t)(*d + 5);
        if (v >= 10) { v -= 10; c = 1; }
        *d = v;
    }
    d--;
    v = (uint8_t)(*d + h + c);
    c = 0;
    if (v >= 10) { v -= 10; c = 1; }
    *d = v;
    d--;
    v = (uint8_t)(*d + th + c);
    c = 0;
    if (v >= 10) { v -= 10; c = 1; }
    *d = v;
    while (c) {
        if (d == w->sdig) {                                /* past 9,999,999: the cap */
            for (v = 0; v < 7; v++) w->sdig[v] = 9;
            w->score = 9999999UL;
            w->score_rev++;
            return;
        }
        d--;
        v = (uint8_t)(*d + 1);
        if (v >= 10) v = 0; else c = 0;
        *d = v;
    }
    w->score += pts;
    w->score_rev++;
}

HELPER void one_up(void)
{
    if (w->lives < 99) w->lives++;
    w->sfx |= EV_1UP;
}

/* award a score step with a pop-up at (x, y) */

HELPER void score_popup(uint8_t step, uint16_t x, int16_t y)
{
    Fx *f = fx_new(FX_SCORE);
    if (f) { f->x = x; f->y = y; f->v = step; f->t = 40; }
}

HELPER void award(uint8_t step, uint16_t x, int16_t y)
{
    if (step > SC_1UP) step = SC_1UP;
    if (step == SC_1UP) one_up();
    else add_score(score_tab[step]);
    score_popup(step, x, y);
}

HELPER void count_coin(void)
{
    w->sfx |= EV_COIN;
    if (++w->coins >= 100) { w->coins = 0; one_up(); }
}

HELPER void get_coin(void)
{
    add_score(4);
    count_coin();
}

HELPER void coin_pop(uint16_t col, uint8_t row)
{
    Fx *f = fx_new(FX_COIN);
    get_coin();                         /* 200 now: dying before the coin lands keeps them */
    if (f) { f->x = (uint16_t)(col * 16 + 4); f->y = (int16_t)(row * 16 - 16); f->vy = -6; f->t = 0; }
}

/* ------------------------------------------------------------------ entities */

HELPER Ent *ent_new(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENTS; i++)
        if (!w->e[i].kind) {
            memset(&w->e[i], 0, sizeof(Ent));
            return &w->e[i];
        }
    return 0;
}

HELPER void ent_flip(Ent *e, int8_t dir)
{
    if (e->kind == E_CANNON) return;
    e->state = ES_FALL;
    e->vy = -0x300;
    e->vx = dir > 0 ? 0x80 : -0x80;
    if (e->kind == E_SHELL || e->kind == E_SHELL_RED || e->kind == E_JET) e->kind = e->kind == E_JET ? E_DOME : e->kind;
}

#define is_enemy(k) ((uint8_t)((k) - E_GLOOP) <= (uint8_t)(E_COMET - E_GLOOP))

#define is_walker(k) ((uint8_t)((k) - E_GLOOP) <= (uint8_t)(E_JET - E_GLOOP))

HELPER void die(uint8_t pit)
{
    if (w->pstate == PS_DEAD) return;
    w->pstate = PS_DEAD;
    w->state_t = 0;
    w->dead_pit = pit;
    w->pvx = 0;
    w->pvy = 0;
    w->power = PW_SMALL;
    w->duck = 0;
    w->nova_t = 0;
    w->sfx |= EV_DIE;
}

HELPER void hurt(void)
{
    if (w->hurt_t || w->nova_t || w->pstate != PS_PLAY || w->god) return;
    if (w->power) {
        w->power = PW_SMALL;
        if (!w->duck) w->py += 16;
        w->duck = 0;
        w->pstate = PS_SHRINK;
        w->state_t = GROW_FRAMES;
        w->hurt_t = HURT_FRAMES;
        w->sfx |= EV_POWERDOWN;
    } else {
        die(0);
    }
}

HELPER void grow(uint8_t power)
{
    if (w->power == PW_SMALL) {
        if (!w->duck) w->py -= 16;
        w->power = PW_BIG;
    } else {
        w->power = power;
    }
    w->duck = 0;
    w->pstate = PS_GROW;
    w->state_t = GROW_FRAMES;
    w->sfx |= EV_POWERUP;
}

#define overlap(ax0, ay0, ax1, ay1, bx0, by0, bx1, by1) \
    ((ax0) <= (bx1) && (bx0) <= (ax1) && (ay0) <= (by1) && (by0) <= (ay1))

/* gravity, walls and floors for walkers and items (16x16 box); returns 1 if it hit a wall */

/* ents.c (its own bank) */
void ents_spawn(uint16_t col, uint8_t sp) SIM_BANKED;
void ents_update(void) SIM_BANKED;
void ents_bump_above(uint16_t col, uint8_t row) SIM_BANKED;

#endif
