/* bot.c - a search bot for SPEEDY PURPLE PETER (host only).
 *
 * It plays the real simulation with button presses only. Every CHUNK frames it picks one of a
 * few button macros (run right, run-jump, walk-jump, wait, back up, ...). The candidates are
 * ranked by a rollout: from each one a simple runner policy (hold right + B, jump at walls, pits
 * and enemies) plays on for a couple of seconds, and the macro whose rollout gets furthest is
 * tried first. If every choice from a state dies, the bot backtracks through saved states.
 * Used by the tests to prove that every generated sector can be played through, and by
 * `sppgen bot` for the balance report. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bot.h"

#define CHUNK    4
#define ROLLOUT  120

static const uint8_t macros[] = {
    K_RIGHT | K_B,
    K_RIGHT | K_B | K_A,
    K_RIGHT | K_A,
    K_RIGHT,
    0,
    K_A,
    K_LEFT,
    K_LEFT | K_A,
    K_LEFT | K_B,
    K_LEFT | K_B | K_A,
    K_RIGHT | K_B | K_DOWN,
};
#define NMAC ((int)(sizeof(macros) / sizeof(macros[0])))

typedef struct {
    World w;
    uint8_t order[NMAC];
    uint8_t n, next;     /* candidates, next to try */
    uint8_t m;           /* the macro that led here */
} Node;

#define HBITS 20
static uint32_t *seen;

static uint32_t key(const World *w)
{
    uint32_t h = 2166136261u;
    uint32_t v[7];
    int i;
    v[0] = w->px >> 1; v[1] = (uint32_t)(w->py + 64) >> 1; v[2] = (uint32_t)(w->pvx + 0x8000) >> 9;
    v[3] = (uint32_t)(w->pvy + 0x8000) >> 10; v[4] = w->power | (w->ground << 2) | ((w->keys & K_A) << 3);
    v[5] = w->cam_x >> 2; v[6] = w->sectors_done;
    for (i = 0; i < 7; i++) { h ^= v[i]; h *= 16777619u; }
    return h | 1;
}

static int seen_add(uint32_t k)
{
    uint32_t i = k & ((1u << HBITS) - 1);
    while (seen[i]) {
        if (seen[i] == k) return 0;
        i = (i + 1) & ((1u << HBITS) - 1);
    }
    seen[i] = k;
    return 1;
}

static int dead(const World *w)
{
    return w->pstate == PS_DEAD || w->pstate == PS_OVER || w->over;
}

/* one frame of the global world W (grow/shrink pauses are skipped) */
static void step(uint8_t keys)
{
    sim_step(keys);
    while (W.pstate == PS_GROW || W.pstate == PS_SHRINK) sim_step(keys);
}

/* run one chunk on *w (through W); 0 if Peter died */
static int chunk(World *w, uint8_t keys)
{
    int i, ok = 1;
    W = *w;
    for (i = 0; i < CHUNK; i++) {
        step(keys);
        if (dead(&W)) { ok = 0; break; }
    }
    *w = W;
    return ok;
}

static uint32_t progress(const World *w)
{
    return (uint32_t)w->sectors_done * 1000000u + (uint32_t)w->dist * 16u + (uint16_t)(w->px - w->cam_x);
}

static int solid_at(const World *w, uint16_t x, int16_t y)
{
    uint8_t t;
    if (y < 0) return 0;
    (void)w;                        /* w is &W: sim_cell reads the global world */
    t = sim_cell((uint16_t)(x >> 4), (uint8_t)(y >> 4));
    return (t >= T_GROUND_TOP && t <= T_TUBE_R) || t == T_CANNON_TOP || t == T_CANNON;
}

/* the runner policy: right + B, jump at walls, pits and enemies ahead */
static uint8_t policy(const World *w, int *hold)
{
    int h = w->power && !w->duck ? 32 : 16;
    int16_t feet = (int16_t)(w->py + (w->power ? 32 : 16));
    int want = 0, i;
    if (w->ground) {
        int look = 8 + (w->pvx > 0 ? (w->pvx >> 12) * 18 : 0), d;
        for (d = 4; d <= look; d += 4) {
            uint16_t ax = (uint16_t)(w->px + 13 + d);
            if (solid_at(w, ax, (int16_t)(feet - 4)) || solid_at(w, ax, (int16_t)(feet - h + 4))) want = 1;
        }
        if (!solid_at(w, (uint16_t)(w->px + 12 + 20), feet) && !solid_at(w, (uint16_t)(w->px + 12 + 20), (int16_t)(feet + 16))) want = 1;
        for (i = 0; i < MAX_ENTS; i++) {
            const Ent *e = &w->e[i];
            int16_t dx = (int16_t)(e->x - w->px);
            if (!e->kind || e->kind == E_CANNON || e->state != ES_LIVE) continue;
            if (dx > 0 && dx < 48 && e->y > feet - 48 && e->y < feet + 8) want = 1;
        }
        if (want && !(w->keys & K_A)) { *hold = 28; return K_RIGHT | K_B | K_A; }
        return K_RIGHT | K_B;
    }
    if (*hold > 0) { (*hold)--; return K_RIGHT | K_B | K_A; }
    return K_RIGHT | K_B;
}

static uint32_t rollout(const World *w0)
{
    int i, hold = (w0->keys & K_A) ? 20 : 0;
    uint32_t best = 0;
    W = *w0;
    for (i = 0; i < ROLLOUT; i++) {
        step(policy(&W, &hold));
        if (dead(&W)) return best / 2;
        if (progress(&W) > best) best = progress(&W);
    }
    return best + 4096;             /* alive at the end */
}

static void expand(Node *n)
{
    uint32_t score[NMAC];
    int i, j;
    n->n = 0;
    n->next = 0;
    for (i = 0; i < NMAC; i++) {
        World c = n->w;
        if (!chunk(&c, macros[i])) continue;
        score[n->n] = rollout(&c);
        n->order[n->n++] = (uint8_t)i;
    }
    for (i = 1; i < n->n; i++)         /* insertion sort, best first (stable: macro order) */
        for (j = i; j > 0 && score[j] > score[j - 1]; j--) {
            uint32_t s = score[j]; uint8_t o = n->order[j];
            score[j] = score[j - 1]; n->order[j] = n->order[j - 1];
            score[j - 1] = s; n->order[j - 1] = o;
        }
}

int bot_play(World *w, uint16_t sectors, uint32_t max_nodes, BotResult *res)
{
    Node *st;
    int sp = 0, maxsp = 8192;
    uint32_t nodes = 0;
    uint16_t goal = (uint16_t)(w->sectors_done + sectors);
    int ok = 0;
    if (!seen) seen = calloc((size_t)1 << HBITS, sizeof(uint32_t));
    memset(seen, 0, sizeof(uint32_t) << HBITS);
    st = malloc(sizeof(Node) * (size_t)maxsp);
    {
        uint8_t *p = res->path;
        uint32_t pm = res->path_max;
        memset(res, 0, sizeof(*res));
        res->path = p;
        res->path_max = pm;
    }
    res->best = *w;
    st[0].w = *w;
    expand(&st[0]);
    while (sp >= 0) {
        Node *n = &st[sp];
        World c;
        if (n->w.sectors_done >= goal && !n->w.bonus) {
            int k;
            *w = n->w;
            ok = 1;
            for (k = 1; k <= sp; k++) {         /* the path: when was each beacon touched */
                if (st[k].w.sectors_done != st[k - 1].w.sectors_done && res->n_sectors < BOT_MAX_SECTORS)
                    res->sector[res->n_sectors++] = st[k].w;
                if (res->path && res->path_n < res->path_max) res->path[res->path_n++] = macros[st[k].m];
            }
            break;
        }
        if (n->next >= n->n) { sp--; res->backtracks++; continue; }
        if (++nodes > max_nodes) {
            res->gave_up = 1;
            if (getenv("BOT_DEBUG")) {
                int k;
                for (k = sp > 300 ? sp - 300 : 0; k <= sp; k++)
                    printf("path %d: col %u+%u py %d vx %d vy %d g%u t%u keys %02x\n", k, st[k].w.px >> 4, st[k].w.px & 15,
                           st[k].w.py, st[k].w.pvx, st[k].w.pvy, st[k].w.ground, st[k].w.time, st[k].w.keys);
            }
            break;
        }
        c = n->w;
        chunk(&c, macros[n->order[n->next++]]);
        if (dead(&c) || !seen_add(key(&c))) continue;
        if (sp + 1 >= maxsp) {          /* very long: forget the oldest half */
            memmove(&st[0], &st[maxsp / 2], sizeof(Node) * (size_t)(maxsp / 2));
            sp -= maxsp / 2;
        }
        sp++;
        st[sp].w = c;
        st[sp].m = n->order[n->next - 1];
        expand(&st[sp]);
        if (progress(&c) > progress(&res->best)) res->best = c;
    }
    res->nodes = nodes;
    free(st);
    return ok;
}

uint32_t bot_replay(uint16_t seed, uint8_t mode, const uint8_t *path, uint32_t n, uint8_t *keys, uint32_t max)
{
    uint32_t i, k = 0;
    int j;
    sim_init(seed, mode);
    for (i = 0; i < n; i++)
        for (j = 0; j < CHUNK; j++) {
            if (k < max) keys[k] = path[i];
            k++;
            sim_step(path[i]);
            while (W.pstate == PS_GROW || W.pstate == PS_SHRINK) {
                if (k < max) keys[k] = path[i];
                k++;
                sim_step(path[i]);
            }
        }
    return k;
}
