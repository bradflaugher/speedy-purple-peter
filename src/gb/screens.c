/* screens.c - SPEEDY PURPLE PETER: title (seed entry), pause, game over and the battery save.
 * Banked. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "front.h"
#include "assets.h"
#include "sound.h"

uint32_t best_score;
uint16_t best_dist;
uint16_t last_seed;

/* ------------------------------------------------------------------ save (MBC5 SRAM) */
#define SAVE_MAGIC 0x5050    /* "PP" */
#define SAVE_VER   1
typedef struct {
    uint16_t magic;
    uint8_t ver;
    uint32_t best_score;
    uint16_t best_dist;
    uint16_t last_seed;
    uint8_t check;
} Save;

static uint8_t checksum(const uint8_t *p, uint8_t n)
{
    uint8_t c = 0x5A;
    while (n--) c = (uint8_t)((c << 1 | c >> 7) ^ *p++);
    return c;
}

void save_load(void) BANKED
{
    Save s;
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy(&s, (const void *)0xA000, sizeof(s));
    DISABLE_RAM;
    if (s.magic == SAVE_MAGIC && s.ver == SAVE_VER && s.check == checksum((const uint8_t *)&s, sizeof(s) - 1)) {
        best_score = s.best_score;
        best_dist = s.best_dist;
        last_seed = s.last_seed;
    } else {
        best_score = 0;
        best_dist = 0;
        last_seed = 0;
    }
}

static void save_store(void)
{
    Save s;
    s.magic = SAVE_MAGIC;
    s.ver = SAVE_VER;
    s.best_score = best_score;
    s.best_dist = best_dist;
    s.last_seed = last_seed;
    s.check = checksum((const uint8_t *)&s, sizeof(s) - 1);
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy((void *)0xA000, &s, sizeof(s));
    DISABLE_RAM;
}

/* ------------------------------------------------------------------ helpers */
static const char hexd[] = "0123456789ABCDEF";

static void hex4(char *t, uint16_t v)
{
    t[0] = hexd[(v >> 12) & 15]; t[1] = hexd[(v >> 8) & 15];
    t[2] = hexd[(v >> 4) & 15]; t[3] = hexd[v & 15]; t[4] = 0;
}

static void clear_rows(uint8_t y0, uint8_t y1)
{
    uint8_t blank[20], y;
    memset(blank, TILE_BLANK, 20);
    for (y = y0; y <= y1; y++) {
        set_bkg_tiles(0, y, 20, 1, blank);
        if (is_cgb) {
            uint8_t a[20];
            memset(a, PAL_HUD, 20);
            VBK_REG = 1;
            set_bkg_tiles(0, y, 20, 1, a);
            VBK_REG = 0;
        }
    }
}

static void wait_frame(void)
{
    scx = 0;
    scy = 0;
    frame_end();
    read_input();
}

/* ------------------------------------------------------------------ title */
static uint16_t rnd_seed(void)
{
    uint16_t s = (uint16_t)(DIV_REG | ((uint16_t)frame_count << 8)) ^ (uint16_t)(sys_time * 0x9E37u);
    return s;
}

uint16_t title_screen(void) BANKED
{
    char t[12];
    uint8_t cursor = 0, blink = 0, edited = 0;
    uint16_t seed = last_seed ? last_seed : 0x1985;
    DISPLAY_OFF;
    hud_on = 0;
    HIDE_WIN;
    sprites_clear();
    load_title_gfx();
    print(4, 12, "PRESS START");
    print(3, 14, "SEED");
    print(3, 16, "BEST");
    fmt_u32(t, best_score, 7);
    print(8, 16, t);
    fmt_u32(t, best_dist, 5);
    t[5] = 'M'; t[6] = 0;
    print(8, 17, t);
    music_play(MUS_TITLE);
    DISPLAY_ON;
    for (;;) {
        hex4(t, seed);
        print(8, 14, t);
        /* the digit being edited blinks */
        if ((blink & 16) && edited) print((uint8_t)(8 + cursor), 14, " ");
        print(13, 14, "SEL:NEW");
        wait_frame();
        blink++;
        if (pressed & J_START) break;
        if (pressed & J_SELECT) { seed = rnd_seed(); sfx_play(SFX_SELECT); }
        if (pressed & J_LEFT) { cursor = (uint8_t)((cursor + 3) & 3); edited = 1; sfx_play(SFX_SELECT); }
        if (pressed & J_RIGHT) { cursor = (uint8_t)((cursor + 1) & 3); edited = 1; sfx_play(SFX_SELECT); }
        if (pressed & (J_UP | J_DOWN)) {
            uint8_t sh = (uint8_t)((3 - cursor) * 4);
            uint16_t d = (uint16_t)((seed >> sh) & 15);
            d = (uint16_t)((d + ((pressed & J_UP) ? 1 : 15)) & 15);
            seed = (uint16_t)((seed & ~(0xFu << sh)) | (d << sh));
            edited = 1;
            blink = 16;
            sfx_play(SFX_SELECT);
        }
    }
    sfx_play(SFX_PAUSE);
    last_seed = seed;
    save_store();
    return seed;
}

/* ------------------------------------------------------------------ pause */
uint8_t pause_screen(void) BANKED
{
    uint8_t r = 0;
    sfx_play(SFX_PAUSE);
    hud_pause(1);
    for (;;) {
        frame_end();
        read_input();
        if (pressed & J_START) { r = 0; break; }
        if (pressed & J_SELECT) { r = 1; break; }      /* restart this seed: a speedrun reset */
        if ((keys & (J_A | J_B)) == (J_A | J_B)) { r = 2; break; }
    }
    hud_pause(0);
    if (r) { music_play(MUS_NONE); }
    else sfx_play(SFX_PAUSE);
    return r;
}

/* ------------------------------------------------------------------ game over */
void game_over_screen(void) BANKED
{
    char t[12];
    uint8_t newbest = 0, i;
    if (W.score > best_score) { best_score = W.score; newbest = 1; }
    if (W.dist > best_dist) { best_dist = W.dist; newbest |= 2; }
    if (newbest) save_store();
    DISPLAY_OFF;
    hud_on = 0;
    HIDE_WIN;
    sprites_clear();
    load_world_gfx();
    clear_rows(0, 17);
    print(5, 1, "GAME  OVER");
    print(2, 4, "SCORE");
    fmt_u32(t, W.score, 7); print(11, 4, t);
    print(2, 6, "DISTANCE");
    fmt_u32(t, W.dist, 5); t[5] = 'M'; t[6] = 0; print(12, 6, t);
    print(2, 8, "SECTORS");
    fmt_u32(t, W.sectors_done, 4); print(13, 8, t);
    print(2, 10, "RUN TIME");
    fmt_time(t, W.frames); print(11, 10, t);
    print(2, 12, "SEED");
    hex4(t, W.seed); print(13, 12, t);
    if (newbest & 1) print(4, 14, "NEW BEST SCORE");
    else if (newbest & 2) print(3, 14, "NEW BEST DISTANCE");
    print(4, 16, "PRESS START");
    DISPLAY_ON;
    for (i = 0; i < 40; i++) wait_frame();
    do wait_frame(); while (!(pressed & (J_START | J_A)));
    sfx_play(SFX_SELECT);
}
