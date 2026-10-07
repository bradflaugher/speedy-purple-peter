/* test_sound.c - host tests for the SPEEDY PURPLE PETER sound engine (src/gb/sound.c).
 *
 *   cc -std=c99 -O2 -Wall -Wextra -Werror -DHOST_TEST -Isrc/gb -Isrc/core \
 *      -o build/test_sound tests/test_sound.c src/gb/sound.c && ./build/test_sound
 *
 * A fake APU: every register write goes through host_snd_write(), which checks it against
 * the hardware rules (DESIGN of the engine: sound.c header) and keeps statistics:
 *   - no DAC off, no NR51 / NR50 / NR52 change after init, no rising envelope, no note
 *     triggered at a level its envelope never leaves (pops / stuck tones);
 *   - wave RAM only written while CH3 is stopped and muted, CH3 never retriggered while it
 *     plays (DMG wave RAM corruption), the CH3 level moves one step a frame at most;
 *   - the song never writes a channel an sfx owns; sfx write only their own channels and
 *     never CH3; channels come back;
 * plus a static walk of all song and sfx data (ranges, keys, loop lengths) and timing
 * checks for the jingles and the hurry tempo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"
#include "hw_sound.h"
#define SND_FREQ_TABLE
#include "sound_notes.h"
#include "sound_data.h"

static int n_pass, n_fail;
#define CHECK(cond, ...) do { if (cond) n_pass++; else { n_fail++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ================================================================== fake APU */
uint8_t host_snd_src;
static uint8_t regs[0x40];
static int in_init;
static uint8_t ch3_on, ch3_stopping;
static unsigned long n_ticks, nr32_tick = ~0ul;

static unsigned long w_total, w_bad_reg, w_bad_nrx4, w_dac_off, w_nr51, w_nr50, w_nr52, w_env_up,
    w_held, w_wave_on, w_wave_loud, w_ch3_retrig, w_nr32_jump, w_music_owned, w_sfx_outside,
    w_bad_freq, w_nr10_music;
static unsigned long trig[4], trig_music[4], wave_loads;
static unsigned long tick_writes, max_tick_writes;

static int reg_ch(uint8_t r)
{
    if (r >= 0x10 && r <= 0x14) return 0;
    if (r >= 0x16 && r <= 0x19) return 1;
    if ((r >= 0x1A && r <= 0x1E) || (r >= 0x30 && r <= 0x3F)) return 2;
    if (r >= 0x20 && r <= 0x23) return 3;
    return -1;
}

static int nr32_level(uint8_t v)          /* 0 = mute .. 3 = 100% */
{
    switch (v & 0x60) {
    case 0x20: return 3;
    case 0x40: return 2;
    case 0x60: return 1;
    default: return 0;
    }
}

static int is_freq(unsigned x)
{
    int i;
    for (i = 0; i < SND_NOTES; i++)
        if (snd_freq[i] == x)
            return 1;
    return 0;
}

void host_snd_write(uint8_t r, uint8_t v)
{
    int c = reg_ch(r);
    uint8_t own = snd_debug_owned();
    w_total++;
    tick_writes++;
    if (!((r >= 0x10 && r <= 0x26 && r != 0x15 && r != 0x1F) || (r >= 0x30 && r <= 0x3F)))
        w_bad_reg++;
    if ((r == 0x14 || r == 0x19 || r == 0x1E) && (v & 0x38))
        w_bad_nrx4++;
    if (r == 0x23 && (v & 0x3F))
        w_bad_nrx4++;
    if (!in_init) {
        if (((r == 0x12 || r == 0x17 || r == 0x21) && !(v & 0xF8)) || (r == 0x1A && !(v & 0x80)))
            w_dac_off++;
        if (r == 0x25) w_nr51++;
        if (r == 0x24) w_nr50++;
        if (r == 0x26) w_nr52++;
        if ((r == 0x12 || r == 0x17 || r == 0x21) && (v & 0x08))
            w_env_up++;
        if (r == 0x1C) {
            int d = nr32_level(v) - nr32_level(regs[0x1C]);
            if (d > 1 || d < -1 || (nr32_tick == n_ticks && d))
                w_nr32_jump++;
            nr32_tick = n_ticks;
        }
        if (r == 0x10 && host_snd_src == SND_SRC_MUSIC)
            w_nr10_music++;
    }
    /* triggers */
    if ((r == 0x14 || r == 0x19 || r == 0x23) && (v & 0x80)) {
        uint8_t env = regs[r == 0x14 ? 0x12 : r == 0x19 ? 0x17 : 0x21];
        if ((env & 0xF0) && !(env & 7) && !(v & 0x40))
            w_held++;                       /* a level nothing fades */
        trig[c]++;
        if (host_snd_src == SND_SRC_MUSIC) {
            trig_music[c]++;
            if (c < 2 && !is_freq(regs[r - 1] | ((v & 7) << 8)))
                w_bad_freq++;
        }
    }
    if (r >= 0x30 && r <= 0x3F) {
        if (ch3_on)
            w_wave_on++;
        if (nr32_level(regs[0x1C]))
            w_wave_loud++;
        if (r == 0x30)
            wave_loads++;
    }
    if (r == 0x1E) {
        if (v & 0x80) {
            if (ch3_on)
                w_ch3_retrig++;
            ch3_on = 1;
            ch3_stopping = 0;
            trig[2]++;
            if (host_snd_src == SND_SRC_MUSIC && !is_freq(regs[0x1D] | ((v & 7) << 8)))
                w_bad_freq++;
        } else if ((v & 0x40) && regs[0x1B] == 0xFF && ch3_on) {
            ch3_stopping = 1;               /* length 1: gone within 1/256 s */
        }
    }
    if (r == 0x1D && host_snd_src == SND_SRC_MUSIC) {
        /* a bass pitch change: the next NR34 write completes it */
    }
    if (c >= 0) {
        if (host_snd_src == SND_SRC_MUSIC && (own & (1 << c)) && !in_init)
            w_music_owned++;
        if (host_snd_src == SND_SRC_SFX && (!(own & (1 << c)) || c == 2))
            w_sfx_outside++;
    } else if (host_snd_src == SND_SRC_SFX) {
        w_sfx_outside++;
    }
    if (r < 0x40)
        regs[r] = v;
}

static void reset_stats(void)
{
    w_total = w_bad_reg = w_bad_nrx4 = w_dac_off = w_nr51 = w_nr50 = w_nr52 = w_env_up = 0;
    w_held = w_wave_on = w_wave_loud = w_ch3_retrig = w_nr32_jump = w_music_owned = 0;
    w_sfx_outside = w_bad_freq = w_nr10_music = 0;
    memset(trig, 0, sizeof trig);
    memset(trig_music, 0, sizeof trig_music);
    wave_loads = 0;
}

static void check_clean(const char *what)
{
    CHECK(w_bad_reg == 0, "%s: %lu writes to invalid registers", what, w_bad_reg);
    CHECK(w_bad_nrx4 == 0, "%s: %lu bad NRx4 values", what, w_bad_nrx4);
    CHECK(w_dac_off == 0, "%s: %lu DACs switched off (pops)", what, w_dac_off);
    CHECK(w_nr51 == 0, "%s: %lu NR51 writes after init (pops)", what, w_nr51);
    CHECK(w_nr50 == 0, "%s: %lu NR50 writes after init", what, w_nr50);
    CHECK(w_nr52 == 0, "%s: %lu NR52 writes after init", what, w_nr52);
    CHECK(w_env_up == 0, "%s: %lu rising envelopes (zombie clicks)", what, w_env_up);
    CHECK(w_held == 0, "%s: %lu notes held at a fixed level", what, w_held);
    CHECK(w_wave_on == 0, "%s: %lu wave RAM writes while CH3 plays", what, w_wave_on);
    CHECK(w_wave_loud == 0, "%s: %lu wave RAM writes with CH3 not muted", what, w_wave_loud);
    CHECK(w_ch3_retrig == 0, "%s: %lu CH3 retriggers while playing", what, w_ch3_retrig);
    CHECK(w_nr32_jump == 0, "%s: %lu CH3 level jumps", what, w_nr32_jump);
    CHECK(w_music_owned == 0, "%s: %lu song writes on sfx-owned channels", what, w_music_owned);
    CHECK(w_sfx_outside == 0, "%s: %lu sfx writes outside their channels", what, w_sfx_outside);
    CHECK(w_bad_freq == 0, "%s: %lu song notes off the note table", what, w_bad_freq);
    CHECK(w_nr10_music == 0, "%s: %lu song writes to NR10", what, w_nr10_music);
    CHECK(snd_debug_late() == 0, "%s: %u rows not parsed ahead", what, snd_debug_late());
}

static void tick(void)
{
    if (ch3_stopping)
        ch3_on = ch3_stopping = 0;
    tick_writes = 0;
    snd_tick();
    if (tick_writes > max_tick_writes)
        max_tick_writes = tick_writes;
    n_ticks++;
}

static void ticks(int n)
{
    while (n-- > 0)
        tick();
}

static void fresh(void)
{
    memset(regs, 0, sizeof regs);
    ch3_on = ch3_stopping = 0;
    in_init = 1;
    snd_init();
    in_init = 0;
    reset_stats();
}

static const char *mus_name[NUM_MUS] = { "NONE", "MAIN", "NOVA", "TITLE", "DEATH", "GAMEOVER", "HURRY" };
static const char *sfx_name[NUM_SFX] = { "JUMP", "JUMP_BIG", "STOMP", "KICK", "BUMP", "BREAK", "COIN",
    "SPROUT", "POWERUP", "POWERDOWN", "1UP", "SHOT", "FLAG", "TICK", "PAUSE", "LAUNCH", "SELECT" };

/* ================================================================== static data walk */
/* key masks: bit = pitch class (C = bit 0) */
#define PC(n) (1u << ((n) % 12))
static const unsigned KEY_D = PC(D2) | PC(E2) | PC(Fs2) | PC(G2) | PC(A2) | PC(B2) | PC(Cs2);
static const unsigned KEY_E = PC(E2) | PC(Fs2) | PC(Gs2) | PC(A2) | PC(B2) | PC(Cs2) | PC(Ds2);

typedef struct {
    int rows;          /* rows from the start to END / the first LOOP */
    int loop_rows;     /* rows of one loop (from the loop point back to LOOP), 0 for END */
    int notes;
    int bad;           /* errors */
    int offkey;
} walk_t;

typedef struct { int len, tr, ins; const uint8_t *ins_first, *ins_cur; } wstate_t;   /* carried on through the loop, like the player */

static void walk_stream(const uint8_t *p, int ch, unsigned key, int *rows, walk_t *w, int *ended,
                        wstate_t *st)
{
    int len = st->len, tr = st->tr, ins = st->ins, guard = 0;
    const uint8_t *ret = 0;
    *ended = 0;
    for (;;) {
        uint8_t b = *p++;
        if (++guard > 20000) { w->bad++; printf("  runaway stream ch%d\n", ch); return; }
        if (b < SB_NOTES) {
            if (ch == 3) {
                if (b >= NUM_DRUMS) { w->bad++; printf("  ch3 bad drum %u\n", b); }
            } else {
                int n = b + (int8_t)tr;
                int top = ch == 2 ? SND_NOTES - 1 - 12 : SND_NOTES - 1;
                if (n < 0 || n > top) { w->bad++; printf("  ch%d note %d out of range\n", ch, n); }
                else if (key && !(key & PC(n))) {
                    w->offkey++;
                    printf("  ch%d off-key note %d (pc %d)\n", ch, n, n % 12);
                }
                if (ins < 0) { w->bad++; printf("  ch%d note before INS\n", ch); }
            }
            w->notes++;
            *rows += len;
            continue;
        }
        if (b >= SB_LEN) { len = b - (SB_LEN - 1); continue; }
        switch (b) {
        case SB_REST: *rows += len; break;
        case SB_END: *ended = 1; goto out;
        case SB_LOOP:
            if (ret) { w->bad++; printf("  LOOP inside a pattern\n"); }
            if (st->ins_first && memcmp(st->ins_first, st->ins_cur, ch == 2 ? 3 : 2)) {
                w->bad++;
                printf("  ch%d loops with another instrument than it starts with\n", ch);
            }
            goto out;
        case SB_CALL:
            if (ret) { w->bad++; printf("  nested CALL\n"); return; }
            if (*p >= NUM_SP) { w->bad++; printf("  bad pattern %u\n", *p); return; }
            ret = p + 1;
            p = snd_pat[*p];
            break;
        case SB_RET:
            if (!ret) { w->bad++; printf("  RET outside a pattern\n"); return; }
            p = ret;
            ret = 0;
            break;
        case SB_TR: tr = *p++; break;
        case SB_INS:
            ins = 1;
            if (!st->ins_first)
                st->ins_first = p;
            st->ins_cur = p;
            if (ch == 2) {
                if (p[0] > 3 || p[2] > 3 || p[2] == 0) { w->bad++; printf("  bad bass instrument\n"); }
                p += 3;
            } else if (ch < 2) {
                if (!(p[1] >> 4) || (p[1] & 0x08) || !(p[1] & 7) || (p[0] & 0x3F)) {
                    w->bad++;
                    printf("  ch%d bad pulse instrument %02X %02X\n", ch, p[0], p[1]);
                }
                p += 2;
            } else {
                w->bad++;
                printf("  INS on the drums\n");
            }
            break;
        default:
            w->bad++;
            printf("  ch%d bad byte 0x%02X\n", ch, b);
            return;
        }
    }
out:
    st->len = len;
    st->tr = tr;
    st->ins = ins;
}

static void test_song_data(void)
{
    int s, c;
    for (s = 1; s < NUM_MUS; s++) {
        const uint8_t * const *ch = snd_song_ch[s];
        uint8_t rate = snd_song_rate[s], rate_h = snd_song_rate_h[s], flags = snd_song_flags[s];
        unsigned key = s == MUS_MAIN || s == MUS_TITLE ? KEY_D : s == MUS_NOVA ? KEY_E : 0;
        int loop_rows[4] = { 0, 0, 0, 0 };
        int max_end = 0;
        CHECK(rate > 0 && rate_h >= rate, "%s: rates", mus_name[s]);
        CHECK(rate_h <= 85, "%s: rows at least 3 frames apart (parse-ahead)", mus_name[s]);
        CHECK(snd_song_wave[s] < NUM_WAVES, "%s: wave", mus_name[s]);
        for (c = 0; c < 4; c++) {
            walk_t w;
            wstate_t st = { 1, 0, -1, 0, 0 };
            int ended, rows = 0;
            memset(&w, 0, sizeof w);
            if (!ch[c])
                continue;
            walk_stream(ch[c], c, key, &rows, &w, &ended, &st);
            if (flags & SF_LOOPS) {
                CHECK(!ended && ch[4 + c], "%s ch%d: loops", mus_name[s], c);
                if (ch[4 + c]) {
                    int lr = 0;
                    walk_stream(ch[4 + c], c, key, &lr, &w, &ended, &st);
                    loop_rows[c] = lr;
                    CHECK(lr > 0, "%s ch%d: loop has rows", mus_name[s], c);
                }
            } else {
                CHECK(ended, "%s ch%d: jingle channel ends", mus_name[s], c);
            }
            CHECK(w.bad == 0, "%s ch%d: %d data errors", mus_name[s], c, w.bad);
            CHECK(w.offkey == 0, "%s ch%d: %d notes out of key", mus_name[s], c, w.offkey);
            CHECK(w.notes > 0, "%s ch%d: has notes", mus_name[s], c);
            if (rows > max_end)
                max_end = rows;
        }
        if (flags & SF_LOOPS) {
            double secs;
            for (c = 1; c < 4; c++)
                CHECK(loop_rows[c] == loop_rows[0], "%s: loop rows ch%d %d vs ch0 %d",
                      mus_name[s], c, loop_rows[c], loop_rows[0]);
            secs = loop_rows[1] * 256.0 / rate / 59.73;
            printf("  %-8s loop %d rows = %.1f s (hurry %.1f s)\n", mus_name[s], loop_rows[1], secs,
                   loop_rows[1] * 256.0 / rate_h / 59.73);
            if (s == MUS_MAIN)
                CHECK(secs >= 30 && secs <= 45, "MAIN loop %.1f s (30-45)", secs);
            else
                CHECK(secs >= 8 && secs <= 60, "%s loop %.1f s", mus_name[s], secs);
        } else {
            double secs = max_end * 256.0 / rate / 59.73;
            printf("  %-8s jingle %d rows = %.2f s\n", mus_name[s], max_end, secs);
            if (s == MUS_DEATH) CHECK(secs > 1.4 && secs < 2.6, "DEATH %.2f s", secs);
            if (s == MUS_GAMEOVER) CHECK(secs > 2.4 && secs < 3.8, "GAMEOVER %.2f s", secs);
            if (s == MUS_HURRY) CHECK(secs > 1.2 && secs < 1.9, "HURRY %.2f s", secs);
        }
    }
    /* hurry is ~1.5x for the loops */
    CHECK(snd_song_rate_h[MUS_MAIN] * 2 >= snd_song_rate[MUS_MAIN] * 3 - 2 &&
          snd_song_rate_h[MUS_MAIN] * 2 <= snd_song_rate[MUS_MAIN] * 3 + 2, "MAIN hurry 1.5x");
    CHECK(snd_song_rate_h[MUS_NOVA] * 2 >= snd_song_rate[MUS_NOVA] * 3 - 2 &&
          snd_song_rate_h[MUS_NOVA] * 2 <= snd_song_rate[MUS_NOVA] * 3 + 2, "NOVA hurry 1.5x");
}

static void test_sfx_data(void)
{
    int id, i;
    static const uint8_t lo[3] = { 0x10, 0x16, 0x20 }, hi[3] = { 0x14, 0x19, 0x23 };
    for (id = 0; id < NUM_SFX; id++) {
        int used = 0;
        for (i = 0; i < 3; i++) {
            const uint8_t *p = i == 0 ? snd_sfx_s0[id] : i == 1 ? snd_sfx_s1[id] : snd_sfx_s2[id];
            CHECK(!p == !(snd_sfx_mask[id] & (i == 2 ? 8 : 1 << i)), "%s: mask matches slot %d", sfx_name[id], i);
            int frames = 0, n = 0, ok = 1, has_trig = 0;
            if (!p)
                continue;
            used++;
            for (;;) {
                uint8_t b = *p++;
                if (++n > 400) { ok = 0; break; }
                if (!b) break;
                if (b >= 0x40) { frames += b - 0x40 + 1; continue; }
                if (b < lo[i] || b > hi[i]) { ok = 0; printf("  %s: reg 0x%02X on slot %d\n", sfx_name[id], b, i); }
                if ((b == 0x14 || b == 0x19 || b == 0x23) && (*p & 0x80)) has_trig = 1;
                p++;
            }
            CHECK(ok, "%s slot %d: script valid", sfx_name[id], i);
            CHECK(has_trig, "%s slot %d: plays a note", sfx_name[id], i);
            CHECK(frames >= 2 && frames <= 90, "%s slot %d: %d frames", sfx_name[id], i, frames);
            if (i == 0)
                CHECK(snd_sfx_s0[id][0] == 0x10, "%s: CH1 script sets NR10 first", sfx_name[id]);
        }
        CHECK(used >= 1, "%s: has a script", sfx_name[id]);
    }
}

/* ================================================================== dynamic tests */
static void test_init(void)
{
    fresh();
    CHECK(regs[0x26] == 0x80 && regs[0x24] == 0x77 && regs[0x25] == 0xFF, "init: APU on, master, panning");
    CHECK(regs[0x1A] == 0x80 && (regs[0x12] & 0xF8) && (regs[0x17] & 0xF8) && (regs[0x21] & 0xF8),
          "init: every DAC on");
    CHECK(music_done(), "nothing playing after init");
    ticks(120);
    CHECK(w_total == 0, "idle writes nothing (%lu)", w_total);
    sfx_play(NUM_SFX);                          /* out-of-range ids are ignored */
    sfx_play(255);
    music_play(NUM_MUS);
    music_play(255);
    ticks(60);
    CHECK(w_total == 0 && music_done() && !snd_debug_owned(), "bad ids ignored (%lu writes)", w_total);
    check_clean("idle");
}

static void test_songs(void)
{
    int s;
    char what[64];
    for (s = 1; s < NUM_MUS; s++) {
        fresh();
        music_play((uint8_t)s);
        CHECK(!music_done(), "%s: not done right after the request", mus_name[s]);
        ticks(60 * 60);
        sprintf(what, "song %s", mus_name[s]);
        check_clean(what);
        CHECK(trig_music[1] >= 4, "%s: lead plays (%lu)", what, trig_music[1]);
        CHECK(trig[2] == 1 || s == MUS_HURRY, "%s: bass triggered once (%lu)", what, trig[2]);
        CHECK(trig_music[3] >= 1, "%s: drums (%lu)", what, trig_music[3]);
        CHECK(wave_loads == 1, "%s: one wave load (%lu)", what, wave_loads);
        if (snd_song_flags[s] & SF_LOOPS)
            CHECK(!music_done() && snd_debug_song() == s, "%s: still looping", what);
        else if (s == MUS_HURRY)
            CHECK(!music_done() && snd_debug_song() == MUS_MAIN, "%s: flows into MAIN", what);
        else
            CHECK(music_done(), "%s: done", what);
    }
    /* switching between songs with different waves swaps the wave cleanly */
    fresh();
    for (s = 0; s < 12; s++) {
        music_play(s & 1 ? MUS_TITLE : MUS_MAIN);
        ticks(37 + s * 13);
    }
    ticks(30);
    check_clean("song switching");
    CHECK(wave_loads >= 6, "wave swapped on song switches (%lu)", wave_loads);
}

static int frames_until_done(uint8_t s, int limit)
{
    int f = 0;
    music_play(s);
    while (f < limit) {
        tick();
        f++;
        if (music_done())
            break;
    }
    return f;
}

static void test_jingles(void)
{
    int f;
    fresh();
    music_play(MUS_MAIN);
    ticks(300);
    f = frames_until_done(MUS_DEATH, 600);
    printf("  DEATH done after %d frames\n", f);
    CHECK(f >= 90 && f <= 150, "DEATH ~2 s (%d frames)", f);
    f = frames_until_done(MUS_GAMEOVER, 600);
    printf("  GAMEOVER done after %d frames\n", f);
    CHECK(f >= 150 && f <= 230, "GAMEOVER ~3 s (%d frames)", f);
    ticks(120);
    CHECK(music_done(), "still done");
    check_clean("jingles");
    /* MUS_NONE stops at once, and the bass fades */
    music_play(MUS_MAIN);
    ticks(200);
    music_play(MUS_NONE);
    ticks(10);
    CHECK(music_done(), "MUS_NONE: done");
    CHECK(nr32_level(regs[0x1C]) == 0, "MUS_NONE: bass faded");
    reset_stats();
    ticks(300);
    CHECK(w_total == 0, "MUS_NONE: silent (%lu writes)", w_total);
}

static unsigned long lead_notes(uint8_t s, int hurry, int frames)
{
    fresh();
    music_hurry((uint8_t)hurry);
    music_play(s);
    ticks(frames);
    return trig_music[1] + trig_music[3];
}

static void test_hurry(void)
{
    unsigned long n0, n1;
    int f = 0;
    double r;
    n0 = lead_notes(MUS_MAIN, 0, 60 * 120);
    n1 = lead_notes(MUS_MAIN, 1, 60 * 120);
    r = (double)n1 / n0;
    printf("  MAIN notes in 2 min: %lu, hurry %lu (x%.2f)\n", n0, n1, r);
    CHECK(r > 1.35 && r < 1.65, "MAIN hurry ~1.5x (%.2f)", r);
    n0 = lead_notes(MUS_NOVA, 0, 60 * 60);
    n1 = lead_notes(MUS_NOVA, 1, 60 * 60);
    r = (double)n1 / n0;
    CHECK(r > 1.35 && r < 1.65, "NOVA hurry ~1.5x (%.2f)", r);
    n0 = lead_notes(MUS_TITLE, 0, 60 * 60);
    n1 = lead_notes(MUS_TITLE, 1, 60 * 60);
    CHECK(n0 == n1, "TITLE ignores hurry (%lu vs %lu)", n0, n1);

    /* MUS_HURRY: the fanfare, then MAIN in hurry tempo, from the top */
    fresh();
    music_play(MUS_MAIN);
    ticks(1000);
    music_play(MUS_HURRY);
    while (f < 400) {
        tick();
        f++;
        if (snd_debug_song() == MUS_MAIN)
            break;
    }
    printf("  HURRY fanfare: %d frames\n", f);
    CHECK(f >= 75 && f <= 115, "HURRY fanfare ~1.5 s (%d frames)", f);
    CHECK(snd_debug_hurry() == 1, "hurry on after the fanfare");
    CHECK(!music_done(), "MAIN goes on");
    reset_stats();
    ticks(60 * 120);
    r = (double)(trig_music[1] + trig_music[3]) / n0;  /* (n0 = title, unused) */
    (void)r;
    CHECK(trig_music[1] + trig_music[3] > lead_notes(MUS_MAIN, 0, 60 * 120) * 135 / 100,
          "MAIN after HURRY is in hurry tempo");
    /* jingles and the title clear it; music_hurry(0) too */
    fresh();
    music_hurry(1);
    music_play(MUS_NOVA);
    ticks(10);
    CHECK(snd_debug_hurry() == 1, "NOVA keeps hurry");
    music_play(MUS_DEATH);
    ticks(2);
    CHECK(snd_debug_hurry() == 0, "DEATH clears hurry");
    music_hurry(1);
    ticks(2);
    music_hurry(0);
    ticks(2);
    CHECK(snd_debug_hurry() == 0, "music_hurry(0)");
    check_clean("hurry");
}

static int run_until_free(int limit)
{
    int f = 0;
    while (f < limit) {
        tick();
        f++;
        if (!snd_debug_owned())
            break;
    }
    return f;
}

static void test_sfx_each(void)
{
    int id, s;
    char what[64];
    for (s = 0; s < 3; s++) {
        uint8_t song = s == 0 ? MUS_NONE : s == 1 ? MUS_MAIN : MUS_NOVA;
        for (id = 0; id < NUM_SFX; id++) {
            int f;
            unsigned long t0;
            fresh();
            music_play(song);
            ticks(97);
            sfx_play((uint8_t)id);
            tick();
            tick();                             /* (none start on a row frame) */
    CHECK(snd_debug_owned() != 0, "%s: took its channels", sfx_name[id]);
            CHECK(trig[0] + trig[1] + trig[3] > 0, "%s: sounded", sfx_name[id]);
            f = run_until_free(300);
            sprintf(what, "sfx %s over %s", sfx_name[id], mus_name[song]);
            CHECK(f < 100, "%s: gives the channels back (%d frames)", what, f);
            CHECK(snd_debug_owned() == 0, "%s: all channels back", what);
            check_clean(what);
            if (song != MUS_NONE) {
                t0 = trig_music[0] + trig_music[1] + trig_music[3];
                ticks(120);
                CHECK(trig_music[0] + trig_music[1] + trig_music[3] > t0, "%s: song goes on", what);
            }
        }
    }
}

static void test_sfx_priority(void)
{
    int i;
    fresh();
    music_play(MUS_MAIN);
    ticks(50);
    /* PAUSE (7) is not cut by a JUMP (3) */
    /* (a new sfx may wait one frame: none start on a frame that plays a song row) */
    sfx_play(SFX_PAUSE);
    ticks(2);
    trig[0] = 0;
    sfx_play(SFX_JUMP);
    ticks(2);
    CHECK(trig[0] == 0, "JUMP does not cut PAUSE");
    run_until_free(200);
    /* coins retrigger every few frames, ticks every frame */
    reset_stats();
    for (i = 0; i < 20; i++) {
        sfx_play(SFX_COIN);
        ticks(3);
    }
    CHECK(trig[0] >= 20, "coins retrigger (%lu)", trig[0]);
    run_until_free(100);
    reset_stats();
    for (i = 0; i < 60; i++) {
        sfx_play(SFX_TICK);
        tick();
    }
    CHECK(trig[0] >= 48, "ticks retrigger every frame (%lu; two in a frame after a row merge)", trig[0]);
    run_until_free(100);
    CHECK(snd_debug_owned() == 0, "ticks give CH1 back");
    /* a coin during a jump plays (equal priority) */
    sfx_play(SFX_JUMP);
    ticks(3);
    reset_stats();
    sfx_play(SFX_COIN);
    ticks(2);
    CHECK(trig[0] == 1, "COIN over JUMP (%lu)", trig[0]);
    /* TICK does not cut a 1UP */
    sfx_play(SFX_1UP);
    ticks(2);
    reset_stats();
    sfx_play(SFX_TICK);
    ticks(2);
    CHECK(trig[0] == 0, "TICK does not cut 1UP");
    run_until_free(200);
    check_clean("priorities");
}

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void test_fuzz(void)
{
    long f;
    int k;
    for (k = 0; k < 4; k++) {
        fresh();
        rng = 0x9E3779B9u * (uint32_t)(k + 1);
        for (f = 0; f < 100000; f++) {
            uint32_t r = rnd();
            if ((r & 0xFF) < 12)
                sfx_play((uint8_t)((r >> 8) % NUM_SFX));
            if ((r & 0xFFF0) == 0x1230)
                music_play((uint8_t)((r >> 16) % NUM_MUS));
            if ((r & 0xFFF00) == 0x45600)
                music_hurry((uint8_t)((r >> 20) & 1));
            if ((r & 0x3F000) == 0x15000 && k == 3)
                sfx_play(SFX_TICK);
            if (f % 7 == 0 && k == 2)
                sfx_play(SFX_COIN);
            tick();
        }
        check_clean("fuzz");
        CHECK(trig_music[1] > 1000, "fuzz %d: music kept playing (%lu)", k, trig_music[1]);
        run_until_free(300);
        CHECK(snd_debug_owned() == 0, "fuzz %d: every channel back", k);
        music_play(MUS_NONE);
        ticks(10);
        CHECK(music_done(), "fuzz %d: stops", k);
        /* request spam: several requests a frame */
        for (f = 0; f < 5000; f++) {
            uint32_t r = rnd();
            sfx_play((uint8_t)(r % NUM_SFX));
            sfx_play((uint8_t)((r >> 5) % NUM_SFX));
            sfx_play((uint8_t)((r >> 10) % NUM_SFX));
            sfx_play((uint8_t)((r >> 15) % NUM_SFX));
            if (!(r & 0x300000))
                music_play((uint8_t)((r >> 22) % NUM_MUS));
            tick();
        }
        check_clean("request spam");
        run_until_free(300);
        CHECK(snd_debug_owned() == 0, "spam %d: every channel back", k);
    }
    printf("  max register writes in one tick: %lu\n", max_tick_writes);
    CHECK(max_tick_writes <= 48, "writes per tick bounded (%lu)", max_tick_writes);
}

int main(void)
{
    test_song_data();
    test_sfx_data();
    test_init();
    test_songs();
    test_jingles();
    test_hurry();
    test_sfx_each();
    test_sfx_priority();
    test_fuzz();
    printf("test_sound: %d passed, %d failed\n", n_pass, n_fail);
    return n_fail ? 1 : 0;
}
