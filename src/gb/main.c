/* main.c - SPEEDY PURPLE PETER: boot, interrupts and the frame loop (bank 0).
 *
 * Screen layout: the HUD is the window over lines 0-15 (a STAT interrupt at LY=15 turns the
 * window off for the rest of the frame), the level is the BG below it. The BG map is a ring of
 * 16 metatile columns (32 tiles) streamed just ahead of the camera.
 *
 * Frame protocol: the loop runs the simulation, writes sprites to shadow OAM and the next
 * scroll, then waits for VBlank. GBDK's VBlank handler copies OAM; the scroll is latched right
 * after it, so sprites and the background always move together. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include <stddef.h>
#include "front.h"
#include "assets.h"
#include "sound.h"

uint8_t is_cgb;
uint8_t keys, pressed;
static uint8_t prev_keys;
uint8_t scx, scy;
uint8_t hud_on;
uint16_t frame_count;

uint8_t ram_mt_tiles[T_COUNT][4];
uint8_t ram_mt_attr[T_COUNT][4];
uint8_t ram_font[128];
static uint8_t ram_anim[ANIM_FRAMES][ANIM_COUNT][16];   /* [frame][tile]: a group's tiles are contiguous */
static uint8_t ram_anim_tile[ANIM_COUNT];

/* for the ROM tests: where the fields of W are (the SDCC layout differs from the host's) */
const uint16_t dbg_world_off[] = {
    offsetof(World, px), offsetof(World, py), offsetof(World, pvx), offsetof(World, pvy),
    offsetof(World, power), offsetof(World, pstate), offsetof(World, lives), offsetof(World, score),
    offsetof(World, frames), offsetof(World, sectors_done), offsetof(World, dist), offsetof(World, time),
    offsetof(World, cam_x), offsetof(World, over), offsetof(World, coins), offsetof(World, seed),
    offsetof(World, ground), offsetof(World, nova_t), offsetof(World, cam_y), offsetof(World, god),
    sizeof(World), offsetof(World, check_col), offsetof(World, gen_col), offsetof(World, lv),
    offsetof(World, bonus), offsetof(World, gen), offsetof(World, e), offsetof(World, mode)
};

/* debug counters the ROM tests read (by symbol) */
uint16_t dbg_frames, dbg_drops;
uint16_t dbg_steps;               /* sim steps since the run began */
/* test hook: with dbg_feed set, the run takes its buttons from dbg_ring[step & 255] (the tests
   keep it filled ahead) and holds still at step dbg_stop_at */
uint8_t dbg_feed, dbg_fed_prev;
uint16_t dbg_stop_at;
uint8_t dbg_ring[256];
uint8_t dbg_ly[8];                 /* LY at stages of this frame; [0] = its start (the renderer
                                      uses it to tell a busy frame) */
#ifdef SPP_PROFILE                 /* profiling stamps (-DSPP_PROFILE builds) */
uint8_t dbg_sim_ly[16];
uint8_t dbg_slow[24];              /* dbg_ly + dbg_sim_ly of the last slow frame */
uint8_t dbg_gen_ly[4];
uint8_t dbg_ent_ly[6];             /* lines each entity's full update took */
#endif
static uint16_t last_vbl;
uint8_t play_mode;                 /* MODE_*: picked on the mode screen (screens.c) */

static void lcd_isr(void)
{
    /* the HUD ends at line 15: window off, sprites on (they never cover the HUD) */
    LCDC_REG = (uint8_t)((LCDC_REG & ~LCDCF_WINON) | LCDCF_OBJON);
}

/* ---- everything written to VRAM while the screen is on: staged here by the main loop, written
   by the VBlank handler (vbl.s, which has the timing). Each queue is the main loop's while its
   count is 0 and the handler's from the moment the count is set until the handler clears it. */
uint8_t col_buf[2][LV_ROWS * 4];        /* the next BG column: tiles, CGB attributes (2 per row) */
uint8_t *col_src, *col_dst;             /* where the next half comes from and goes to */
volatile uint8_t col_pending;           /* halves still to write (2, 1, 0) */
const uint8_t *anim_src;                /* an animated group's 64 bytes, and their tiles in VRAM */
uint8_t *anim_dst;
volatile uint8_t anim_req;
uint8_t hud_q[HUDQ * 4];                /* HUD tiles: address, tile, (unused) */
volatile uint8_t hud_ready;
uint8_t cell_q[2][CELLQ * 6];           /* changed cells: address and 4 tiles; the same with attributes */
volatile uint8_t cell_ready;
uint8_t dbg_vbl_late, dbg_vbl_ly;       /* VBlanks whose writes ran over; the latest line one ended on */
void vbl_vram(void);

static void vbl_isr(void)
{
    if (hud_on) LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_WINON) & ~LCDCF_OBJON);
    else LCDC_REG |= LCDCF_OBJON;
    vbl_vram();
    snd_tick();
}

void read_input(void)
{
    prev_keys = keys;
    keys = joypad();
    pressed = (uint8_t)(keys & ~prev_keys);
}

/* text: in bank 0 so a banked caller's string literals stay readable */
void print(uint8_t x, uint8_t y, const char *s)
{
    uint8_t buf[20], n = 0;
    while (*s && n < 20) buf[n++] = ram_font[(uint8_t)*s++ & 127];
    set_bkg_tiles(x, y, n, 1, buf);
}

void print_win(uint8_t x, uint8_t y, const char *s)
{
    uint8_t buf[20], n = 0;
    while (*s && n < 20) buf[n++] = ram_font[(uint8_t)*s++ & 127];
    set_win_tiles(x, y, n, 1, buf);
}

void far_copy(void *dst, const void *src, uint16_t n)
{
    uint8_t b = CURRENT_BANK;
    SWITCH_ROM(BANK(assets));
    memcpy(dst, src, n);
    SWITCH_ROM(b);
}

void load_world_gfx(void)
{
    uint8_t b = CURRENT_BANK;
    SWITCH_ROM(BANK(assets));
    set_bkg_data(0, BG_TILES_LO, bg_tiles);
    if (BG_TILES_HI) set_bkg_data(208, BG_TILES_HI, bg_tiles + BG_TILES_LO * 16);
    set_sprite_data(0, SPR_TILE_COUNT, spr_tiles);
    memcpy(ram_mt_tiles, mt_tiles, sizeof(ram_mt_tiles));
    memcpy(ram_mt_attr, mt_attr, sizeof(ram_mt_attr));
    memcpy(ram_font, font_map, sizeof(ram_font));
    {
        uint8_t i, f;
        for (i = 0; i < ANIM_COUNT; i++)
            for (f = 0; f < ANIM_FRAMES; f++) memcpy(ram_anim[f][i], anim_frames[i][f], 16);
    }
    memcpy(ram_anim_tile, anim_tile, sizeof(ram_anim_tile));
    if (is_cgb) {
        set_bkg_palette(0, 8, &cgb_bg_pal[0][0]);
        set_sprite_palette(0, 8, &cgb_obj_pal[0][0]);
    }
    SWITCH_ROM(b);
    BGP_REG = DMG_BGP;
    OBP0_REG = DMG_OBP0;
    OBP1_REG = DMG_OBP1;
}

void load_title_gfx(void)
{
    uint8_t b = CURRENT_BANK, y;
    load_world_gfx();
    SWITCH_ROM(BANK(assets));
    set_bkg_data(208, TITLE_TILE_COUNT, title_tiles);
    for (y = 0; y < 18; y++) {
        set_bkg_tiles(0, y, 20, 1, title_map[y]);
        if (is_cgb) {
            VBK_REG = 1;
            set_bkg_tiles(0, y, 20, 1, title_attr[y]);
            VBK_REG = 0;
        }
    }
    SWITCH_ROM(b);
}

/* animated tiles come in groups of 4 consecutive tiles (the capsule, the star bit): the group's
   next frame is copied in by the VBlank handler. 0 if the last one is still waiting. */
uint8_t anim_group(uint8_t g, uint8_t f)
{
    uint8_t t;
    if (anim_req) return 0;
    t = ram_anim_tile[g << 2];
    anim_src = ram_anim[f][g << 2];
    anim_dst = (uint8_t *)(t < 128 ? 0x9000 + t * 16 : 0x8000 + t * 16);
    anim_req = 1;
    return 1;
}

void frame_end(void)
{
    wait_vbl_done();
    SCX_REG = scx;
    SCY_REG = scy;
    frame_count++;
    dbg_frames++;
    if ((uint16_t)(sys_time - last_vbl) > 1) {
        dbg_drops++;
#ifdef SPP_PROFILE
        memcpy(dbg_slow, dbg_ly, 8);            /* what the slow frame spent its time on */
        memcpy(dbg_slow + 8, dbg_sim_ly, 16);
#endif
    }
    last_vbl = sys_time;
}

/* one run, from the seed to game over (or a quit) */
static void play(uint16_t seed)
{
    uint8_t r;
restart:
    DISPLAY_OFF;
    hud_on = 0;
    load_world_gfx();
    sim_init(seed, play_mode);
    dbg_steps = 0;
    render_reset();
    hud_draw_all();
    music_hurry(0);
    music_play(MUS_MAIN);
    hud_on = 1;
    SHOW_WIN;
    DISPLAY_ON;
    keys = joypad();
    wait_vbl_done();
    last_vbl = sys_time;           /* the setup above is not a slow game frame */
    for (;;) {
        read_input();
        if (dbg_feed) {
            uint8_t k;
            while (dbg_steps == dbg_stop_at) frame_end();
            k = dbg_ring[(uint8_t)dbg_steps];
            pressed = (uint8_t)(k & ~dbg_fed_prev);
            keys = dbg_fed_prev = k;
        }
        if ((pressed & J_START) && !W.over && W.pstate == PS_PLAY) {
            r = pause_screen();
            if (r == 1) goto restart;
            if (r == 2) return;
            continue;
        }
        dbg_ly[0] = LY_REG;
        dbg_steps++;
        sim_step(keys);
        dbg_ly[1] = LY_REG;
        render_frame();
        dbg_ly[5] = LY_REG;
        frame_end();
        if (W.over && music_done()) {
            uint8_t t;
            for (t = 0; t < 30; t++) frame_end();
            game_over_screen();
            return;
        }
    }
}

/* Colour hardware is told by what it does, not by the boot value of A (some emulators boot a DMG
   with A = 0x11) or by unused register bits (many emulators read VBK back without its 0xFE bits):
   only a CGB keeps two VRAM banks apart. Needs the LCD off. */
static uint8_t probe_cgb(void)
{
    volatile uint8_t *p = (volatile uint8_t *)0x9FFF;  /* the corner of the window map */
    uint8_t r;
    VBK_REG = 1;
    *p = 0x5A;
    VBK_REG = 0;
    *p = 0xA5;               /* on a DMG this overwrites the 0x5A: there is only one bank */
    VBK_REG = 1;
    r = (uint8_t)(*p == 0x5A);
    *p = 0;
    VBK_REG = 0;
    *p = 0;
    return r;
}

void main(void)
{
    DISPLAY_OFF;
    is_cgb = probe_cgb();
    if (is_cgb) cpu_fast();
    ENABLE_RAM;
    SWITCH_RAM(0);
    save_load();
    snd_init();
    LCDC_REG = (uint8_t)(LCDCF_OFF | LCDCF_WIN9C00 | LCDCF_BG8800 | LCDCF_BG9800 | LCDCF_OBJ16 |
                         LCDCF_OBJON | LCDCF_BGON);
    WX_REG = 7;
    WY_REG = 0;
    LYC_REG = 15;
    STAT_REG = STATF_LYC;
    CRITICAL {
        add_VBL(vbl_isr);
        add_LCD(lcd_isr);
    }
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    for (;;) {
        uint16_t seed = title_screen();
        play(seed);
    }
}
