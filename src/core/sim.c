/* sim.c - SPEEDY PURPLE PETER simulation core: Peter, blocks, the level ring, camera and clock
 * (portable: SDCC + gcc). Enemies and items are in ents.c. See sim.h. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "sim_int.h"

World W;                 /* the one world */

const uint16_t score_value[11] = { 100, 200, 400, 500, 800, 1000, 2000, 4000, 5000, 8000, 0 };

uint8_t sim_cell(uint16_t col, uint8_t row) SIM_BANKED
{
    return cell_at(col, row);
}

uint8_t sim_visual(uint16_t col, uint8_t row) SIM_BANKED
{
    uint8_t t = cell_at(col, row), i;
    for (i = 0; i < MAX_FX; i++)
        if (w->fx[i].kind == FX_BUMP && w->fx[i].col == (uint8_t)(col & 0xFF) && w->fx[i].row == row
            && w->fx[i].x >> 4 == (col & COLMASK))
            return T_SKY;
    switch (t) {
    case T_HIDDEN_1UP: return T_SKY;
    case T_BRICK_COIN: case T_BRICK_POWER: case T_BRICK_NOVA: return T_BRICK;
    case T_Q_POWER: return T_Q_COIN;
    }
    return t;
}

uint8_t sim_peter_h(void) SIM_BANKED
{
    return (uint8_t)(w->power && !w->duck ? 32 : 16);
}

#define GEN_AHEAD 22
#define GEN_MIN 16
#define GEN_URGENT 13

/* one column of the level; returns nothing, keeps the checkpoint and sector bookkeeping */
static void gen_one(void)
{
    uint8_t k = (uint8_t)(w->gen_col & (LV_COLS - 1));
    uint8_t f = gen_column(&w->gen, w->lv[k], &w->spawn[k]);
        if (f & GEN_SECTOR_START) { w->sec_start_next = w->gen_col; w->sector_next = w->gen.sector; }
        if (f & GEN_CHECKPOINT) w->check_col = w->gen_col;
        w->gen_col = (uint16_t)((w->gen_col + 1) & COLMASK);
}

/* The level is generated GEN_MIN..GEN_AHEAD columns ahead of the camera. Within that slack a
   column is only made on a frame with at most one enemy about: generating is the slowest thing
   the Game Boy does in a frame, and so is a crowd, so the two are kept apart. */
static void generate(void)
{
    uint16_t cc = (uint16_t)(w->cam_x >> 4);
    uint8_t ahead = (uint8_t)((w->gen_col - cc) & COLMASK);
    while (ahead < GEN_URGENT) { gen_one(); ahead++; }
    if (ahead < GEN_MIN || (ahead < GEN_AHEAD && w->n_ents < 2)) {
        /* starting a new segment (rolling its layout) is the slow part: give it its own frame */
        if (w->gen.pos >= w->gen.len) gen_prepare(&w->gen);
        else gen_one();
    }
    while ((uint16_t)((w->spawn_col - cc) & COLMASK) <= 11 && w->spawn_col != w->gen_col) {
        uint8_t s = w->spawn[w->spawn_col & (LV_COLS - 1)];
        if (s) ents_spawn(w->spawn_col, s);
        w->spawn_col = (uint16_t)((w->spawn_col + 1) & COLMASK);
    }
}

void sim_respawn(void) SIM_BANKED
{
    w->power = PW_SMALL;
    w->pstate = PS_PLAY;
    w->hurt_t = 0;
    w->nova_t = 0;
    w->duck = 0;
    w->time = TIME_START;
    w->time_sub = 0;
    w->hurry = 0;
    w->bonus = 0;
    w->cam_x = (uint16_t)(w->sec_start * 16);
    w->cam_y = CAM_Y_MAX;
    gen_begin(&w->gen, w->seed, w->sector);
    w->gen_col = w->sec_start;
    w->spawn_col = w->sec_start;
    w->check_col = 0xFFFF;
    memset(w->e, 0, sizeof(w->e));
    memset(&w->item, 0, sizeof(w->item));
    memset(w->shot, 0, sizeof(w->shot));
    memset(w->fx, 0, sizeof(w->fx));
    w->n_fx = 0;
    w->mc_t = 0;
    w->px = (uint16_t)(w->cam_x + 32);
    w->pxs = 0;
    w->py = GROUND_ROW * 16 - 16;
    w->pys = 0;
    w->pvx = w->pvy = 0;
    w->ground = 1;
    w->jumped = 0;
    w->face = 0;
    w->skid = 0;
    w->chain = 0;
    w->anim = 0;
    w->run_t = 0;
    generate();
    while ((uint8_t)((w->gen_col - (w->cam_x >> 4)) & COLMASK) < GEN_AHEAD) gen_one();  /* all of it now */
    w->redraw = 1;
    w->mev |= MEV_RESPAWN;
}

void sim_init(uint16_t seed, uint8_t mode) SIM_BANKED
{
    memset(&W, 0, sizeof(World));
    w->seed = seed;
    w->mode = mode < MODE_COUNT ? mode : MODE_CLASSIC;
    w->rngs = (uint16_t)(seed ^ 0x5EED) ? (uint16_t)(seed ^ 0x5EED) : 1;
    w->lives = START_LIVES;
    w->sector = 0;
    w->sec_start = 0;
    w->far_col = 0;
    sim_respawn();
}

/* ------------------------------------------------------------------ Peter */

static void sprout(uint16_t col, uint8_t row, uint8_t kind)
{
    Ent *e = &w->item;
    memset(e, 0, sizeof(Ent));
    e->kind = kind;
    e->state = ES_SPROUT;
    e->x = (uint16_t)(col * 16);
    e->y = (int16_t)(row * 16);
    e->t = 32;
    w->sfx |= EV_SPROUT;
}

/* things standing on a block that gets bumped */

static void bump_fx(uint16_t col, uint8_t row, uint8_t final)
{
    Fx *f = fx_new(FX_BUMP);
    if (!f) { mark(col, row); return; }
    f->x = (uint16_t)(col * 16);
    f->y = (int16_t)(row * 16);
    f->col = (uint8_t)col;
    f->row = row;
    f->v = final;
    f->t = 0;
    mark(col, row);
}

static void bump(uint16_t col, uint8_t row)
{
    uint8_t t = cell_at(col, row);
    switch (t) {
    case T_BRICK:
        if (w->power) {
            uint8_t i;
            static const int8_t svx[4] = { -1, 1, -1, 1 }, svy[4] = { -6, -6, -4, -4 };
            ents_bump_above(col, row);
            set_cell(col, row, T_SKY);
            for (i = 0; i < 4; i++) {
                Fx *f = fx_new(FX_SHARD);
                if (!f) break;
                f->x = (uint16_t)(col * 16 + (i & 1) * 8);
                f->y = (int16_t)(row * 16 + (i >> 1) * 8);
                f->vx = svx[i];
                f->vy = svy[i];
            }
            add_score(1);
            w->sfx |= EV_BREAK;
            return;
        }
        ents_bump_above(col, row);
        bump_fx(col, row, T_BRICK);
        w->sfx |= EV_BUMP;
        return;
    case T_BRICK_COIN:
        ents_bump_above(col, row);
        if (!(w->mc_n && w->mc_col == (uint8_t)col && w->mc_row == row)) {
            w->mc_col = (uint8_t)col;
            w->mc_row = row;
            w->mc_t = 240;
            w->mc_n = 0;
        }
        coin_pop(col, row);
        if (++w->mc_n >= 10 || !w->mc_t) {
            w->lv[col & (LV_COLS - 1)][row] = T_USED;
            w->mc_n = 0;
            bump_fx(col, row, T_USED);
        } else {
            bump_fx(col, row, T_BRICK);
        }
        return;
    case T_Q_COIN:
        ents_bump_above(col, row);
        w->lv[col & (LV_COLS - 1)][row] = T_USED;
        coin_pop(col, row);
        bump_fx(col, row, T_USED);
        return;
    case T_Q_POWER: case T_BRICK_POWER: case T_BRICK_NOVA: case T_HIDDEN_1UP:
        ents_bump_above(col, row);
        w->lv[col & (LV_COLS - 1)][row] = T_USED;
        sprout(col, row, t == T_BRICK_NOVA ? E_NOVA : t == T_HIDDEN_1UP ? E_1UP
                            : w->power ? E_BLASTER : E_PLANET);
        bump_fx(col, row, T_USED);
        return;
    default:
        w->sfx |= EV_BUMP;
    }
}

/* collect star bits the box touches */

/* collect star bits the box touches: the cells under its corners (and its middle when big) */
static void touch_coins(int16_t top, int16_t bot)
{
    SST uint8_t c, c1, r0, n, i;
    SST uint8_t *p;
    if (bot < 0) return;
    c = (uint8_t)((w->px + 3) >> 4);
    c1 = (uint8_t)((w->px + 12) >> 4);
    r0 = top < 0 ? 0 : (uint8_t)(top >> 4);
    n = (uint8_t)(bot >> 4);
    if (n >= LV_ROWS) n = LV_ROWS - 1;
    if (r0 > n) return;
    n = (uint8_t)(n - r0);                                 /* rows to look at, less one */
    for (;;) {
        p = &w->lv[c & (LV_COLS - 1)][r0];
        for (i = 0;; i++, p++) {
            if (*p == T_COIN) {
                *p = T_SKY;
                mark(c, (uint8_t)(r0 + i));
                get_coin();
            }
            if (i == n) break;
        }
        if (c == c1) break;
        c++;
    }
}

static void choose_gravity(uint16_t speed)
{
    if (speed < 0x1000) { w->g_hold = 0x200; w->g_fall = 0x700; }
    else if (speed < 0x2500) { w->g_hold = 0x1E0; w->g_fall = 0x600; }
    else { w->g_hold = 0x280; w->g_fall = 0x900; }
}

static void peter_move_x(void)
{
    int16_t t = (int16_t)(w->pxs + w->pvx);          /* |pvx| <= 0x5000: no overflow */
    w->px = (uint16_t)(w->px + (t >> 12));
    w->pxs = (uint16_t)(t & 0xFFF);
}

static void peter_move_y(void)
{
    int16_t t = (int16_t)(w->pys + w->pvy);
    w->py = (int16_t)(w->py + (t >> 12));
    w->pys = (uint16_t)(t & 0xFFF);
}

static void peter_physics(void)
{
    SST uint8_t keys, pressed, h, ht, big;
    SST int8_t dir, mdir;
    SST uint16_t speed;
    SST int16_t oldfeet;
    keys = w->keys;
    if (w->mode == MODE_AUTORUN) keys = (uint8_t)((keys & ~K_LEFT) | K_RIGHT);   /* always right */
    pressed = (uint8_t)(w->keys & ~w->prev);
    dir = (keys & K_RIGHT) ? 1 : (keys & K_LEFT) ? -1 : 0;
    speed = ABS16(w->pvx);
    mdir = w->pvx > 0 ? 1 : w->pvx < 0 ? -1 : 0;

    if ((keys & (K_LEFT | K_RIGHT)) == (K_LEFT | K_RIGHT)) dir = 0;

    if (w->ground) {
        w->duck = (uint8_t)(w->power && (keys & K_DOWN));
        if ((keys & K_B) || w->mode != MODE_CLASSIC) w->run_t = PH_RUN_MEMORY;   /* B runs */
        else if (w->run_t) w->run_t--;
        if (w->duck) dir = 0;
        if (dir && (speed == 0 || dir == mdir)) {
            uint16_t max = w->run_t ? PH_MAX_RUN : PH_MAX_WALK;
            uint16_t acc = w->run_t ? PH_RUN_ACC : PH_WALK_ACC;
            w->face = (uint8_t)(dir < 0);
            w->skid = 0;
            if (speed < PH_MIN_WALK) speed = PH_MIN_WALK;
            else if (speed < max) { speed = (uint16_t)(speed + acc); if (speed > max) speed = max; }
            else if (speed > max) { speed = (uint16_t)(speed - PH_RELEASE_DEC); if (speed < max) speed = max; }
            mdir = dir;
        } else if (dir) {                                  /* skidding */
            w->skid = 1;
            if (speed <= PH_SKID_TURN || speed <= PH_SKID_DEC) {
                speed = 0;
                w->skid = 0;
                w->face = (uint8_t)(dir < 0);
            } else {
                speed = (uint16_t)(speed - PH_SKID_DEC);
            }
        } else {
            w->skid = 0;
            speed = speed > PH_RELEASE_DEC ? (uint16_t)(speed - PH_RELEASE_DEC) : 0;
        }
        w->pvx = mdir < 0 ? -(int16_t)speed : (int16_t)speed;

        if (pressed & K_A) {                               /* jump */
            if (speed < 0x1000) w->pvy = -0x4000;
            else if (speed < 0x2500) w->pvy = -0x4000;
            else w->pvy = -0x5000;
            choose_gravity(speed);
            w->takeoff = speed;
            w->ground = 0;
            w->jumped = 1;
            w->skid = 0;
            w->sfx |= w->power ? EV_JUMP_BIG : EV_JUMP;
        }
    } else {                                               /* air control: facing is locked */
        int8_t f = w->face ? -1 : 1;
        uint16_t max = w->takeoff >= PH_MAX_WALK ? PH_MAX_RUN : PH_MAX_WALK;
        int16_t v = w->face ? (int16_t)-w->pvx : w->pvx;  /* + forward, - backward */
        if (dir == f) {
            uint16_t acc = speed >= PH_MAX_WALK ? PH_RUN_ACC : PH_WALK_ACC;
            v = (int16_t)(v + acc);
            if (v > (int16_t)max) v = (int16_t)max;
        } else if (dir == -f) {
            uint16_t dec = speed >= PH_MAX_WALK ? PH_RUN_ACC
                         : w->takeoff >= PH_AIR_BACK_FAST ? PH_RELEASE_DEC : PH_WALK_ACC;
            v = (int16_t)(v - dec);
            if (v < -(int16_t)max) v = -(int16_t)max;
        }
        w->pvx = w->face ? (int16_t)-v : v;
        w->skid = 0;
    }

    PROF(8);
    /* blaster */
    if ((pressed & K_B) && w->power == PW_BLASTER && !w->duck) {
        uint8_t i;
        for (i = 0; i < MAX_SHOTS; i++)
            if (!w->shot[i].kind) {
                Ent *s = &w->shot[i];
                memset(s, 0, sizeof(Ent));
                s->kind = 1;
                s->x = (uint16_t)(w->px + (w->face ? -4 : 12));
                s->y = (int16_t)(w->py + 8);
                s->vx = w->face ? -0x400 : 0x400;
                s->vy = 0x300;
                w->sfx |= EV_SHOT;
                w->shoot_t = 8;
                break;
            }
    }

    big = (uint8_t)(w->power && !w->duck);
    h = (uint8_t)(w->power ? 32 : 16);
    ht = (uint8_t)(h - (big ? 32 : 16));                   /* hitbox top offset */

    PROF(9);
    /* horizontal move and walls */
    peter_move_x();
    {
        SST int16_t y0, y1, y2;
        SST uint8_t hitl, hitr;
        SST uint16_t xl, xr;
        hitl = hitr = 0;
        if (big) { y0 = (int16_t)(w->py + 6); y1 = (int16_t)(w->py + 16); y2 = (int16_t)(w->py + 27); }
        else { y0 = (int16_t)(w->py + ht + 6); y1 = (int16_t)(w->py + ht + 12); y2 = y1; }
        xl = (uint16_t)(w->px + 2);
        xr = (uint16_t)(w->px + 13);
        /* only the side he is moving towards (both when standing still) */
        if (w->pvx <= 0)
            hitl = (uint8_t)(solid_px(xl, y0) || solid_px(xl, y1) || (big && solid_px(xl, y2)));
        if (w->pvx >= 0)
            hitr = (uint8_t)(solid_px(xr, y0) || solid_px(xr, y1) || (big && solid_px(xr, y2)));
        if (hitr && !hitl) {
            w->px = (uint16_t)(((w->px + 13) & ~15) - 14);
            if (w->pvx > 0) w->pvx = 0;
        } else if (hitl && !hitr) {
            w->px = (uint16_t)(((w->px + 2) & ~15) + 16 - 2);
            if (w->pvx < 0) w->pvx = 0;
        }
    }
    /* the screen's left edge is a wall */
    if ((uint16_t)(w->px - w->cam_x) >= 0x8000u) {
        w->px = w->cam_x;
        if (w->pvx < 0) w->pvx = 0;
    }

    PROF(10);
    /* vertical (no gravity on the takeoff frame) */
    if (!w->ground && !(w->jumped && (pressed & K_A) && w->pvy <= -0x4000)) {
        uint16_t g = (w->jumped && (keys & K_A) && w->pvy < 0) ? w->g_hold : w->g_fall;
        w->pvy = (int16_t)(w->pvy + g);
        if (w->pvy >= PH_MAX_FALL) w->pvy = PH_FALL_RESET;
    }
    oldfeet = (int16_t)(w->py + h);
    peter_move_y();
    if (w->pvy < 0) {
        int16_t hy = (int16_t)(w->py + ht + 2);
        uint16_t hx = (uint16_t)(w->px + 8);
        uint8_t t = cell_px(hx, hy);
        if (hy >= 0 && (solid(t) || t == T_HIDDEN_1UP)) {
            w->py = (int16_t)(((hy >> 4) + 1) * 16 - ht - 2);
            w->pvy = 0;
            bump((uint16_t)(hx >> 4), (uint8_t)(hy >> 4));
        }
    } else {
        int16_t fy = (int16_t)(w->py + h);
        uint8_t l = solid_px((uint16_t)(w->px + 3), fy), r = solid_px((uint16_t)(w->px + 12), fy);
        int16_t top = (int16_t)(fy & ~15);
        if ((l || r) && fy >= 0 && oldfeet <= top) {
            w->py = (int16_t)(top - h);
            w->pvy = 0;
            w->pys = 0;
            if (!w->ground) w->chain = 0;
            w->ground = 1;
            w->jumped = 0;
        } else if (w->ground) {                            /* walked off a ledge */
            w->ground = 0;
            w->jumped = 0;
            w->takeoff = ABS16(w->pvx);
            choose_gravity(w->takeoff);
        }
    }

    PROF(11);
    /* run animation */
    if (w->ground) {
        w->anim_t = (uint16_t)(w->anim_t + (ABS16(w->pvx) >> 4));
        while (w->anim_t >= 7 * 256) { w->anim_t -= 7 * 256; if (++w->anim == 3) w->anim = 0; }
        if (!w->pvx) w->anim = 0;
    }
    if (w->shoot_t) w->shoot_t--;

    PROF(12);
    touch_coins((int16_t)(w->py + ht + 2), (int16_t)(w->py + h - 2));
    PROF(13);

    if (w->py > LV_ROWS * 16 + 8) die(1);
}

/* the checkpoint: the sector's last column. Nothing marks it (the level just goes on): running
   past it moves the restart point on, refills the clock and counts the time left into the score */
static void checkpoint(void)
{
    if (w->check_col == 0xFFFF) return;
    /* (written as an unsigned test: SDCC 4.3-4.5 miscompiles "(int16_t)(px + 12 - x) < 0" here,
       testing the low byte's sign; tests/test_rom.py would catch it) */
    if ((uint16_t)(w->px + 12 - (uint16_t)(w->check_col * 16 + 7)) >= 0x8000u) return;
    w->check_col = 0xFFFF;
    w->sfx |= EV_CHECKPOINT;
    w->sector = w->sector_next;
    w->sec_start = w->sec_start_next;
    w->sectors_done++;
    w->bonus = w->time;
}

/* ------------------------------------------------------------------ entities */

/* The camera. Horizontally it only moves forward, keeping Peter at CAM_LEAD. Vertically it
 * keeps the ground in view like the classic game: standing on the ground the view shows the
 * ground's two rows at the bottom, and an ordinary jump never moves it. It only rises when Peter
 * climbs onto something high (or his feet leave the top of the view), and it eases back down. */
static void camera(void)
{
    SST int16_t sx, feet, target, sy;
    SST uint16_t cc;
    sx = (int16_t)(w->px - w->cam_x);
    feet = (int16_t)(w->py + (w->power ? 32 : 16));
    target = (int16_t)w->cam_y;
    if (sx > CAM_LEAD) w->cam_x = (uint16_t)(w->cam_x + (sx - CAM_LEAD));
    if (w->ground && w->cam_y == CAM_Y_MAX && feet >= GROUND_ROW * 16) goto dist;  /* the usual */
    sy = (int16_t)(feet - w->cam_y);
    if (w->ground) target = (int16_t)(feet - (VIEW_H - 32));        /* settle: feet 32 px up */
    else if (sy < 8) target = (int16_t)(feet - 8);                 /* high above: follow up */
    else if (sy > VIEW_H - 8) target = (int16_t)(feet - (VIEW_H - 8)); /* falling: follow down */
    if (target < 0) target = 0;
    if (target > CAM_Y_MAX) target = CAM_Y_MAX;
    if (target < (int16_t)w->cam_y - 4) target = (int16_t)w->cam_y - 4;
    if (target > (int16_t)w->cam_y + 4) target = (int16_t)w->cam_y + 4;
    w->cam_y = (uint8_t)target;
dist:
    cc = (uint16_t)(w->cam_x >> 4);
    if ((uint8_t)cc != (uint8_t)w->far_col) {       /* (cheap test first) */
        uint16_t d = (uint16_t)((cc - w->far_col) & COLMASK);
        if (d && d < 2048) {
            w->far_col = cc;
            w->dist = (uint16_t)(w->dist + d < w->dist ? 0xFFFF : w->dist + d);
        }
    }
}

static void tick_time(void)
{
    if (w->bonus) {                                        /* count the time bonus in */
        uint8_t n;
        if (w->frames & 1) return;                         /* 4 units every other frame */
        n = w->bonus >= 4 ? 4 : (uint8_t)w->bonus;
        w->bonus -= n;
        w->time -= n;
        add_score(n);                                      /* 50 points a unit */
        w->sfx |= EV_TICK;
        if (!w->bonus) {
            w->time = TIME_START;
            w->time_sub = 0;
            if (w->hurry) { w->hurry = 0; w->mev |= MEV_RESPAWN; }
        }
        return;
    }
    if (++w->time_sub < TIME_FRAMES) return;
    w->time_sub = 0;
    if (w->time) w->time--;
    if (w->time == 100 && !w->hurry) { w->hurry = 1; w->mev |= MEV_HURRY; }
    if (!w->time) die(0);
}

void sim_step(uint8_t keys) SIM_BANKED
{
    w->sfx = 0;
    w->mev = 0;
    w->dirty_n = 0;
    w->redraw = 0;
    w->prev = w->keys;
    w->keys = keys;
    w->frozen = 0;
    if (w->over) return;

    if (w->pstate == PS_GROW || w->pstate == PS_SHRINK) {
        w->frozen = 1;
        if (!--w->state_t) w->pstate = PS_PLAY;
        return;
    }
    if (w->pstate == PS_DEAD) {
        w->frozen = 1;
        w->state_t++;
        if (!w->dead_pit && w->state_t >= 30) {
            if (w->state_t == 30) w->pvy = -0x4000;
            w->pvy = (int16_t)(w->pvy + 0x200);
            if (w->pvy > PH_MAX_FALL) w->pvy = PH_MAX_FALL;
            peter_move_y();
        }
        if (w->state_t >= 180) {
            if (w->lives) w->lives--;
            if (!w->lives) { w->over = 1; w->pstate = PS_OVER; w->mev |= MEV_GAMEOVER; }
            else sim_respawn();
        }
        return;
    }

    w->frames++;
    if (w->hurt_t) w->hurt_t--;
    if (w->nova_t && !--w->nova_t) w->mev |= MEV_NOVA_END;
    if (w->mc_t) w->mc_t--;

    PROF(0);
    peter_physics();
    PROF(1);
    if (w->pstate != PS_PLAY) { camera(); return; }
    checkpoint();
    camera();
    PROF(2);
    generate();
    PROF(3);
    ents_update();
    PROF(4);
    if (w->pstate == PS_PLAY) tick_time();
    PROF(15);
}

