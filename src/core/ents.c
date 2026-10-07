/* ents.c - SPEEDY PURPLE PETER enemies, power-ups, shots and effects (portable C). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "sim_int.h"

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

/* ------------------------------------------------------------------ the level */

static uint8_t ent_physics(Ent *e, int16_t grav, int16_t maxfall)
{
    uint8_t wall = 0;
    {
        int16_t sum = (int16_t)((int16_t)e->xs + e->vx);
        e->x = (uint16_t)(e->x + (sum >> 8));
        e->xs = (uint8_t)(sum & 0xFF);
    }
    if (e->vx > 0 && solid_px((uint16_t)(e->x + 14), (int16_t)(e->y + 8))) {
        e->x = (uint16_t)(((e->x + 14) & ~15) - 15);
        wall = 1;
    } else if (e->vx < 0 && solid_px((uint16_t)(e->x + 1), (int16_t)(e->y + 8))) {
        e->x = (uint16_t)(((e->x + 1) & ~15) + 15);
        wall = 1;
    }
    if (!e->ground) {
        e->vy = (int16_t)(e->vy + grav);
        if (e->vy > maxfall) e->vy = maxfall;
    }
    {
        int16_t oldfeet = (int16_t)(e->y + 16), sum = (int16_t)((int16_t)e->ys + e->vy), fy, top;
        e->y = (int16_t)(e->y + (sum >> 8));
        e->ys = (uint8_t)(sum & 0xFF);
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
    }
    return wall;
}

static void ent_kill_by(Ent *e, uint8_t step, int8_t dir)
{
    award(step, e->x, e->y);
    ent_flip(e, dir);
    w->sfx |= EV_KICK;
}

static void update_ent(Ent *e)
{
    int16_t sx = (int16_t)(e->x - w->cam_x);
    if (sx < -48 || sx > 272 || e->y > LV_ROWS * 16 + 32) { e->kind = E_NONE; return; }

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

    switch (e->kind) {
    case E_GLOOP: case E_DOME: case E_DOME_RED:
        if (ent_physics(e, 0x60, 0x400)) e->vx = (int16_t)-e->vx;
        if (e->kind == E_DOME_RED && e->ground) {          /* turn at ledges */
            uint16_t fx = (uint16_t)(e->vx > 0 ? e->x + 13 : e->x + 2);
            if (!solid_px(fx, (int16_t)(e->y + 17))) e->vx = (int16_t)-e->vx;
        }
        e->t++;
        break;
    case E_JET:
        if (ent_physics(e, 0x30, 0x400)) e->vx = (int16_t)-e->vx;
        if (e->ground) { e->vy = -0x380; e->ground = 0; }
        e->t++;
        break;
    case E_SHELL: case E_SHELL_RED:
        if (e->t2) e->t2--;
        if (ent_physics(e, 0x60, 0x400) && e->vx) {
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
            Ent *o;
            for (o = w->e; o != w->e + MAX_ENTS; o++) {
                if (o == e || !is_enemy(o->kind) || o->kind == E_CHOMP || o->state != ES_LIVE) continue;
                if (overlap((int16_t)e->x, e->y, (int16_t)(e->x + 15), (int16_t)(e->y + 15),
                            (int16_t)o->x, o->y, (int16_t)(o->x + 15), (int16_t)(o->y + 15))) {
                    ent_kill_by(o, (uint8_t)(SC_500 + e->chain), e->vx > 0 ? 1 : -1);
                    if (e->chain < SC_1UP - SC_500) e->chain++;
                }
            }
        }
        break;
    case E_CHOMP: {
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
        break;
    }
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

/* walkers turn around when they bump into each other */

static void ent_bumps(void)
{
    Ent *a, *b;
    for (a = w->e; a != w->e + MAX_ENTS - 1; a++) {
        if (!is_walker(a->kind) || a->state != ES_LIVE) continue;
        for (b = a + 1; b != w->e + MAX_ENTS; b++) {
            int16_t dx, dy;
            if (!b->kind) continue;
            dx = (int16_t)(b->x - a->x);
            if (dx > 14 || dx < -14) continue;
            if (!(is_walker(b->kind) || ((b->kind == E_SHELL || b->kind == E_SHELL_RED) && !b->vx))) continue;
            if (b->state != ES_LIVE) continue;
            dy = (int16_t)(b->y - a->y);
            if (dy > 14 || dy < -14) continue;
            if (dx >= 0) { if (a->vx > 0) a->vx = (int16_t)-a->vx; if (b->vx < 0) b->vx = (int16_t)-b->vx; }
            else { if (a->vx < 0) a->vx = (int16_t)-a->vx; if (b->vx > 0) b->vx = (int16_t)-b->vx; }
        }
    }
}

static void peter_vs_ents(void)
{
    uint8_t h = sim_peter_h();
    Ent *e;
    int16_t px0 = (int16_t)(w->px - w->cam_x) + 3, px1 = px0 + 9;
    int16_t py0 = (int16_t)(w->py + (w->power ? 32 : 16) - h + 4), py1 = (int16_t)(w->py + (w->power ? 32 : 16) - 1);
    int16_t feet = (int16_t)(py1 + 1);
    for (e = w->e; e != w->e + MAX_ENTS; e++) {
        int16_t ex, top;
        if (!e->kind || e->kind == E_CANNON || e->state != ES_LIVE) continue;
        ex = (int16_t)(e->x - w->px);
        if (ex > 20 || ex < -20) continue;                  /* far apart: the cheap test */
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
        if (e->kind == E_NOVA) {
            if (ent_physics(e, 0x30, 0x400)) e->vx = (int16_t)-e->vx;
            if (e->ground) { e->vy = -0x480; e->ground = 0; }
        } else if (ent_physics(e, 0x60, 0x400)) {
            e->vx = (int16_t)-e->vx;
        }
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
    Fx *f;
    static const int8_t bump_dy[9] = { -2, -4, -5, -6, -5, -4, -2, -1, 0 };
    for (f = w->fx; f != w->fx + MAX_FX; f++) {
        if (!f->kind) continue;
        switch (f->kind) {
        case FX_COIN:
            f->y = (int16_t)(f->y + f->vy);
            f->vy++;
            if (++f->t >= 12) {
                uint16_t x = f->x;
                int16_t y = f->y;
                f->kind = FX_NONE;
                award(SC_200, x, y);
            }
            break;
        case FX_SCORE:
            if (f->t > 10) f->y--;
            if (!--f->t) f->kind = FX_NONE;
            break;
        case FX_SHARD:
            f->x = (uint16_t)(f->x + f->vx);
            f->y = (int16_t)(f->y + f->vy);
            if (++f->t & 1) f->vy++;
            if (f->y > LV_ROWS * 16 + 16) f->kind = FX_NONE;
            break;
        case FX_PUFF:
            if (!--f->t) f->kind = FX_NONE;
            break;
        case FX_BUMP:
            f->vy = bump_dy[f->t];
            if (++f->t >= 9) {
                uint16_t col = (uint16_t)(f->x >> 4);
                f->kind = FX_NONE;
                mark(col, f->row);
            }
            break;
        }
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

void ents_update(void) SIM_BANKED
{
    Ent *e;
    PROF(5);
    for (e = w->e; e != w->e + MAX_ENTS; e++)
        if (e->kind) update_ent(e);
    PROF(6);
    ent_bumps();
    if (w->pstate == PS_PLAY) peter_vs_ents();
    PROF(7);
    update_item();
    update_shots();
    update_fx();
}
