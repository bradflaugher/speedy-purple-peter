/* render.c - SPEEDY PURPLE PETER: the level's BG streaming, sprites, HUD and sound events.
 * Banked. Reads the world (W) and never changes it. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "front.h"
#include "assets.h"
#include "sound.h"

#define COLMASK 0x0FFF
#define SST static                  /* hot locals in static RAM: faster than the stack */
extern uint8_t dbg_ly[8];          /* LY stamps of this frame (main.c); [0] = its start */
#define VIEW_COLS 14                /* metatile columns drawn from the camera's column */

static uint16_t drawn_col;
uint16_t dbg_built, dbg_dirty_drawn;          /* the sim's gen_col last frame (spread the work out) */          /* next column to draw (the BG ring holds 16) */
static uint8_t tbuf[LV_ROWS * 4], abuf[LV_ROWS * 4];
static uint8_t oam_n;
static uint8_t hud_coins = 0xFF, hud_lives = 0xFF, hud_sector_v = 0xFF;
static uint16_t hud_time = 0xFFFF;
static uint8_t hud_score_rev;      /* W.score_rev when the score was last shown */
static uint8_t anim_f, anim_t;

/* ------------------------------------------------------------------ text */
void fmt_u32(char *out, uint32_t v, uint8_t digits) BANKED
{
    out[digits] = 0;
    while (digits) {
        out[--digits] = (char)('0' + (uint8_t)(v % 10));
        v /= 10;
    }
}

void fmt_time(char *out, uint32_t frames) BANKED
{
    /* frames at 59.73 Hz -> minutes, seconds, hundredths (close enough: 60 Hz) */
    uint32_t cs = frames * 5 / 3;            /* centiseconds */
    uint16_t s = (uint16_t)(cs / 100);
    uint8_t m = (uint8_t)(s / 60 > 99 ? 99 : s / 60);
    char t[3];
    fmt_u32(t, m, 2); out[0] = t[0]; out[1] = t[1]; out[2] = ':';
    fmt_u32(t, s % 60, 2); out[3] = t[0]; out[4] = t[1]; out[5] = '.';
    fmt_u32(t, (uint8_t)(cs % 100), 2); out[6] = t[0]; out[7] = t[1];
    out[8] = 0;
}

/* decimal digits by subtraction (division is slow on the Game Boy) */
static const uint16_t pow10[5] = { 10000, 1000, 100, 10, 1 };

static void digits16(uint8_t *d, uint16_t v)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        uint8_t n = 0;
        uint16_t p = pow10[i];
        while (v >= p) { v -= p; n++; }
        d[i] = n;
    }
}

static void fmt_u16(char *out, uint16_t v, uint8_t digits)
{
    uint8_t d[5], i;
    digits16(d, v);
    for (i = 0; i < digits; i++) out[i] = (char)('0' + d[5 - digits + i]);
    out[digits] = 0;
}

/* ------------------------------------------------------------------ the BG ring */
/* what a cell looks like: look-alikes merged, hidden blocks are sky (see sim_visual) */
static const uint8_t vis_map[T_COUNT] = {
    T_SKY, T_STARS_A, T_STARS_B, T_GROUND_TOP, T_GROUND, T_BRICK, T_BRICK, T_BRICK, T_BRICK,
    T_Q_COIN, T_Q_COIN, T_USED, T_SOLID, T_TUBE_TL, T_TUBE_TR, T_TUBE_L, T_TUBE_R, T_COIN,
    T_SKY, T_POLE, T_POLE_TOP, T_CANNON_TOP, T_CANNON, T_HILL_L, T_HILL_R, T_HILL_TOP,
    T_HILL_FILL, T_HILL_CRATER, T_CLOUD_L, T_CLOUD_M, T_CLOUD_R, T_BUSH_L, T_BUSH_M, T_BUSH_R,
    T_PLANET_TL, T_PLANET_TR, T_PLANET_BL, T_PLANET_BR
};

/* a bouncing block is drawn as a sprite: its cell shows sky */
static uint8_t bumping(uint16_t col, uint8_t row)
{
    uint8_t i;
    for (i = 0; i < MAX_FX; i++)
        if (W.fx[i].kind == FX_BUMP && W.fx[i].row == row && (W.fx[i].x >> 4) == col) return 1;
    return 0;
}

/* 26 rows of 2 tiles to the BG map, straight to VRAM between the LCD's busy periods */
static void vram_strip(uint8_t *dst, const uint8_t *src)
{
    uint8_t n = LV_ROWS * 2;
    do {
        while (STAT_REG & STATF_BUSY) {}
        dst[0] = src[0];
        dst[1] = src[1];
        src += 2;
        dst += 32;
    } while (--n);
}

/* build a column into tp/ap (2 x 26 tiles and attributes) */
/* the cell -> 4 tiles / 4 attributes tables with the look-alikes already merged (vis_map) */
static uint8_t vt[T_COUNT][4], va[T_COUNT][4];
static const uint8_t *bt_lv, *bt_tab;
static uint8_t *bt_dst;
static uint8_t bt_n;

/* 13 cells from bt_lv -> 4 bytes each from bt_tab into bt_dst (the hot loop, in assembly) */
static void build_asm(void) __naked
{
    __asm
        ld  a, #13
        ld  (_bt_n), a
        ld  hl, #_bt_dst
        ld  a, (hl+)
        ld  e, a
        ld  d, (hl)             ; de = dst
        ld  hl, #_bt_lv
        ld  a, (hl+)
        ld  h, (hl)
        ld  l, a                ; hl = lv
    1$:
        ld  a, (hl+)            ; the cell
        push hl
        ld  l, a
        ld  h, #0
        add hl, hl
        add hl, hl              ; * 4
        ld  a, (_bt_tab)
        add a, l
        ld  l, a
        ld  a, (_bt_tab + 1)
        adc a, h
        ld  h, a
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl+)
        ld  (de), a
        inc de
        ld  a, (hl)
        ld  (de), a
        inc de
        pop hl
        ld  a, (_bt_n)
        dec a
        ld  (_bt_n), a
        jr  nz, 1$
        ret
    __endasm;
}

static void build_col(uint16_t col, uint8_t *tp, uint8_t *ap)
{
    const Fx *f;
    bt_lv = W.lv[(uint8_t)col & (LV_COLS - 1)];
    bt_tab = &vt[0][0];
    bt_dst = tp;
    build_asm();
    if (is_cgb) {
        bt_tab = &va[0][0];
        bt_dst = ap;
        build_asm();
    }
    /* a bouncing block is drawn as a sprite: its cell shows sky */
    for (f = W.fx; f != W.fx + MAX_FX; f++)
        if (f->kind == FX_BUMP && (f->x >> 4) == col) {
            uint8_t *p = tp + (f->row << 2);
            p[0] = p[1] = p[2] = p[3] = vt[T_SKY][0];
            if (is_cgb) { p = ap + (f->row << 2); p[0] = p[1] = p[2] = p[3] = va[T_SKY][0]; }
        }
}

/* draw a column at once (a fresh view) */
static void draw_col(uint16_t col)
{
    uint8_t mx = (uint8_t)((col & 15) << 1);
    build_col(col, tbuf, abuf);
    vram_strip((uint8_t *)0x9800 + mx, tbuf);
    if (is_cgb) {
        VBK_REG = 1;
        vram_strip((uint8_t *)0x9800 + mx, abuf);
        VBK_REG = 0;
    }
}

static void draw_cell(uint16_t col, uint8_t row)
{
    uint8_t t = vis_map[W.lv[(uint8_t)col & (LV_COLS - 1)][row]], mx = (uint8_t)((col & 15) << 1);
    if (bumping(col, row)) t = T_SKY;
    set_bkg_tiles(mx, (uint8_t)(row << 1), 2, 2, ram_mt_tiles[t]);
    if (is_cgb) {
        VBK_REG = 1;
        set_bkg_tiles(mx, (uint8_t)(row << 1), 2, 2, ram_mt_attr[t]);
        VBK_REG = 0;
    }
}

void render_reset(void) BANKED
{
    uint8_t i;
    col_pending = 0;
    for (i = 0; i < T_COUNT; i++) {
        memcpy(vt[i], ram_mt_tiles[vis_map[i]], 4);
        memcpy(va[i], ram_mt_attr[vis_map[i]], 4);
    }
    uint16_t c = (uint16_t)(W.cam_x >> 4);
    for (i = 0; i < VIEW_COLS; i++) draw_col((uint16_t)((c + i) & COLMASK));
    drawn_col = (uint16_t)((c + VIEW_COLS) & COLMASK);
    sprites_clear();
}

static void stream(void)
{
    uint16_t c = (uint16_t)(W.cam_x >> 4);
    uint8_t i;
    if (W.redraw) { render_reset(); return; }
    /* changed cells within the ring */
    for (i = 0; i < W.dirty_n; i++) {
        uint16_t col = (uint16_t)((c & 0xF00) | W.dirty_col[i]);
        int16_t d = (int16_t)((col - c) << 4) >> 4;          /* signed 12-bit distance */
        if (d > 128) col -= 256; else if (d < -128) col += 256;
        col &= COLMASK;
        if ((uint16_t)((drawn_col - col - 1) & COLMASK) < 16) { draw_cell(col, W.dirty_row[i]); dbg_dirty_drawn = frame_count; }
    }
    /* the next column: built here, written by the VBlank handler over two VBlanks (the camera
       needs at least 6 frames to cross a column) */
    if (col_pending) return;
    i = (uint8_t)((drawn_col - c) & COLMASK);
    if (i >= VIEW_COLS) return;
    /* not needed for another column: leave it for a quieter frame if this one is busy */
    if (i >= VIEW_COLS - 2 && (uint8_t)(LY_REG - dbg_ly[0] + (LY_REG < dbg_ly[0] ? 154 : 0)) > 60) return;
    {
        dbg_built = frame_count;
        build_col(drawn_col, col_buf[0], col_buf[1]);
        col_dst = (uint8_t *)0x9800 + ((drawn_col & 15) << 1);
        col_pending = 2;
        drawn_col = (uint16_t)((drawn_col + 1) & COLMASK);
    }
}

/* ------------------------------------------------------------------ sprites
 * Written for SDCC: positions are turned into 8-bit OAM coordinates once (at()), then objects
 * are written with a byte pointer. Off-screen halves need no clipping: an OAM x of 0 or >= 168
 * and a y of 0 or >= 160 are invisible, and the 8-bit wrap puts everything off-screen there. */
uint8_t dbg_sprl[4];
uint8_t dbg_spr2[4];
uint8_t dbg_hud_ly[4];
static uint8_t ox, oy;              /* OAM x, y of the current object's top-left */
static uint8_t *op8;                /* next free OAM byte */
static uint8_t *oam_last = (uint8_t *)&shadow_OAM[40];   /* the end of last frame's objects */
#define OAM_END ((uint8_t *)&shadow_OAM[40])
static uint8_t pl_gold, pl_fx, pl_green, pl_red, pl_red1, pl_item, pl_block;

void sprites_clear(void) BANKED
{
    uint8_t i;
    for (i = 0; i < 40; i++) shadow_OAM[i].y = 0;
    oam_n = 0;
    oam_last = (uint8_t *)shadow_OAM;
}

/* 0 if the 16 px wide object at world (x, y) is entirely off-screen */
static uint8_t at(uint16_t x, int16_t y)
{
    SST uint16_t sx, sy;
    sx = (uint16_t)(x - W.cam_x + 8);
    sy = (uint16_t)(y - W.cam_y + 32);
    if ((uint16_t)(sx + 8) >= 184 || (uint16_t)(sy + 16) >= 192) return 0;
    ox = (uint8_t)sx;
    oy = (uint8_t)sy;
    return 1;
}

static void put1(uint8_t tile, uint8_t prop)            /* one 8x16 object at (ox, oy) */
{
    uint8_t *p = op8;
    if (p == OAM_END) return;
    *p++ = oy; *p++ = ox; *p++ = tile; *p++ = prop;
    op8 = p;
}

/* a 16x16 frame at (ox, oy): two objects, the halves swapped when flipped. In assembly (it is
   the inner loop of the sprite drawing); shadow OAM is page-aligned at 0xC000 in GBDK. */
static uint8_t pt, pp;                  /* put2's tile and attributes */
static void put2a(void) __naked
{
    __asm
        ld  hl, #_op8
        ld  a, (hl+)
        ld  h, (hl)
        ld  l, a
        ld  a, l
        cp  #0x99                   ; room for two objects (l <= 0x98)?
        ret nc
        ld  a, (_pt)
        ld  e, a
        add a, #2
        ld  d, a                    ; e = left half, d = right half
        ld  a, (_pp)
        bit 5, a                    ; S_FLIPX
        jr  z, 1$
        ld  a, e
        ld  e, d
        ld  d, a
    1$:
        ld  a, (_oy)
        ld  (hl+), a
        ld  a, (_ox)
        ld  (hl+), a
        ld  a, e
        ld  (hl+), a
        ld  a, (_pp)
        ld  (hl+), a
        ld  a, (_oy)
        ld  (hl+), a
        ld  a, (_ox)
        add a, #8
        ld  (hl+), a
        ld  a, d
        ld  (hl+), a
        ld  a, (_pp)
        ld  (hl+), a
        ld  a, l
        ld  (_op8), a
        ld  a, h
        ld  (_op8 + 1), a
        ret
    __endasm;
}
#define put2(t, p) (pt = (t), pp = (p), put2a())

static void put4(uint8_t tile, uint8_t prop)            /* a 16x32 frame at (ox, oy) */
{
    uint8_t top = tile, bot = (uint8_t)(tile + 4), y = oy;
    if (prop & S_FLIPY) { top = bot; bot = tile; }
    put2(top, prop);
    oy = (uint8_t)(y + 16);
    put2(bot, prop);
    oy = y;
}

static void draw_peter(void)
{
    uint8_t big = W.power != PW_SMALL, tile, prop, flash;
    int16_t y = W.py;
    if (W.pstate == PS_GROW || W.pstate == PS_SHRINK) {
        /* the classic flicker between the two sizes */
        uint8_t show_big = (uint8_t)((W.state_t >> 2) & 1);
        if (W.pstate == PS_GROW && W.power == PW_BLASTER) show_big = 1;
        if (W.pstate == PS_SHRINK) { big = show_big; if (big) y -= 16; }
        else { big = show_big; if (!big) y += 16; }
    }
    if (W.hurt_t && (frame_count & 2) && W.pstate == PS_PLAY) return;
    if (!at(W.px, y)) return;
    prop = W.face ? S_FLIPX : 0;
    flash = (uint8_t)((frame_count >> 1) & 3);
    if (is_cgb) {
        static const uint8_t nova_pal[4] = { OPAL_PETER, OPAL_GOLD, OPAL_BLASTER, OPAL_RED };
        prop |= W.nova_t ? nova_pal[flash] : (W.power == PW_BLASTER ? OPAL_BLASTER : OPAL_PETER);
        if (W.pstate == PS_GROW && W.power == PW_BLASTER) prop = (uint8_t)((prop & ~7) | ((W.state_t >> 2) & 1 ? OPAL_BLASTER : OPAL_PETER));
    } else {
        if (W.nova_t ? (flash & 1) : W.power == PW_BLASTER) prop |= S_PALETTE;
        if (W.pstate == PS_GROW && W.power == PW_BLASTER && ((W.state_t >> 2) & 1)) prop ^= S_PALETTE;
    }
    if (W.pstate >= PS_DEAD) { put2(SPR_PS_DEAD, (uint8_t)(prop & ~S_FLIPX)); return; }
    if (big && W.duck) { oy += 16; put2(SPR_PB_DUCK, prop); return; }
    if (!W.ground && W.jumped) tile = big ? SPR_PB_JUMP : SPR_PS_JUMP;
    else if (W.skid) tile = big ? SPR_PB_SKID : SPR_PS_SKID;
    else if (!W.pvx && W.ground) tile = big ? SPR_PB_STAND : SPR_PS_STAND;
    else tile = (uint8_t)((big ? SPR_PB_WALK0 : SPR_PS_WALK0) + (W.anim << (big ? 3 : 2)));
    if (big) put4(tile, prop); else put2(tile, prop);
}

static const uint8_t score_l[11] = { SPR_N10, SPR_N20, SPR_N40, SPR_N50, SPR_N80, SPR_N10, SPR_N20, SPR_N40, SPR_N50, SPR_N80, SPR_N1U };
static const uint8_t score_r[11] = { SPR_N0, SPR_N0, SPR_N0, SPR_N0, SPR_N0, SPR_N00, SPR_N00, SPR_N00, SPR_N00, SPR_N00, SPR_NP };
static const uint8_t coin_t[4] = { SPR_COIN0, SPR_COIN1, SPR_COIN2, SPR_COIN1 };

static void draw_fx(void)
{
    const Fx *f;
    for (f = W.fx; f != W.fx + MAX_FX; f++) {
        uint8_t k = f->kind;
        if (!k) continue;
        if (k == FX_BUMP) {
            if (at(f->x, (int16_t)(f->y + f->vy))) put2(f->v == T_USED ? SPR_USED : SPR_BRICK, pl_block);
            continue;
        }
        if (!at(f->x, f->y)) continue;
        switch (k) {
        case FX_COIN: {
            uint8_t c = (uint8_t)((f->t >> 1) & 3);
            put1(coin_t[c], (uint8_t)(pl_gold | (c == 3 ? S_FLIPX : 0)));
            break;
        }
        case FX_SCORE:
            put1(score_l[f->v], pl_fx);
            ox += 8;
            put1(score_r[f->v], pl_fx);
            break;
        case FX_SHARD:
            put1(SPR_SHARD, (uint8_t)(pl_block | ((f->t & 4) ? S_FLIPX : 0) | ((f->t & 8) ? S_FLIPY : 0)));
            break;
        case FX_PUFF:
            put1(SPR_PUFF, (uint8_t)(pl_fx | ((f->t & 2) ? S_FLIPX : 0)));
            break;
        }
    }
}

static void draw_ents(void)
{
    const Ent *e;
    for (e = W.e; e != W.e + MAX_ENTS; e++) {
        uint8_t k = e->kind, prop, fall;
        if (!k || k == E_CANNON || !at(e->x, e->y)) continue;
        fall = e->state == ES_FALL;
        prop = fall ? S_FLIPY : 0;
        switch (k) {
        case E_GLOOP:
            prop |= pl_green;
            if (e->state == ES_FLAT) { put2(SPR_GLOOP_FLAT, prop); break; }
            if (e->t & 8) prop |= S_FLIPX;
            put2(SPR_GLOOP, prop);
            break;
        case E_DOME: case E_DOME_RED: case E_JET:
            prop |= k == E_DOME_RED ? pl_red1 : pl_green;
            if (e->vx < 0) prop |= S_FLIPX;
            if (fall) { put2(SPR_SHELL, prop); break; }
            oy -= 16;
            put4((e->t & 8) ? SPR_DOME1 : SPR_DOME0, prop);
            if (k == E_JET) {
                ox = (uint8_t)(e->vx < 0 ? ox + 10 : ox - 2);
                oy += 2;
                put1((frame_count & 4) ? SPR_JET1 : SPR_JET0, (uint8_t)(pl_red1 | (prop & S_FLIPX)));
            }
            break;
        case E_SHELL: case E_SHELL_RED:
            prop |= k == E_SHELL_RED ? pl_red1 : pl_green;
            put2((!e->vx && e->t < 60 && (e->t & 4)) ? SPR_SHELL_WAKE : SPR_SHELL, prop);
            break;
        case E_CHOMP:
            prop |= (uint8_t)(pl_green | (fall ? 0 : S_PRIORITY));
            put4((e->ys & 8) ? SPR_CHOMP1 : SPR_CHOMP0, prop);
            break;
        case E_COMET:
            prop |= pl_red1;
            if (e->vx < 0) prop |= S_FLIPX;
            put2(SPR_COMET, prop);
            break;
        }
    }
}

static void draw_item(void)
{
    const Ent *e = &W.item;
    uint8_t k = e->kind, prop;
    if (!k || !at(e->x, e->y)) return;
    prop = k == E_NOVA ? pl_gold : pl_item;
    if (k == E_BLASTER && is_cgb && (frame_count & 4)) prop = OPAL_RED;
    if (e->state == ES_SPROUT) prop |= S_PRIORITY;
    put2(k == E_PLANET ? SPR_PLANET : k == E_BLASTER ? SPR_BLASTER : k == E_NOVA ? SPR_NOVA : SPR_1UP, prop);
}

static void draw_sprites(void)
{
    const Ent *s;
    uint8_t *p;
    if (is_cgb) {
        pl_gold = OPAL_GOLD; pl_fx = OPAL_FX; pl_green = OPAL_GREEN; pl_red = pl_red1 = OPAL_RED;
        pl_item = OPAL_ITEM; pl_block = OPAL_BLOCK;
    } else {
        pl_gold = pl_fx = pl_green = pl_red = pl_item = pl_block = 0;
        pl_red1 = S_PALETTE;
    }
    op8 = (uint8_t *)shadow_OAM;
    draw_peter();
    for (s = W.shot; s != W.shot + MAX_SHOTS; s++)
        if (s->kind && at(s->x, s->y))
            put1(SPR_SHOT, (uint8_t)(pl_fx | ((frame_count & 2) ? S_FLIPX : 0) | ((frame_count & 4) ? S_FLIPY : 0)));
    dbg_sprl[0] = LY_REG;
    draw_ents();
    dbg_sprl[1] = LY_REG;
    draw_item();
    dbg_spr2[0] = LY_REG;
    if (W.flag_col != 0xFFFF && at((uint16_t)(W.flag_col * 16 - 8), W.flag_y)) put2(SPR_FLAG, pl_gold);
    dbg_spr2[1] = LY_REG;
    draw_fx();
    dbg_spr2[2] = LY_REG;
    /* hide the objects left over from the last frame (only those) */
    {
        uint8_t *end = op8;
        for (p = op8; p < oam_last; p += 4) *p = 0;
        oam_last = end;
    }
    dbg_spr2[3] = LY_REG;
}

/* ------------------------------------------------------------------ HUD */
/* row 0: PETER    *x00  ^300
   row 1: 0000000  @x03 SEC 1 */
void hud_draw_all(void) BANKED
{
    uint8_t blank[20], i;
    for (i = 0; i < 20; i++) blank[i] = TILE_BLANK;
    set_win_tiles(0, 0, 20, 1, blank);
    set_win_tiles(0, 1, 20, 1, blank);
    if (is_cgb) {
        uint8_t a[20];
        for (i = 0; i < 20; i++) a[i] = PAL_HUD;
        VBK_REG = 1;
        set_win_tiles(0, 0, 20, 1, a);
        set_win_tiles(0, 1, 20, 1, a);
        VBK_REG = 0;
    }
    print_win(0, 0, "PETER");
    print_win(9, 0, "*x");
    print_win(15, 0, "^");
    print_win(9, 1, "@x");
    print_win(14, 1, "S");
    hud_coins = hud_lives = hud_sector_v = 0xFF;
    hud_time = 0xFFFF;
    hud_score_rev = (uint8_t)(W.score_rev - 1);
}

void hud_pause(uint8_t on) BANKED
{
    if (on) {
        char t[5];
        print_win(0, 0, "PAUSE");
        print_win(0, 1, "SEED     ");
        {
            static const char hex[] = "0123456789ABCDEF";
            t[0] = hex[(W.seed >> 12) & 15]; t[1] = hex[(W.seed >> 8) & 15];
            t[2] = hex[(W.seed >> 4) & 15]; t[3] = hex[W.seed & 15]; t[4] = 0;
        }
        print_win(5, 1, t);
    } else {
        print_win(0, 0, "PETER   ");
        hud_score_rev = (uint8_t)(W.score_rev - 1);
    }
}

static uint8_t hud_turn;

/* one HUD field per frame, in turn (each costs a decimal conversion and VRAM writes) */
static void hud_update(void)
{
    char t[8];
    {
        extern uint8_t dbg_hud_case;
        dbg_hud_case = (uint8_t)(hud_turn + 1) & 3;
    }
    switch (++hud_turn & 3) {
    case 0:
        if (W.score_rev != hud_score_rev) {     /* the sim keeps the score's digits */
            uint8_t i;
            hud_score_rev = W.score_rev;
            for (i = 0; i < 7; i++) t[i] = (char)('0' + W.sdig[i]);
            t[7] = 0;
            hud_print(0, 1, t);
        }
        break;
    case 1:
        if (W.coins != hud_coins) { hud_coins = W.coins; fmt_u16(t, hud_coins, 2); hud_print(11, 0, t); }
        if (W.lives != hud_lives) { hud_lives = W.lives; fmt_u16(t, hud_lives, 2); hud_print(11, 1, t); }
        break;
    case 2:
        if (W.time != hud_time) { hud_time = W.time; fmt_u16(t, hud_time, 3); hud_print(16, 0, t); }
        break;
    default:
        if ((uint8_t)W.sectors_done != hud_sector_v) {
            hud_sector_v = (uint8_t)W.sectors_done;
            fmt_u16(t, (uint16_t)(W.sectors_done + 1 > 999 ? 999 : W.sectors_done + 1), 3);
            hud_print(16, 1, t);
        }
    }
}

/* ------------------------------------------------------------------ sound */
static void sounds(void)
{
    uint16_t s = W.sfx;
    uint8_t m = W.mev;
    if (m & MEV_GAMEOVER) music_play(MUS_GAMEOVER);
    else if (s & EV_DIE) music_play(MUS_DEATH);
    else if (m & MEV_RESPAWN) { music_hurry(0); music_play(MUS_MAIN); }
    else if (m & MEV_NOVA) music_play(MUS_NOVA);
    else if (m & MEV_NOVA_END) music_play(MUS_MAIN);
    else if (m & MEV_HURRY) music_play(MUS_HURRY);
    if (!s) return;
    if (s & EV_POWERUP) sfx_play(SFX_POWERUP);
    else if (s & EV_POWERDOWN) sfx_play(SFX_POWERDOWN);
    else if (s & EV_1UP) sfx_play(SFX_1UP);
    else if (s & EV_FLAG) sfx_play(SFX_FLAG);
    else if (s & EV_SPROUT) sfx_play(SFX_SPROUT);
    else if (s & EV_BREAK) sfx_play(SFX_BREAK);
    else if (s & EV_STOMP) sfx_play(SFX_STOMP);
    else if (s & EV_KICK) sfx_play(SFX_KICK);
    else if (s & EV_JUMP_BIG) sfx_play(SFX_JUMP_BIG);
    else if (s & EV_JUMP) sfx_play(SFX_JUMP);
    else if (s & EV_SHOT) sfx_play(SFX_SHOT);
    else if (s & EV_LAUNCH) sfx_play(SFX_LAUNCH);
    else if (s & EV_BUMP) sfx_play(SFX_BUMP);
    if (s & EV_COIN) sfx_play(SFX_COIN);
    else if (s & EV_TICK) sfx_play(SFX_TICK);
}


void render_frame(void) BANKED
{
    stream();
    dbg_ly[2] = LY_REG;
    draw_sprites();
    dbg_ly[3] = LY_REG;
    hud_update();
    dbg_ly[7] = LY_REG;
    sounds();
    dbg_ly[4] = LY_REG;
    /* the tile animation: the capsule's 4 tiles on one frame, the star bit's on another */
    if (!(anim_t & 3) && (anim_t >> 2) < ANIM_COUNT / 4) anim_group((uint8_t)(anim_t >> 2), anim_f);
    if (++anim_t >= ANIM_PERIOD) {
        anim_t = 0;
        anim_f = (uint8_t)((anim_f + 1) & (ANIM_FRAMES - 1));
    }
    scx = (uint8_t)W.cam_x;
    scy = (uint8_t)(W.cam_y - 16);
}
