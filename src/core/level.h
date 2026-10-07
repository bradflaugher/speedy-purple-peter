/* level.h - SPEEDY PURPLE PETER level generator.
 *
 * The level is one endless run, generated column by column, left to right, in "sectors" of
 * roughly 150-250 columns. Each sector ends with a beacon (the checkpoint). A sector is a pure
 * function of (seed, sector), so dying sends you back to the start of the sector and it is
 * rebuilt identically. Portable C: builds with SDCC (Game Boy) and gcc (host tests, tools). */
#ifndef SPP_LEVEL_H
#define SPP_LEVEL_H

#include <stdint.h>
#include "tiles.h"

#if defined(__SDCC) && defined(__PORT_sm83)
#define GEN_BANKED __banked
#else
#define GEN_BANKED
#endif

#define LV_ROWS     13          /* metatile rows 0..12 */
#define GROUND_ROW  11          /* the ground's top row (rows 11 and 12 are ground) */

/* spawn byte: kind << 4 | row (the row of the enemy's lowest cell); 0 = nothing */
enum {
    SP_NONE = 0,
    SP_GLOOP,      /* walks, falls off ledges */
    SP_DOME,       /* green Dome-bot: walks off ledges */
    SP_DOME_RED,   /* red Dome-bot: turns at ledges */
    SP_JET,        /* flying (hopping) Dome-bot */
    SP_CHOMP,      /* Moon Chomper in the tube whose left half is this column; row = tube mouth */
    SP_CANNON,     /* comet launcher controller; row = launcher muzzle */
    SP_COUNT
};
#define SPAWN(k, r) ((uint8_t)(((k) << 4) | (r)))

/* generator segments */
enum {
    SEG_START, SEG_FLAT, SEG_PIT, SEG_QROW, SEG_HIGH, SEG_TUBE, SEG_STAIRS, SEG_BRIDGE,
    SEG_COINS, SEG_CANNON, SEG_END, SEG_COUNT
};

typedef struct {
    uint16_t seed;
    uint16_t sector;        /* current sector (0-based) */
    uint16_t rng;
    uint8_t seg, pos, len;  /* current segment, column within it, its length */
    uint8_t a, b, c, d;     /* segment parameters */
    uint8_t segs_left;      /* body segments still to come in this sector */
    uint8_t col;            /* column number within the sector (wraps; decor only) */
    uint8_t diff;           /* difficulty 0..15 */
    uint8_t power_left;     /* power-ups still owed to this sector */
    uint8_t gap_next;       /* 1: put a short flat stretch before the next segment */
    uint8_t run_before;     /* length of the flat stretch just before this segment */
} Gen;

/* returned by gen_column */
#define GEN_SECTOR_START 0x01   /* this column is the first column of a new sector */
#define GEN_BEACON       0x02   /* this column holds the beacon pole */

void gen_begin(Gen *g, uint16_t seed, uint16_t sector) GEN_BANKED;
/* Writes the next column's 13 cells (top to bottom) and its spawn byte. */
uint8_t gen_column(Gen *g, uint8_t *cells, uint8_t *spawn) GEN_BANKED;

/* longest pit (in columns) the generator makes at difficulty d */
uint8_t gen_max_pit(uint8_t d) GEN_BANKED;

#endif
