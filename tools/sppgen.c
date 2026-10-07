/* sppgen.c - SPEEDY PURPLE PETER on the command line (host build of src/core).
 *
 *   sppgen show SEED SECTOR        ASCII map of one sector
 *   sppgen bot SEED SECTORS [god]  let the search bot play; prints per-sector times
 *   sppgen stats SEEDS SECTORS     sector lengths and segment mix
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "../tests/bot.h"


static char cell_char(uint8_t t)
{
    switch (t) {
    case T_SKY: case T_STARS_A: case T_STARS_B: return ' ';
    case T_GROUND_TOP: return '=';
    case T_GROUND: return '#';
    case T_BRICK: return 'B';
    case T_BRICK_COIN: return 'c';
    case T_BRICK_POWER: return 'p';
    case T_BRICK_NOVA: return 'n';
    case T_Q_COIN: return '?';
    case T_Q_POWER: return 'P';
    case T_USED: return 'u';
    case T_SOLID: return 'H';
    case T_TUBE_TL: case T_TUBE_TR: return 'T';
    case T_TUBE_L: case T_TUBE_R: return '|';
    case T_COIN: return '*';
    case T_HIDDEN_1UP: return '1';
    case T_POLE: return 'I';
    case T_POLE_TOP: return 'o';
    case T_CANNON_TOP: return 'C';
    case T_CANNON: return 'c';
    default: return '.';
    }
}

static const char spawn_char[SP_COUNT] = { ' ', 'g', 'd', 'r', 'j', 'm', 'k' };

static void show(uint16_t seed, uint16_t sector)
{
    Gen g;
    static uint8_t cols[1024][LV_ROWS], sp[1024];
    int n = 0, r, c;
    gen_begin(&g, seed, sector);
    for (;;) {
        uint8_t f = gen_column(&g, cols[n], &sp[n]);
        if (n && (f & GEN_SECTOR_START)) break;
        if (++n >= 1024) break;
    }
    printf("seed %04X sector %u: %d columns\n", seed, sector, n);
    for (c = 0; c < n; c += 100) {
        int e = c + 100 < n ? c + 100 : n, k;
        for (r = 0; r < LV_ROWS; r++) {
            for (k = c; k < e; k++) {
                char ch = cell_char(cols[k][r]);
                if (sp[k] && (sp[k] & 15) == r && ch == ' ') ch = spawn_char[sp[k] >> 4];
                putchar(ch);
            }
            putchar('\n');
        }
        putchar('\n');
    }
}

static uint8_t mode = MODE_CLASSIC;   /* -m: the play mode */

static int bot(uint16_t seed, int sectors, int god)
{
    static World w;
    static BotResult res;
    int s;
    uint32_t f0 = 0;
    sim_init(seed, mode);
    if (god) { W.god = 1; memset(W.e, 0, sizeof(W.e)); }
    w = W;
    if (!bot_play(&w, (uint16_t)sectors, 2000000, &res)) {
        printf("STUCK at column %u (sector %u starts at %u, +%u) after %u sectors, %u nodes, lives %u time %u\n",
               (unsigned)(res.best.px >> 4), res.best.sector, res.best.sec_start,
               (unsigned)(((res.best.px >> 4) - res.best.sec_start) & 0xFFF), res.best.sectors_done, res.nodes,
               res.best.lives, res.best.time);
        return 1;
    }
    for (s = 0; s < res.n_sectors; s++) {
        const World *x = &res.sector[s];
        printf("sector %2d: %5.1f s  score %7lu  dist %5u  power %u  lives %u  coins %2u\n", s,
               (x->frames - f0) / 60.0, (unsigned long)x->score, x->dist, x->power, x->lives, x->coins);
        f0 = x->frames;
    }
    printf("OK %d sectors in %.1f s, %u nodes, %u backtracks\n", sectors, w.frames / 60.0, res.nodes, res.backtracks);
    return 0;
}

/* the runner from the start of one sector, printing every few frames */
static int trace(uint16_t seed, uint16_t sector, const char *moves)
{
    int i, n = (int)strlen(moves);
    World *wp = &W;
#define w (*wp)
    sim_init(seed, mode);
    w.god = 1;
    w.sector = sector;
    w.sec_start = 0;
    sim_respawn();
    for (i = 0; i < n * 6; i++) {
        char m = moves[i / 6];
        uint8_t k = (uint8_t)((m == 'R' ? K_RIGHT | K_B : m == 'J' ? K_RIGHT | K_B | K_A : m == 'r' ? K_RIGHT
                    : m == 'j' ? K_RIGHT | K_A : m == 'A' ? K_A : m == 'L' ? K_LEFT : 0));
        sim_step(k);
        printf("%4d %c col %3u+%2u py %4d vx %5d vy %6d g%u st%u\n", i, m, (unsigned)(w.px >> 4), w.px & 15, w.py, w.pvx, w.pvy, w.ground, w.pstate);
        if (w.pstate == PS_DEAD) break;
    }
#undef w
    return 0;
}

/* the bot's winning input for SECTORS sectors: one hex byte per frame to FILE, and the final
   state (for the ROM tests) to stdout */
static int path(uint16_t seed, int sectors, const char *file)
{
    static World w;
    static BotResult res;
    static uint8_t mac[200000], keys[2000000];
    uint32_t n, i;
    FILE *f;
    sim_init(seed, mode);
    w = W;
    res.path = mac;
    res.path_max = sizeof(mac);
    if (!bot_play(&w, (uint16_t)sectors, 4000000, &res)) { fprintf(stderr, "bot failed\n"); return 1; }
    n = bot_replay(seed, mode, mac, res.path_n, keys, sizeof(keys));
    f = fopen(file, "w");
    if (!f) return 1;
    for (i = 0; i < n; i++) fprintf(f, "%02x\n", keys[i]);
    fclose(f);
    printf("steps %lu px %u py %d score %lu sectors %u frames %lu coins %u lives %u power %u dist %u time %u\n",
           (unsigned long)n, W.px, W.py, (unsigned long)W.score, W.sectors_done, (unsigned long)W.frames,
           W.coins, W.lives, W.power, W.dist, W.time);
    return 0;
}

/* replay a key file (one hex byte per frame), printing the state after every step */
static int replay(uint16_t seed, const char *file)
{
    FILE *f = fopen(file, "r");
    unsigned k;
    unsigned long n = 0;
    if (!f) return 1;
    sim_init(seed, mode);
    while (fscanf(f, "%x", &k) == 1) {
        sim_step((uint8_t)k);
        n++;
        printf("%lu %u %d %d %d %lu %u %u\n", n, W.px, W.py, W.pvx, W.pvy, (unsigned long)W.score, W.time, W.bonus);
    }
    fclose(f);
    return 0;
}

/* the first N columns of a run, one line each (cells in hex, capped at f) */
static int cols(uint16_t seed, int n)
{
    Gen g;
    uint8_t c[LV_ROWS], sp;
    int i, r;
    gen_begin(&g, seed, 0);
    for (i = 0; i < n; i++) {
        gen_column(&g, c, &sp);
        printf("%d ", i);
        for (r = 0; r < LV_ROWS; r++) printf("%x", c[r] > 15 ? 15 : c[r]);
        printf(" %02x\n", sp);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "-m")) {            /* -m MODE: 0 classic, 1 auto sprint, 2 auto run */
        mode = (uint8_t)atoi(argv[2]);
        argc -= 2;
        argv += 2;
    }
    if (argc >= 4 && !strcmp(argv[1], "cols")) return cols((uint16_t)strtoul(argv[2], 0, 0), atoi(argv[3]));
    if (argc >= 4 && !strcmp(argv[1], "replay")) return replay((uint16_t)strtoul(argv[2], 0, 0), argv[3]);
    if (argc >= 5 && !strcmp(argv[1], "path")) return path((uint16_t)strtoul(argv[2], 0, 0), atoi(argv[3]), argv[4]);
    if (argc >= 5 && !strcmp(argv[1], "trace")) return trace((uint16_t)strtoul(argv[2], 0, 0), (uint16_t)atoi(argv[3]), argv[4]);
    if (argc >= 4 && !strcmp(argv[1], "show")) { show((uint16_t)strtoul(argv[2], 0, 0), (uint16_t)atoi(argv[3])); return 0; }
    if (argc >= 4 && !strcmp(argv[1], "bot")) return bot((uint16_t)strtoul(argv[2], 0, 0), atoi(argv[3]), argc > 4);
    fprintf(stderr, "usage: sppgen [-m MODE] show SEED SECTOR | bot SEED SECTORS [god] | path SEED SECTORS FILE"
                    " | replay SEED FILE | trace SEED SECTOR MOVES | cols SEED N\n");
    return 2;
}
