/* tiles.h - SPEEDY PURPLE PETER level cell IDs (one 16x16 metatile each).
 * Shared by the simulation (src/core), the asset generator (tools/gen_assets.py reads this
 * enum by name) and the Game Boy renderer. Order matters: it is the index into mt_tiles[]. */
#ifndef SPP_TILES_H
#define SPP_TILES_H

enum {
    T_SKY = 0,        /* empty space (colour 0 background) */
    T_STARS_A,        /* empty space with a faint star or two (decor, not solid) */
    T_STARS_B,        /* another star pattern */
    T_GROUND_TOP,     /* moon-rock surface (top row of the ground) */
    T_GROUND,         /* moon rock below the surface */
    T_BRICK,          /* breakable space-rock brick (empty) */
    T_BRICK_COIN,     /* looks like a brick: gives up to 10 star bits */
    T_BRICK_POWER,    /* looks like a brick: gives a power-up */
    T_BRICK_NOVA,     /* looks like a brick: gives a supernova */
    T_Q_COIN,         /* ? capsule: one star bit */
    T_Q_POWER,        /* ? capsule: a power-up (power cell when small, blaster when big) */
    T_USED,           /* emptied capsule (solid) */
    T_SOLID,          /* hull block: unbreakable (stairs) */
    T_TUBE_TL,        /* tube mouth, left half  (2 metatiles wide) */
    T_TUBE_TR,        /* tube mouth, right half */
    T_TUBE_L,         /* tube body, left half */
    T_TUBE_R,         /* tube body, right half */
    T_COIN,           /* a floating star bit (collected on touch) */
    T_HIDDEN_1UP,     /* invisible until bumped from below: a 1UP */
    T_POLE,           /* beacon antenna pole (not solid; touching it scores) */
    T_POLE_TOP,       /* the beacon light on top of the pole */
    T_CANNON_TOP,     /* comet launcher muzzle (solid) */
    T_CANNON,         /* comet launcher base (solid) */
    T_HILL_L,         /* background moon hill (decor): left slope */
    T_HILL_R,         /*   right slope */
    T_HILL_TOP,       /*   rounded top (1 wide) */
    T_HILL_FILL,      /*   inside */
    T_HILL_CRATER,    /*   inside, with a crater */
    T_CLOUD_L,        /* nebula cloud (decor, rows 1-4): left end */
    T_CLOUD_M,        /*   middle */
    T_CLOUD_R,        /*   right end */
    T_BUSH_L,         /* crystal bush on the ground (decor): left end */
    T_BUSH_M,         /*   middle */
    T_BUSH_R,         /*   right end */
    T_PLANET_TL,      /* a big ringed planet in the sky (decor, 2x2) */
    T_PLANET_TR,
    T_PLANET_BL,
    T_PLANET_BR,
    T_COUNT
};

#endif
