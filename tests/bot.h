/* bot.h - the host search bot (see bot.c) */
#ifndef SPP_BOT_H
#define SPP_BOT_H
#include "sim.h"

#define BOT_MAX_SECTORS 64

typedef struct {
    World best;          /* the furthest state it reached */
    uint32_t nodes, backtracks;
    uint8_t gave_up;
    uint16_t n_sectors;
    World sector[BOT_MAX_SECTORS];   /* the state right after each beacon on the winning path */
    uint8_t *path;                   /* if set: the winning path's macros (one per chunk) */
    uint32_t path_n, path_max;
} BotResult;

/* Plays from *w until `sectors` more beacons are touched. Returns 1 on success (and *w is the
   state right after the last beacon), 0 if every choice dies or the node budget runs out. */
int bot_play(World *w, uint16_t sectors, uint32_t max_nodes, BotResult *res);

/* Replays a path from sim_init(seed), writing the buttons of every sim_step (grow/shrink
   pauses included) to keys; returns the number of steps. W is the final state. */
uint32_t bot_replay(uint16_t seed, const uint8_t *path, uint32_t n, uint8_t *keys, uint32_t max);

#endif
