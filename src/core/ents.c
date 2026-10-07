/* ents.c - SPEEDY PURPLE PETER enemies, power-ups, shots and effects (portable C). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "sim_int.h"

#if defined(__SDCC) && defined(SPP_PROFILE)       /* profiling stamps (see sim_int.h) */
extern uint8_t dbg_ent_ly[MAX_ENTS];
static uint8_t ent_ly0;
#define ENT_PROF_A() (ent_ly0 = *(volatile uint8_t *)0xFF44)
#define ENT_PROF_B(i) (dbg_ent_ly[i] = (uint8_t)(*(volatile uint8_t *)0xFF44 - ent_ly0))
#else
#define ENT_PROF_A() ((void)0)
#define ENT_PROF_B(i) ((void)0)
#endif

void ents_spawn(uint16_t col, uint8_t sp) SIM_BANKED
{
    uint8_t k = (uint8_t)(sp >> 4), row = (uint8_t)(sp & 15);
    Ent *e;
    if (w->god) return;
    e = ent_new();
    if (!e) return;
    e->x = (uint16_t)(col * 16);
    e->y = (int16_t)(row * 16);
    e->vx = -0x80;
    switch (k) {
    case SP_GLOOP: e->kind = E_GLOOP; break;
    case SP_DOME: e->kind = E_DOME; break;
    case SP_DOME_RED: e->kind = E_DOME_RED; break;
    case SP_JET: e->kind = E_JET; break;
    case SP_CHOMP:
        e->kind = E_CHOMP;
        e->x = (uint16_t)(col * 16 + 8);
        e->vx = 0;
        e->t = 30;
        e->t2 = 0;
        e->chain = row;                 /* the tube mouth's row */
        break;
    case SP_CANNON:
        e->kind = E_CANNON;
        e->vx = 0;
        e->t = 40;
        break;
    default:
        e->kind = E_NONE;
    }
}

/* ------------------------------------------------------------------ the entity being updated
 * The entity being updated is copied out of its slot into E, a plain global, and back: SDCC
 * reaches a global's fields directly, while every access through a pointer costs an address
 * calculation (the copy is 16 bytes in assembly). cur is the slot. */
static Ent E;
static Ent *cur;
static Ent *const slots = &W.e[0];  /* (W.e itself is out of reach under the macro below) */

typedef char ent_is_16_bytes[sizeof(Ent) == 16 ? 1 : -1];   /* ent_copy copies 16 */

#if defined(__SDCC) && defined(__PORT_sm83)
static void ent_copy(void *dst, const void *src) __naked     /* 16 bytes; dst in DE, src in BC */
{
    (void)dst; (void)src;
    __asm
        ld  h, b
        ld  l, c
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ret
    __endasm;
}
#else
static void ent_copy(void *dst, const void *src) { memcpy(dst, src, sizeof(Ent)); }
#endif
/* a bit (1 << slot) for every used slot whose x is in [x0, x0 + near_w - 1]: the cheap first test
   of an overlap (for 16x16 boxes, x0 = x - 15 and near_w = 31) */
static uint8_t near_w;
#if defined(__SDCC) && defined(__PORT_sm83)
static uint8_t near_t;
static uint8_t near_x(const Ent *s, uint16_t x0) __naked     /* s in DE, x0 in BC; result in A */
{
    (void)s; (void)x0;
    __asm
        ld  h, d
        ld  l, e
        ld  de, #0x0100             ; d = the slot bit, e = the result
    1$:
        push hl
        ld  a, (hl+)                ; kind
        or  a, a
        jr  z, 2$
        inc hl
        ld  a, (hl+)                ; x - x0, wanted in 0..30
        sub a, c
        ld  (_near_t), a
        ld  a, (hl)
        sbc a, b
        jr  nz, 2$
        ld  hl, #_near_w
        ld  a, (_near_t)
        cp  a, (hl)
        jr  nc, 2$
        ld  a, e
        or  a, d
        ld  e, a
    2$:
        pop hl
        ld  a, l
        add a, #16
        ld  l, a
        jr  nc, 3$
        inc h
    3$:
        sla d
        bit 6, d                    ; six slots
        jr  z, 1$
        ld  a, e
        ret
    __endasm;
}
#else
static uint8_t near_x(const Ent *s, uint16_t x0)
{
    uint8_t i, m = 0;
    for (i = 0; i < MAX_ENTS; i++)
        if (s[i].kind && (uint16_t)(s[i].x - x0) < near_w) m |= (uint8_t)(1 << i);
    return m;
}
#endif
#define e (&E)

/* gravity, walls and floors for walkers and items (16x16 box); returns 1 if it hit a wall */
static uint8_t ent_physics(int16_t grav, int16_t maxfall)
{
    SST uint8_t wall, xl, slow;
    SST int16_t sum, oldfeet, fy, top;
    wall = 0;
    sum = (int16_t)((int16_t)e->xs + e->vx);
    e->x = (uint16_t)(e->x + (sum >> 8));
    e->xs = (uint8_t)sum;
    /* A slow mover (at most 1 px a frame) steps through every x, so a probe point can only enter a
       new cell at one value of x & 15: the level is only looked at then (it does not change under a
       walker, except by a bump, which knocks the walker out anyway). */
    xl = (uint8_t)((uint8_t)e->x & 15);
    slow = (uint8_t)(e->vx <= 0x100 && e->vx >= -0x100);
    if (e->vx > 0) {
        if ((!slow || xl == 2) && solid_px((uint16_t)(e->x + 14), (int16_t)(e->y + 8))) {
            e->x = (uint16_t)(((e->x + 14) & ~15) - 15);
            wall = 1;
        }
    } else if (e->vx < 0) {
        if ((!slow || xl == 14) && solid_px((uint16_t)(e->x + 1), (int16_t)(e->y + 8))) {
            e->x = (uint16_t)(((e->x + 1) & ~15) + 15);
            wall = 1;
        }
    }
    if (e->ground && slow && !wall && xl != 13 && xl != 4) return 0;   /* still on the same cells */
    if (!e->ground) {
        e->vy = (int16_t)(e->vy + grav);
        if (e->vy > maxfall) e->vy = maxfall;
    }
    oldfeet = (int16_t)(e->y + 16);
    sum = (int16_t)((int16_t)e->ys + e->vy);
    e->y = (int16_t)(e->y + (sum >> 8));
    e->ys = (uint8_t)sum;
    fy = (int16_t)(e->y + 16);
    top = (int16_t)(fy & ~15);
    if (e->vy >= 0 && fy >= 0 && oldfeet <= top + 1 &&
        (solid_px((uint16_t)(e->x + 3), fy) || solid_px((uint16_t)(e->x + 12), fy))) {
        e->y = (int16_t)(top - 16);
        e->vy = 0;
        e->ys = 0;
        e->ground = 1;
    } else {
        e->ground = 0;
    }
    return wall;
}

#undef e
static void ent_kill_by(Ent *e, uint8_t step, int8_t dir)
{
    award(step, e->x, e->y);
    ent_flip(e, dir);
    w->sfx |= EV_KICK;
}
#define e (&E)

static void update_ent(void)
{
    SST int16_t sx;
    sx = (int16_t)(e->x - w->cam_x);
    if ((uint16_t)(sx + 48) > 48 + 272 || e->y > LV_ROWS * 16 + 32) { e->kind = E_NONE; return; }

    if (e->state == ES_FLAT) {
        if (!--e->t) e->kind = E_NONE;
        return;
    }
    if (e->state == ES_FALL) {
        int16_t sum = (int16_t)((int16_t)e->xs + e->vx);
        e->x = (uint16_t)(e->x + (sum >> 8));
        e->xs = (uint8_t)(sum & 0xFF);
        e->vy = (int16_t)(e->vy + 0x40);
        if (e->vy > 0x400) e->vy = 0x400;
        sum = (int16_t)((int16_t)e->ys + e->vy);
        e->y = (int16_t)(e->y + (sum >> 8));
        e->ys = (uint8_t)(sum & 0xFF);
        return;
    }

    /* walkers wait just off the right edge until the screen reaches them (as in the classic game) */
    if (sx >= VIEW_W && is_walker(e->kind)) return;

    switch (e->kind) {
    case E_GLOOP: case E_DOME: case E_DOME_RED:
        if (ent_physics(0x60, 0x400)) e->vx = (int16_t)-e->vx;
        if (e->kind == E_DOME_RED && e->ground) {          /* turn at ledges */
            /* (only when the front foot enters a new cell: x & 15 == 3 going right, 13 going left) */
            uint8_t xl = (uint8_t)((uint8_t)e->x & 15);
            if (e->vx > 0 ? xl == 3 : xl == 13) {
                uint16_t fx = (uint16_t)(e->vx > 0 ? e->x + 13 : e->x + 2);
                if (!solid_px(fx, (int16_t)(e->y + 17))) e->vx = (int16_t)-e->vx;
            }
        }
        e->t++;
        break;
    case E_JET:
        if (ent_physics(0x30, 0x400)) e->vx = (int16_t)-e->vx;
        if (e->ground) { e->vy = -0x380; e->ground = 0; }
        e->t++;
        break;
    case E_SHELL: case E_SHELL_RED:
        if (e->t2) e->t2--;
        if (ent_physics(0x60, 0x400) && e->vx) {
            e->vx = (int16_t)-e->vx;
            if ((int16_t)(e->x - w->cam_x) < 176) w->sfx |= EV_BUMP;
        }
        if (!e->vx) {                                      /* waking up */
            if (e->t) e->t--;
            if (!e->t) {
                e->kind = e->kind == E_SHELL ? E_DOME : E_DOME_RED;
                e->vx = (int16_t)(w->px - e->x) < 0 ? -0x80 : 0x80;
            }
        } else {                                           /* sliding: knock others over */
            SST Ent *o;
            SST uint8_t m;
            near_w = 31;
            m = near_x(slots, (uint16_t)(e->x - 15));    /* the boxes (16x16) overlap */
            for (o = slots; m; o++, m >>= 1) {
                if (!(m & 1) || o == cur) continue;
                if (!is_enemy(o->kind) || o->kind == E_CHOMP || o->state != ES_LIVE) continue;
                if ((uint16_t)(o->y - e->y + 15) <= 30) {
                    ent_kill_by(o, (uint8_t)(SC_500 + e->chain), e->vx > 0 ? 1 : -1);
                    if (e->chain < SC_1UP - SC_500) e->chain++;
                }
            }
        }
        break;
    case E_CHOMP:                                          /* (see chomp_step) */
        break;
    case E_COMET: {
        int16_t sum = (int16_t)((int16_t)e->xs + e->vx);
        e->x = (uint16_t)(e->x + (sum >> 8));
        e->xs = (uint8_t)(sum & 0xFF);
        break;
    }
    case E_CANNON:
        if (sx < -16) { e->kind = E_NONE; break; }
        if (e->t) e->t--;
        if (!e->t) {
            int16_t d = (int16_t)(w->px + 8 - (e->x + 8));
            e->t = (uint8_t)(110 + (rng() & 63));
            if (sx >= 0 && sx < VIEW_W && (d > 40 || d < -40)) {
                Ent *c = ent_new();
                if (c) {
                    c->kind = E_COMET;
                    c->x = e->x;
                    c->y = e->y;
                    c->vx = d < 0 ? -0x140 : 0x140;
                    w->sfx |= EV_LAUNCH;
                }
            }
        }
        break;
    }
}

#undef e

/* Walkers turn around when they bump into each other. Each frame two of the six slots are
   checked against the rest, so every walker is looked at every third frame: they move half a pixel
   a frame, so a turn a frame or two late is invisible, and a crowd costs a few checks a frame. */
static void bump_one(Ent *a)
{
    SST Ent *b;
    SST int16_t dx, dy;
    SST uint8_t m;
    if (!(a->state == ES_LIVE && (is_walker(a->kind) || ((a->kind == E_SHELL || a->kind == E_SHELL_RED) && !a->vx))))
        return;
    near_w = 29;
    m = near_x(slots, (uint16_t)(a->x - 14));            /* within 14 px */
    for (b = slots; m; b++, m >>= 1) {
        if (!(m & 1) || b == a) continue;
        dx = (int16_t)(b->x - a->x);
        if (!is_walker(a->kind) && !is_walker(b->kind)) continue;     /* two resting pods */
        if (!(is_walker(b->kind) || ((b->kind == E_SHELL || b->kind == E_SHELL_RED) && !b->vx))) continue;
        if (b->state != ES_LIVE) continue;
        dy = (int16_t)(b->y - a->y);
        if (dy > 14 || dy < -14) continue;
        if (dx >= 0) { if (a->vx > 0) a->vx = (int16_t)-a->vx; if (b->vx < 0) b->vx = (int16_t)-b->vx; }
        else { if (a->vx < 0) a->vx = (int16_t)-a->vx; if (b->vx > 0) b->vx = (int16_t)-b->vx; }
    }
}

static void ent_bumps(void)
{
    if (++w->bump_i >= MAX_ENTS / 2) w->bump_i = 0;
    bump_one(&w->e[w->bump_i]);
    bump_one(&w->e[w->bump_i + MAX_ENTS / 2]);
}

static void peter_vs_ents(void)
{
    SST uint8_t h, m;
    SST Ent *e;
    SST int16_t px0, px1, py0, py1, feet, ex, top;
    near_w = 41;
    m = near_x(slots, (uint16_t)(w->px - 20));            /* within 20 px: the cheap test */
    if (!m) return;
    h = (uint8_t)(w->power && !w->duck ? 32 : 16);
    px0 = (int16_t)(w->px - w->cam_x) + 3;
    px1 = px0 + 9;
    py0 = (int16_t)(w->py + (w->power ? 32 : 16) - h + 4);
    py1 = (int16_t)(w->py + (w->power ? 32 : 16) - 1);
    feet = (int16_t)(py1 + 1);
    for (e = slots; m; e++, m >>= 1) {
        if (!(m & 1) || e->kind == E_CANNON || e->state != ES_LIVE) continue;
        ex = (int16_t)(e->x - w->cam_x);
        if (e->kind == E_CHOMP) {
            int16_t mouth = (int16_t)(e->chain * 16);
            if (e->y + 4 >= mouth) continue;
            if (!overlap(px0, py0, px1, py1, (int16_t)(ex + 3), (int16_t)(e->y + 4), (int16_t)(ex + 12), (int16_t)(mouth - 1))) continue;
            if (w->nova_t) ent_kill_by(e, SC_200, 1);
            else hurt();
            continue;
        }
        if (!overlap(px0, py0, px1, py1, (int16_t)(ex + 2), (int16_t)(e->y + 3), (int16_t)(ex + 13), (int16_t)(e->y + 15)))
            continue;
        top = (int16_t)(e->y + 3);
        if (w->nova_t) {
            ent_kill_by(e, w->chain < SC_8000 ? w->chain : SC_8000, (int16_t)(e->x - w->px) >= 0 ? 1 : -1);
            if (w->chain < SC_1UP) w->chain++;
            continue;
        }
        if ((e->kind == E_SHELL || e->kind == E_SHELL_RED) && !e->vx) {     /* kick */
            int8_t dir = (int16_t)(w->px + 8 - (e->x + 8)) <= 0 ? 1 : -1;
            e->vx = dir > 0 ? 0x300 : -0x300;
            e->t2 = 10;
            e->chain = 0;
            if (w->pvy > 0 && feet < top + 12) {
                w->pvy = PH_STOMP_VY;
                w->jumped = 1;
            }
            award(SC_400, e->x, e->y);
            w->sfx |= EV_KICK;
            continue;
        }
        if (w->pvy > 0 && feet < top + 12) {               /* stomp */
            award(w->chain, e->x, e->y);
            if (w->chain < SC_1UP) w->chain++;
            w->pvy = PH_STOMP_VY;
            w->jumped = 1;
            w->sfx |= EV_STOMP;
            switch (e->kind) {
            case E_GLOOP: e->state = ES_FLAT; e->t = 30; break;
            case E_DOME: case E_DOME_RED:
                e->kind = e->kind == E_DOME ? E_SHELL : E_SHELL_RED;
                e->vx = 0; e->t = 255; e->t2 = 0;
                break;
            case E_SHELL: case E_SHELL_RED:               /* stop a moving shell */
                e->vx = 0; e->t = 255; e->t2 = 0;
                break;
            case E_JET: e->kind = E_DOME; e->vx = -0x80; e->vy = 0; break;
            case E_COMET: e->state = ES_FALL; e->vy = 0; e->vx = 0; break;
            }
            continue;
        }
        if ((e->kind == E_SHELL || e->kind == E_SHELL_RED) && e->t2) continue;  /* just kicked */
        hurt();
    }
}

static void update_item(void)
{
    Ent *e = &w->item;
    int16_t sx;
    uint8_t h;
    if (!e->kind) return;
    sx = (int16_t)(e->x - w->cam_x);
    if (sx < -32 || sx > 240 || e->y > LV_ROWS * 16 + 16) { e->kind = E_NONE; return; }
    if (e->state == ES_SPROUT) {
        if (e->t & 1) e->y--;
        if (!--e->t) {
            e->state = ES_LIVE;
            if (e->kind != E_BLASTER) e->vx = 0x100;
            if (e->kind == E_NOVA) { e->vy = -0x300; }
        }
    } else if (e->kind != E_BLASTER) {
        ent_copy(&E, e);                                   /* ent_physics works on E */
        cur = e;
        if (E.kind == E_NOVA) {
            if (ent_physics(0x30, 0x400)) E.vx = (int16_t)-E.vx;
            if (E.ground) { E.vy = -0x480; E.ground = 0; }
        } else if (ent_physics(0x60, 0x400)) {
            E.vx = (int16_t)-E.vx;
        }
        ent_copy(e, &E);
    }
    if (w->pstate != PS_PLAY) return;
    h = sim_peter_h();
    if (overlap((int16_t)(w->px - w->cam_x) + 3, (int16_t)(w->py + (w->power ? 32 : 16) - h + 2),
                (int16_t)(w->px - w->cam_x) + 12, (int16_t)(w->py + (w->power ? 32 : 16) - 1),
                (int16_t)(sx + 2), (int16_t)(e->y + 2), (int16_t)(sx + 13), (int16_t)(e->y + 15))) {
        uint8_t k = e->kind;
        e->kind = E_NONE;
        switch (k) {
        case E_PLANET:
            award(SC_1000, e->x, e->y);
            if (!w->power) grow(PW_BIG); else w->sfx |= EV_POWERUP;
            break;
        case E_BLASTER:
            award(SC_1000, e->x, e->y);
            if (w->power != PW_BLASTER) grow(PW_BLASTER); else w->sfx |= EV_POWERUP;
            break;
        case E_NOVA:
            award(SC_1000, e->x, e->y);
            w->nova_t = NOVA_FRAMES;
            w->chain = 0;
            w->mev |= MEV_NOVA;
            w->sfx |= EV_POWERUP;
            break;
        case E_1UP:
            award(SC_1UP, e->x, e->y);
            break;
        }
    }
}

static void update_shots(void)
{
    Ent *s, *e;
    if (!w->shot[0].kind && !w->shot[1].kind) return;
    for (s = w->shot; s != w->shot + MAX_SHOTS; s++) {
        int16_t sum, sx;
        Fx *f;
        if (!s->kind) continue;
        sum = (int16_t)((int16_t)s->xs + s->vx);
        s->x = (uint16_t)(s->x + (sum >> 8));
        s->xs = (uint8_t)(sum & 0xFF);
        sx = (int16_t)(s->x - w->cam_x);
        if (sx < -8 || sx > VIEW_W + 8 || s->y > LV_ROWS * 16) { s->kind = 0; continue; }
        if (solid_px((uint16_t)(s->x + 4), (int16_t)(s->y + 4))) goto puff;
        s->vy = (int16_t)(s->vy + 0x40);
        if (s->vy > 0x400) s->vy = 0x400;
        sum = (int16_t)((int16_t)s->ys + s->vy);
        s->y = (int16_t)(s->y + (sum >> 8));
        s->ys = (uint8_t)(sum & 0xFF);
        if (solid_px((uint16_t)(s->x + 4), (int16_t)(s->y + 7))) {
            if (s->vy > 0) {
                s->y = (int16_t)(((s->y + 7) & ~15) - 8);
                s->vy = -0x280;
            } else goto puff;
        }
        for (e = w->e; e != w->e + MAX_ENTS; e++) {
            int16_t ey0, ey1;
            if (!is_enemy(e->kind) || e->state != ES_LIVE) continue;
            ey0 = (int16_t)(e->y + 2);
            ey1 = (int16_t)(e->y + 15);
            if (e->kind == E_CHOMP) { ey1 = (int16_t)(e->chain * 16 - 1); if (ey0 > ey1) continue; }
            if (!overlap((int16_t)s->x, s->y, (int16_t)(s->x + 7), (int16_t)(s->y + 7),
                         (int16_t)e->x, ey0, (int16_t)(e->x + 15), ey1)) continue;
            if (e->kind != E_COMET)
                ent_kill_by(e, e->kind == E_GLOOP ? SC_100 : SC_200, s->vx > 0 ? 1 : -1);
            goto puff;
        }
        continue;
    puff:
        f = fx_new(FX_PUFF);
        if (f) { f->x = s->x; f->y = s->y; f->t = 12; }
        s->kind = 0;
    }
}

static void update_fx(void)
{
    SST Fx *f;
    SST uint8_t left;
    static const int8_t bump_dy[9] = { -2, -4, -5, -6, -5, -4, -2, -1, 0 };
    left = w->n_fx;                                        /* stop after the last one */
    if (!left) return;
    for (f = w->fx;; f++) {
        if (!f->kind) continue;
        switch (f->kind) {
        case FX_COIN:
            f->y = (int16_t)(f->y + f->vy);
            f->vy++;
            if (++f->t >= 12) {
                uint16_t x = f->x;
                int16_t y = f->y;
                f->kind = FX_NONE; w->n_fx--;
                award(SC_200, x, y);
                /* (its pop-up takes the first free slot: this one or an earlier one) */
            }
            break;
        case FX_SCORE:
            if (f->t > 10) f->y--;
            if (!--f->t) { f->kind = FX_NONE; w->n_fx--; }
            break;
        case FX_SHARD:
            f->x = (uint16_t)(f->x + f->vx);
            f->y = (int16_t)(f->y + f->vy);
            if (++f->t & 1) f->vy++;
            if (f->y > LV_ROWS * 16 + 16) { f->kind = FX_NONE; w->n_fx--; }
            break;
        case FX_PUFF:
            if (!--f->t) { f->kind = FX_NONE; w->n_fx--; }
            break;
        case FX_BUMP:
            f->vy = bump_dy[f->t];
            if (++f->t >= 9) {
                uint16_t col = (uint16_t)(f->x >> 4);
                f->kind = FX_NONE; w->n_fx--;
                mark(col, f->row);
            }
            break;
        }
        if (!--left) break;
    }
}

void ents_bump_above(uint16_t col, uint8_t row) SIM_BANKED
{
    Ent *e;
    uint16_t bx = (uint16_t)(col * 16);
    int16_t top = (int16_t)(row * 16);
    for (e = w->e; e != w->e + MAX_ENTS; e++) {
        if (!is_walker(e->kind) && e->kind != E_SHELL && e->kind != E_SHELL_RED) continue;
        if (e->state != ES_LIVE) continue;
        if (e->y + 16 < top - 4 || e->y + 16 > top + 4) continue;
        if ((uint16_t)(e->x + 13 - bx) > 26) continue;
        award(SC_100, e->x, e->y);
        ent_flip(e, (int16_t)(e->x - bx) >= 0 ? 1 : -1);
        w->sfx |= EV_KICK;
    }
    if (w->item.kind && w->item.state == ES_LIVE && w->item.ground
        && (uint16_t)(w->item.x + 13 - bx) <= 26 && w->item.y + 16 >= top - 4 && w->item.y + 16 <= top + 4) {
        w->item.vy = -0x300;
        w->item.ground = 0;
        if (w->item.vx) w->item.vx = (int16_t)(w->item.x + 8) >= (int16_t)(bx + 8) ? 0x100 : -0x100;
    }
    if (row && cell_at(col, (uint8_t)(row - 1)) == T_COIN) {
        set_cell(col, (uint8_t)(row - 1), T_SKY);
        coin_pop(col, row);
    }
}

/* a Moon Chomper: hidden in its tube until Peter is not right next to it, then up, bite, down.
   Simple enough to be done in place (see ents_update). */
static void chomp_step(Ent *e)
{
    int16_t mouth = (int16_t)(e->chain * 16);
    switch (e->t2) {
    case 0:                                            /* hidden */
        e->y = mouth;
        if (e->t) e->t--;
        if (!e->t) {
            int16_t d = (int16_t)(w->px + 8 - (e->x + 8));
            if (d > 28 || d < -28) e->t2 = 1;
            else e->t = 8;
        }
        break;
    case 1:                                            /* rising */
        e->y--;
        if (e->y <= mouth - 24) { e->t2 = 2; e->t = 60; }
        break;
    case 2:                                            /* up, biting */
        if (!--e->t) e->t2 = 3;
        break;
    default:                                           /* sinking */
        e->y++;
        if (e->y >= mouth) { e->t2 = 0; e->t = 60; }
    }
    e->ys++;                                           /* bite animation clock */
}

void ents_update(void) SIM_BANKED
{
    SST Ent *e;
    uint8_t n = 0;
    PROF(5);
    for (e = w->e; e != w->e + MAX_ENTS; e++)
        if (e->kind) {
            n++;
            /* the common case, a walker strolling along between cell boundaries, is done in place
               (nothing but its x and its step clock can change) */
            if ((e->kind == E_GLOOP || e->kind == E_DOME) && e->state == ES_LIVE && e->ground
                && e->vx <= 0x100 && e->vx >= -0x100) {
                int16_t sum = (int16_t)((int16_t)e->xs + e->vx);
                uint16_t nx = (uint16_t)(e->x + (sum >> 8));
                uint8_t xl = (uint8_t)((uint8_t)nx & 15);
                int16_t sx = (int16_t)(nx - w->cam_x);
                if (sx >= -48 && sx < VIEW_W && xl != 13 && xl != 4 && xl != (e->vx > 0 ? 2 : 14)) {
                    e->x = nx;
                    e->xs = (uint8_t)(sum & 0xFF);
                    e->t++;
                    continue;
                }
            }
            if (e->kind == E_CHOMP && e->state == ES_LIVE
                && (uint16_t)((int16_t)(e->x - w->cam_x) + 48) <= 48 + 272) {
                chomp_step(e);
                continue;
            }
            ENT_PROF_A();
            ent_copy(&E, e);
            cur = e;
            update_ent();
            ent_copy(e, &E);
            ENT_PROF_B(e - w->e);
        }
    w->n_ents = n;
    PROF(6);
    ent_bumps();
    if (w->pstate == PS_PLAY) peter_vs_ents();
    PROF(7);
    update_item();
    update_shots();
    update_fx();
}
