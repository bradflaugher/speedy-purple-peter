/* screens.c - SPEEDY PURPLE PETER: title (seed entry), pause, game over and the battery save.
 * Banked. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "front.h"
#include "assets.h"
#include "sound.h"

uint32_t best_score[MODE_COUNT];     /* a best per mode: assisted runs never beat classic ones */
uint16_t best_dist[MODE_COUNT];
uint16_t last_seed;
uint8_t seed_saved;                  /* last_seed holds a seed played (0000 is a seed too) */

/* ------------------------------------------------------------------ save (MBC5 SRAM) */
#define SAVE_MAGIC 0x5050    /* "PP" */
#define SAVE_VER   2
typedef struct {
    uint16_t magic;
    uint8_t ver;
    uint32_t best_score[MODE_COUNT];
    uint16_t best_dist[MODE_COUNT];
    uint16_t last_seed;
    uint8_t seed_saved;
    uint8_t last_mode;
    uint8_t check;
} Save;

typedef struct {             /* version 1: one best, no mode */
    uint16_t magic;
    uint8_t ver;
    uint32_t best_score;
    uint16_t best_dist;
    uint16_t last_seed;
    uint8_t check;
} SaveV1;

static uint8_t checksum(const uint8_t *p, uint8_t n)
{
    uint8_t c = 0x5A;
    while (n--) c = (uint8_t)((c << 1 | c >> 7) ^ *p++);
    return c;
}

void save_load(void) BANKED
{
    Save s;
    uint8_t i;
    ENABLE_RAM;
    SWITCH_RAM(0);
    memcpy(&s, (const void *)0xA000, sizeof(s));
    DISABLE_RAM;
    memset(best_score, 0, sizeof(best_score));
    memset(best_dist, 0, sizeof(best_dist));
    last_seed = 0;
    seed_saved = 0;
    play_mode = MODE_CLASSIC;
    if (s.magic != SAVE_MAGIC) return;
    if (s.ver == SAVE_VER && s.check == checksum((const uint8_t *)&s, sizeof(s) - 1)) {
        for (i = 0; i < MODE_COUNT; i++) { best_score[i] = s.best_score[i]; best_dist[i] = s.best_dist[i]; }
        last_seed = s.last_seed;
        seed_saved = s.seed_saved;
        play_mode = s.last_mode < MODE_COUNT ? s.last_mode : MODE_CLASSIC;
    } else {
        const SaveV1 *v = (const SaveV1 *)&s;  /* an older save: its best was a classic run */
        if (v->ver == 1 && v->check == checksum((const uint8_t *)v, sizeof(SaveV1) - 1)) {
            best_score[MODE_CLASSIC] = v->best_score;
            best_dist[MODE_CLASSIC] = v->best_dist;
            last_seed = v->last_seed;
            seed_saved = last_seed != 0;
        }
    }
}

static void save_store(void)
{
    Save s;
    s.magic = SAVE_MAGIC;
    s.ver = SAVE_VER;
    memcpy(s.best_score, best_score, sizeof(best_score));
    memcpy(s.best_dist, best_dist, sizeof(best_dist));
    s.last_seed = last_seed;
    s.seed_saved = seed_saved;
    s.last_mode = play_mode;
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

/* Peter sprints in place on the title's ground, in front of his speed streaks: a 16x32 frame of
   four 8x16 objects, the top half always DASH0, the bottom half DASH0's stride or DASH1's high
   knee. OPAL_PETER on CGB, OBP0 on DMG. */
#define DASH_X (16 + 8)          /* OAM position of the top-left object (screen x 16) */
#define DASH_Y (40 + 16)         /* screen y 40: his feet on the ground row (y 71) */
#define DASH_PERIOD 4            /* frames per pose: a quick 2-frame run cycle */

static uint8_t trail[2];          /* the trail's tail tiles (title map column 0, rows 5-6) */

static void title_peter(uint8_t pose)
{
    uint8_t bot = pose ? SPR_PB_DASH1 : (uint8_t)(SPR_PB_DASH0 + 4);
    uint8_t prop = is_cgb ? OPAL_PETER : 0;
    volatile OAM_item_t *o = shadow_OAM;
    o[0].y = DASH_Y;      o[0].x = DASH_X;     o[0].tile = SPR_PB_DASH0;       o[0].prop = prop;
    o[1].y = DASH_Y;      o[1].x = DASH_X + 8; o[1].tile = SPR_PB_DASH0 + 2;   o[1].prop = prop;
    o[2].y = DASH_Y + 16; o[2].x = DASH_X;     o[2].tile = bot;                o[2].prop = prop;
    o[3].y = DASH_Y + 16; o[3].x = DASH_X + 8; o[3].tile = (uint8_t)(bot + 2); o[3].prop = prop;
}

/* the speed streak behind him flickers with his stride: its tail is there on one pose only */
static void title_trail(uint8_t pose)
{
    static const uint8_t blank[2] = { TILE_BLANK, TILE_BLANK };
    set_bkg_tiles(0, 5, 1, 2, pose ? blank : trail);
}

/* the modes as the menus name and explain them */
static const char *const mode_name[MODE_COUNT] = { "CLASSIC", "AUTO SPRINT", "AUTO RUN" };
static const char *const mode_help[MODE_COUNT] = { "B RUNS  A JUMPS", "ALWAYS RUNNING", "ONLY JUMP!" };

/* "BEST 0000000 00000M" for each mode, made once per title (the 32-bit formatting is slow) */
static char best_line[MODE_COUNT][20];

static void make_best_lines(void)
{
    uint8_t m;
    for (m = 0; m < MODE_COUNT; m++) {
        char *t = best_line[m];
        memcpy(t, "BEST ", 5);
        fmt_u32(t + 5, best_score[m], 7);
        t[12] = ' ';
        fmt_u32(t + 13, best_dist[m], 5);
        t[18] = 'M';
        t[19] = 0;
    }
}

/* the title's text rows for step 0 (the seed) or 1 (the mode) */
static void title_text(uint8_t step)
{
    char t[20];
    uint8_t m;
    clear_rows(11, 17);
    if (!step) {
        print(4, 12, "PRESS START");
        print(3, 14, "SEED");
        print(1, 16, best_line[play_mode]);
        memcpy(t, "MODE ", 5);
        strcpy(t + 5, mode_name[play_mode]);
        print(1, 17, t);
        return;
    }
    print(3, 11, "CHOOSE A MODE");
    for (m = 0; m < MODE_COUNT; m++) print(4, (uint8_t)(12 + m), mode_name[m]);
}

/* step 1: the mode menu's cursor, its help line and that mode's best */
static void mode_text(void)
{
    uint8_t m;
    for (m = 0; m < MODE_COUNT; m++) print(2, (uint8_t)(12 + m), m == play_mode ? ">" : " ");
    print(0, 15, "                    ");
    print((uint8_t)((20 - strlen(mode_help[play_mode])) / 2), 15, mode_help[play_mode]);
    print(1, 16, best_line[play_mode]);
    print(1, 17, "START:GO  B:BACK");
}

uint16_t title_screen(void) BANKED
{
    char t[12];
    uint8_t cursor = 0, blink = 0, edited = 0, run = 0, step = 0;
    uint16_t seed = seed_saved ? last_seed : 0x1985;
    DISPLAY_OFF;
    hud_on = 0;
    HIDE_WIN;
    sprites_clear();
    load_title_gfx();
    make_best_lines();
    title_text(0);
    far_copy(&trail[0], &title_map[5][0], 1);
    far_copy(&trail[1], &title_map[6][0], 1);
    title_peter(0);
    music_play(MUS_TITLE);
    DISPLAY_ON;
    for (;;) {
        if (!step) {
            hex4(t, seed);
            print(8, 14, t);
            /* the digit being edited blinks */
            if ((blink & 16) && edited) print((uint8_t)(8 + cursor), 14, " ");
            print(13, 14, "SEL:NEW");
        }
        wait_frame();
        /* the pose set last frame is on screen now: its trail goes with it */
        if (run == 0 || run == DASH_PERIOD) title_trail(run != 0);
        blink++;
        if (++run == 2 * DASH_PERIOD) run = 0;
        title_peter(run >= DASH_PERIOD);
        if (step) {                          /* the mode menu */
            if (pressed & (J_START | J_A)) break;
            if (pressed & J_B) { step = 0; title_text(0); sfx_play(SFX_SELECT); continue; }
            if (pressed & (J_UP | J_DOWN | J_SELECT)) {
                play_mode = (uint8_t)((play_mode + ((pressed & J_UP) ? MODE_COUNT - 1 : 1)) % MODE_COUNT);
                mode_text();
                sfx_play(SFX_SELECT);
            }
            continue;
        }
        if (pressed & J_START) {             /* on to the mode menu */
            step = 1;
            title_text(1);
            mode_text();
            sfx_play(SFX_SELECT);
            continue;
        }
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
    sprites_clear();                         /* Peter leaves with the title */
    sfx_play(SFX_PAUSE);
    last_seed = seed;
    seed_saved = 1;
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
    uint8_t m = W.mode;
    if (W.score > best_score[m]) { best_score[m] = W.score; newbest = 1; }
    if (W.dist > best_dist[m]) { best_dist[m] = W.dist; newbest |= 2; }
    if (newbest) save_store();
    DISPLAY_OFF;
    hud_on = 0;
    HIDE_WIN;
    sprites_clear();
    load_world_gfx();
    clear_rows(0, 17);
    print(5, 1, "GAME  OVER");
    print(2, 3, "SCORE");
    fmt_u32(t, W.score, 7); print(11, 3, t);
    print(2, 5, "DISTANCE");
    fmt_u32(t, W.dist, 5); t[5] = 'M'; t[6] = 0; print(12, 5, t);
    print(2, 7, "SECTORS");
    fmt_u32(t, W.sectors_done, 4); print(13, 7, t);
    print(2, 9, "RUN TIME");
    fmt_time(t, W.frames); print(11, 9, t);
    print(2, 11, "SEED");
    hex4(t, W.seed); print(13, 11, t);
    print(2, 13, "MODE");
    print((uint8_t)(18 - strlen(mode_name[m])), 13, mode_name[m]);
    if (newbest & 1) print(4, 15, "NEW BEST SCORE");
    else if (newbest & 2) print(2, 15, "NEW BEST DISTANCE");
    print(4, 17, "PRESS START");
    DISPLAY_ON;
    for (i = 0; i < 40; i++) wait_frame();
    do wait_frame(); while (!(pressed & (J_START | J_A)));
    sfx_play(SFX_SELECT);
}
