/* front.h - SPEEDY PURPLE PETER Game Boy front end: shared declarations.
 *
 * Bank 0 (main.c): boot, interrupts, the frame loop, and the helpers that read the banked art.
 * render.c (banked): the level's BG streaming, sprites, the HUD.
 * screens.c (banked): title (seed entry), pause, game over, the battery save. */
#ifndef SPP_FRONT_H
#define SPP_FRONT_H

#include <gb/gb.h>
#include <stdint.h>
#include "sim.h"

/* ---- main.c (bank 0) ---- */
extern uint8_t is_cgb;
extern uint8_t keys, pressed;          /* this frame's buttons, and the ones newly pressed */
extern uint8_t scx, scy;               /* the next frame's BG scroll */
extern uint8_t hud_on;                 /* the window shows the HUD over lines 0-15 */
extern uint16_t frame_count;

/* run-time copies of the art tables (the art itself is in a switchable bank) */
extern uint8_t ram_mt_tiles[T_COUNT][4];
extern uint8_t ram_mt_attr[T_COUNT][4];
extern uint8_t ram_font[128];

extern uint8_t col_buf[2][LV_ROWS * 4];
extern uint8_t *col_dst;
extern volatile uint8_t col_pending;

void read_input(void);
void print(uint8_t x, uint8_t y, const char *s);      /* text on the BG map */
void print_win(uint8_t x, uint8_t y, const char *s);  /* text on the window (HUD) */
void hud_print(uint8_t x, uint8_t y, const char *s);  /* the same, queued for the next VBlank */
void far_copy(void *dst, const void *src, uint16_t n);
void load_world_gfx(void);             /* world BG tiles, sprites, palettes */
void load_title_gfx(void);             /* + the title tiles at 208, the title map */
void anim_group(uint8_t g, uint8_t f);  /* animated BG tile group g (4 tiles) to frame f */
void frame_end(void);                  /* wait for VBlank, then latch the scroll */

/* ---- render.c ---- */
void render_reset(void) BANKED;        /* a fresh level view: draw every visible column */
void render_frame(void) BANKED;        /* streaming, changed cells, sprites, HUD */
void hud_draw_all(void) BANKED;
void hud_pause(uint8_t on) BANKED;
void sprites_clear(void) BANKED;
void fmt_u32(char *out, uint32_t v, uint8_t digits) BANKED;        /* zero-padded */
void fmt_time(char *out, uint32_t frames) BANKED;                  /* "MM:SS.CC" */

/* ---- screens.c ---- */
uint16_t title_screen(void) BANKED;    /* returns the seed to play */
uint8_t pause_screen(void) BANKED;     /* 0 resume, 1 restart the seed, 2 quit to title */
void game_over_screen(void) BANKED;
void save_load(void) BANKED;
extern uint32_t best_score;
extern uint16_t best_dist;
extern uint16_t last_seed;

#endif
